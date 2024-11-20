#include "vm/vm.h"
#include "vm/swap.h"
#include "threads/pte.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
void
free_swap_map_init (void) 
{
  swap_device = block_get_role(BLOCK_SWAP);
  free_swap_map = bitmap_create (block_size (swap_device));
  if (free_swap_map == NULL)
    PANIC ("bitmap creation failed--swap device is too large");
  bitmap_mark (free_swap_map, 0);
}

unsigned swap_hash_func(const struct hash_elem *e, void *aux) {
   struct swap_frame *sf = hash_entry(e, struct swap_frame, elem);
   
   return hash_int(sf->upage);
}

bool swap_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
    struct swap_frame *sf_a = hash_entry(a, struct swap_frame, elem);
    struct swap_frame *sf_b = hash_entry(b, struct swap_frame, elem);
    return sf_a->upage < sf_b->upage; // 比较的方式
}
void swap_table_init(struct hash *h){
    hash_init(h, swap_hash_func, swap_less_func, NULL);
}

// 向swap空间写入数据，从扇区bsn开始写入size大小的数据
static void swap_write_data(uint32_t bsn, void * buf, uint32_t size){
  uint32_t count = (size+BLOCK_SECTOR_SIZE-1)/BLOCK_SECTOR_SIZE;
  for(int i=0, w=0; i<count; ++i,w+=BLOCK_SECTOR_SIZE){
      block_write(swap_device, bsn+i, buf+w);
  }
}
// 从swap空间读取数据，从扇区bsn开始读取size大小的数据
static void swap_read_data(uint32_t bsn, void * buf, uint32_t size){
  uint32_t count = (size+BLOCK_SECTOR_SIZE-1)/BLOCK_SECTOR_SIZE;
  for(int i=0, r=0; i<count; ++i,r+=BLOCK_SECTOR_SIZE){
      block_read (swap_device, bsn+i, buf+r);
  }
}
uint32_t write_swap(void* buf, int32_t size){
    uint32_t bs = (size+BLOCK_SECTOR_SIZE-1)/BLOCK_SECTOR_SIZE;
    uint32_t sc = bitmap_scan_and_flip(free_swap_map, 1, bs, false);
    // printf("bs %d\n", sc);
    for(int i=0, w=0; i<bs; ++i,w+=BLOCK_SECTOR_SIZE){
        block_write(swap_device, sc+i, buf+w);
    }
    return sc;
}

uint32_t rewrite_swap(struct thread* t, uint32_t upage){
  struct swap_frame* sf = find_swap(&t->swap_table, upage);
  if(sf==NULL)return;
  swap_write_data(sf->bs, sf->upage, PGSIZE);
}

bool read_sawp(uint32_t sc, void *buf, int32_t size){
    uint32_t bs = (size+BLOCK_SECTOR_SIZE-1)/BLOCK_SECTOR_SIZE;
    for(int i=0, r=0; i<bs; ++i,r+=BLOCK_SECTOR_SIZE){
        block_read (swap_device, sc+i, buf+r);
    }
    // ASSERT (bitmap_all (free_swap_map, sc, bs));
    // bitmap_set_multiple (free_swap_map, sc, bs, false);
    return true;
}

struct swap_frame* find_swap(struct hash *ht, uint32_t upage){
  // struct list_elem* e=NULL;
  // struct hash t;
  // hash_find(&t, );
  // for(e=list_begin(list); e!=list_end(list); e=list_next(e)){
  //   struct swap_frame* sf = list_entry(e, struct swap_frame, elem);
  //   if(sf->upage==upage){
  //     return sf;
  //   }
  // }
  struct swap_frame item_to_find;
  int bs = item_to_find.bs;
  item_to_find.upage = upage;  
  struct hash_elem* e = hash_find(ht, &item_to_find.elem);
  if(e!=NULL){
    return hash_entry(e, struct swap_frame, elem);
  }
  return NULL;
}

void remove_swap(struct vm_eara *vma, struct hash *ht){
  if(vma->file==NULL)return;
  uint8_t *kpage = malloc(PGSIZE);
  for(uint32_t upage=vma->start; upage<vma->end; upage+=PGSIZE){
    struct swap_frame * sf = find_swap(ht, upage);
    if(sf!=NULL){
        read_sawp(sf->bs, kpage, PGSIZE);
        file_seek(vma->file, upage-vma->start+vma->offset);
        off_t size = vma->end - upage>PGSIZE?PGSIZE:vma->end-upage;
        file_write(vma->file, kpage, size);
        int count = (PGSIZE+BLOCK_SECTOR_SIZE-1)/BLOCK_SECTOR_SIZE;
        ASSERT (bitmap_all (free_swap_map, sf->bs, count));
        bitmap_set_multiple (free_swap_map, sf->bs, count, false);
        hash_delete(ht, &sf->elem);
        free(sf);
    }
  }
  free(kpage);
}

bool swap_out(struct thread * t, uint32_t upage, uint32_t kpage){
  struct swap_frame *sf = find_swap(&t->swap_table, upage);
  if(!sf)return false;
  return read_sawp(sf->bs, kpage, PGSIZE);
}
void swap_in(struct thread * t, uint32_t upage, uint32_t kpage){
  struct page_table_entry* pte = pagedir_get_pte(t->pagedir, upage);
  if(pte==NULL||!pte->dirty)return;
  // printf("write upage: %p --- %p\n", upage, upage+PGSIZE-1);
  struct vm_eara *vma = find_vma(&t->vm_list, upage);
  if(vma->file!=NULL&&file_get_inode(vma->file)!=file_get_inode(t->exec)){
    file_seek(vma->file, upage-vma->start+vma->offset);
    off_t size = vma->end - upage>PGSIZE?PGSIZE:vma->end-upage;
    file_write(vma->file, kpage, size);
  }
  //该页是脏页
  struct swap_frame * sf = NULL;

  //脏页在swap空间已有备份，直接重写
  sf = find_swap(&t->swap_table, upage);
  if(sf!=NULL){
    swap_write_data(sf->bs, kpage, PGSIZE);
    return;
  }
  //脏页在swap空间无备份，进行备份
  sf = (struct swap_frame *)malloc(sizeof(*sf));
  sf->bs    = write_swap(kpage, PGSIZE);
  sf->upage = upage;
  // list_push_back(&t->swap_list, &sf->elem);
  hash_insert(&t->swap_table, &sf->elem);
}
void swap_hash_cleanup(struct hash_elem *e, void *aux) {
    struct swap_frame *sf = hash_entry(e, struct swap_frame, elem);
    int count = (PGSIZE+BLOCK_SECTOR_SIZE-1)/BLOCK_SECTOR_SIZE;
    ASSERT (bitmap_all (free_swap_map, sf->bs, count));
    bitmap_set_multiple (free_swap_map, sf->bs, count, false);
    free(sf);
}
void free_swap_frame(struct thread* t){
  hash_destroy(&t->swap_table, swap_hash_cleanup);
}
