#include "filesys/buffer.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"
#include "lib/string.h"
#include "lib/debug.h"
#include "lib/round.h"
static const int bk_size = (BUFFER_SIZE/BLOCK_SECTOR_SIZE);
static struct buffer bufcache[BUFFER_LENGTH];
static struct{
    struct lock lock;
    struct list cachelist;
}bcache;


void buffer_init(){
    list_init(&bcache.cachelist);
    lock_init(&bcache.lock);
    for(int i=0; i<BUFFER_LENGTH; ++i){
        lock_init(&bufcache[i].lock);
        bufcache[i].blockno = - bk_size;
        list_push_back(&bcache.cachelist, &bufcache[i].elem);
    }
}
static void bahead(struct block *dev, block_sector_t blockno){
    lock_acquire(&bcache.lock);

    struct list_elem * e=NULL;
    struct list_elem * last = NULL;
    for(e=list_begin(&bcache.cachelist); e!=list_end(&bcache.cachelist); e=list_next(e)){
        struct buffer * b = list_entry(e, struct buffer, elem);
        if(b->dev!=NULL&&b->dev==dev&&(blockno/bk_size)==(b->blockno/bk_size)){
            lock_release(&bcache.lock);
            return;
        }
        if(b->refcnt==0)last=e;
    }
    if(last){
        struct buffer * b = list_entry(last, struct buffer, elem);
        lock_acquire(&b->lock);
        if(b->dirty){
            bwrite(b);
        }
        b->dev = dev;
        b->blockno = (blockno/bk_size)*bk_size;
        for(int bk_no=0; bk_no<bk_size; ++bk_no){
            block_read(b->dev, b->blockno+bk_no, b->cache+BLOCK_SECTOR_SIZE*bk_no);
        }
        b->valid  = 1;
        b->refcnt = 0;
        lock_release(&b->lock);
    }
    lock_release(&bcache.lock);
}
static struct buffer* bget(struct block *dev, block_sector_t blockno){
    lock_acquire(&bcache.lock);

    struct list_elem * e=NULL;
    struct list_elem * last = NULL;
    for(e=list_begin(&bcache.cachelist); e!=list_end(&bcache.cachelist); e=list_next(e)){
        struct buffer * b = list_entry(e, struct buffer, elem);
        if(b->dev!=NULL&&b->dev==dev&&(blockno/bk_size)==(b->blockno/bk_size)){
            b->refcnt++;
            lock_acquire(&b->lock);
            lock_release(&bcache.lock);
            return b;
        }
        if(b->refcnt==0)last=e;
    }
    // 使用最近最少使用 （LRU） 未使用的缓冲区。
    // (队头是常使用的buffer，因此从后往前找到第一个refcnt为0的buffer)
    // for(e=list_rbegin(&bcache.cachelist); e!=list_rend(&bcache.cachelist); e=list_prev(e)){
    // }
    if(last){
        struct buffer * b = list_entry(last, struct buffer, elem);
        lock_acquire(&b->lock);
        if(b->dirty){
            bwrite(b);
        }
        b->dev = dev;
        b->blockno = (blockno/bk_size)*bk_size;
        // printf("set sector %d-%d\n", b->blockno, b->blockno+bk_size-1);
        b->valid = 0;
        b->refcnt = 1;
        lock_release(&bcache.lock);
        return b;
    }
    lock_release(&bcache.lock);
    PANIC("buffer ran out of use!!!\n");
}

struct buffer* bread(struct block * dev, block_sector_t blockno){
    struct buffer * b = bget(dev, blockno);
    if(!b->valid) {
        //读取磁盘
        for(int bk_no=0; bk_no<bk_size; ++bk_no){
            block_read(b->dev, b->blockno+bk_no, b->cache+BLOCK_SECTOR_SIZE*bk_no);
        }
        b->valid = 1;
    }
    // if((b->blockno-blockno)*2>=bk_size){
    //     bahead(dev, DIV_ROUND_UP(blockno, bk_size));
    // }
    return b;
}

void bwrite(struct buffer *b){
    if(!lock_held_by_current_thread(&b->lock)){
        printf("It must held lock\n");
    }
    for(int bk_no=0; bk_no<bk_size; ++bk_no){
        block_write(b->dev, b->blockno+bk_no, b->cache+BLOCK_SECTOR_SIZE*bk_no);
    }
    b->dirty = 0;
}

void brelse(struct buffer *b){
    if(!b)return;
    // 分析死锁产生的原因
    lock_release(&b->lock);
    if(b->dirty){
        // periodic_disk_sync(b);
    }
    lock_acquire(&bcache.lock);
    b->refcnt--;
    if(b->refcnt==0){
        list_remove(&b->elem);
        list_push_front(&bcache.cachelist, &b->elem);
    }
    lock_release(&bcache.lock);
}

void *boffset(struct buffer *b, block_sector_t bno){
    return b->cache + (bno-b->blockno)*BLOCK_SECTOR_SIZE;
}

void buffer_write(struct block * dev, block_sector_t sector, size_t ofs, const void * buf_, size_t size){
    ASSERT(ofs + size <= BLOCK_SECTOR_SIZE);
    ASSERT(size);
    ASSERT(sector<=INT32_MAX);
    struct buffer* b = bread(dev, sector);
    memcpy(boffset(b, sector)+ofs, buf_, size);
    b->dirty = 1;
    brelse(b);
}
void buffer_set(struct block * dev, block_sector_t sector, size_t ofs, uint8_t byte, size_t size){
    ASSERT(ofs + size <= BLOCK_SECTOR_SIZE);
    ASSERT(size);
    ASSERT(sector<=INT32_MAX);
    struct buffer* b = bread(dev, sector);
    memset(boffset(b, sector)+ofs, byte,  size);
    b->dirty = 1;
    brelse(b);
}

void buffer_read(struct block * dev, block_sector_t sector, size_t ofs, const void * buf_, size_t size){
    ASSERT(ofs + size <= BLOCK_SECTOR_SIZE);
    ASSERT(sector<=INT32_MAX);
    struct buffer* b = bread(dev, sector);
    memcpy(buf_, boffset(b, sector)+ofs, size);
    brelse(b);
}
#include "devices/timer.h"
#include "threads/thread.h"

static uint64_t delay = 200;

void bwriteback(struct buffer *b){
    timer_sleep(delay);
    // sema_down(&b->sema);
    if(b->dirty){
        bwrite(b);
        b->dirty = 1;
    }
    // sema_up(&b->sema);
}

void periodic_disk_sync(struct buffer *b){
    thread_create("write_back", PRI_MAX, bwriteback, b);
}

void buffer_write_to_disk(){
    lock_acquire(&bcache.lock);
    struct list_elem * e=NULL;
    for(e=list_begin(&bcache.cachelist); e!=list_end(&bcache.cachelist); e=list_next(e)){
        struct buffer * b = list_entry(e, struct buffer, elem);
        if(b->dirty){
            lock_acquire(&b->lock);
            bwrite(b);
            lock_release(&b->lock);
        }
    }
    lock_release(&bcache.lock);
}
static struct io_queue{
    struct lock lock;
    struct list queue;
}io_queue;
struct io_task{
    enum {IO_READ, IO_WRITE} io_type;
    struct semaphore sema;
    struct thread * wait;
    struct buffer * buffer;
    struct list_elem elem;
};

void io_queue_init(){
    list_init(&io_queue.queue);
    lock_init(&io_queue.lock);
    struct io_task task;
    task.io_type = IO_READ;
    task.buffer  = NULL;
    // task.buffer->lock.holder;
    sema_init(&task.sema, 0);
    list_push_front(&io_queue, &task.elem);
    // sema_down(&task.sema);
    thread_block();
    thread_unblock(task.wait);
}

void io_queue_add(struct io_task *task){
    lock_acquire(&io_queue.lock);
    list_push_back(&io_queue.queue, &task->elem);
    lock_release(&io_queue.lock);
    thread_block();
}
void io_queue_exec(){
    struct list_elem * e = NULL;
    while (list_empty(&io_queue)){
        lock_acquire(&io_queue.lock);
        e = list_pop_front(&io_queue);
        struct io_task * task = list_entry(e, struct io_task, elem);
        if(task->io_type==IO_READ){
            // block_read();
        }
        else{
            // block_write();
        }
        lock_release(&io_queue.lock);
        // thread_unblock(b->lock.holder);
    }
    
}
void
bpin(struct buffer *b) {
  lock_acquire(&bcache.lock);
  b->refcnt++;
  lock_release(&bcache.lock);
}

void
bunpin(struct buffer *b) {
  lock_acquire(&bcache.lock);
  b->refcnt--;
  lock_release(&bcache.lock);
}