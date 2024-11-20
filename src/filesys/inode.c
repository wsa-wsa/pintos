#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/buffer.h"

// 空闲的inode编号
static uint32_t free_inode = 1;
/** Identifies an inode. */
/** 识别inode的魔数 */
#define INODE_MAGIC 0x494e4f44
//地址总数量
#define NADDR 12
//间接块数量
#define NINDIRECT 1
//二重间接块数量
#define NDINDIRECT 1
//直接块数量
#define NDIRECT (NADDR-NINDIRECT-NDINDIRECT)
//一个磁盘块可以存储的地址数量
#define NENTRY (BLOCK_SECTOR_SIZE / sizeof(block_sector_t))

#define NLEVEL 3

/** On-disk inode.在磁盘上的inode，长度必须正好为 BLOCK_SECTOR_SIZE 字节。
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
/** 在磁盘上采用顺序存储的方式进行数据的存储 */
struct inode_disk
  {
    uint16_t type;        // File type 文件类型
    uint16_t nlink;       // 硬连接数量
    uint16_t major;       // Major device number (T_DEVICE only)
    uint16_t minor;       // Minor device number (T_DEVICE only)
    // block_sector_t start;               /**< First data sector. 起始块扇区号*/
    block_sector_t addr[NADDR];
    off_t length;                       /**< File size in bytes. */
    unsigned magic;                     /**< Magic number. */
    uint32_t unused[112];               /**< Not used. */
  };

/** Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
/** 返回要为 inode 分配的SIZE字节长度需要分配的扇区数。*/
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/** In-memory inode. */
/** inode 结构体 */
struct inode 
  {
    struct list_elem elem;              /**< Element in inode list. */
    block_sector_t sector;              /**< Sector number of disk location. 所处的扇区号*/
    int open_cnt;                       /**< Number of openers. */
    bool removed;                       /**< True if deleted, false otherwise. */
    int deny_write_cnt;                 /**< 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /**< Inode content. inode的内容*/
    // struct inode_disk表示的是disk中一块连续的区域
  };

int inode_type(struct inode* inode){
  return inode->data.type;
}

void set_inode_type(struct inode* inode, int type){
  inode->data.type = type;

}

int sector_idx_set(uint16_t* idx, size_t sector){
  memset(idx, -1, NLEVEL*sizeof(uint16_t));
  if(sector<NDIRECT){
    idx[0] = sector;
    return 0;
  }else{
    sector-=NDIRECT;
    if(sector<NINDIRECT*NENTRY){
      idx[0]= NDIRECT + sector/NENTRY;
      idx[1]= sector % NENTRY;
      return 1;
    }
    sector -= NINDIRECT*NENTRY;
    idx[0] = NDIRECT + NINDIRECT + sector /(NENTRY*NENTRY);
    idx[1] = (sector/NENTRY)%NENTRY;
    idx[2] = sector % NENTRY;
    return 2;
  }
  return -1;
}

// 所有的inode_disk * d_inode 的内容都不在buffer-cache中????
static block_sector_t
walk_to_sector(struct inode_disk* d_inode, block_sector_t inode_sector,size_t sectors, int alloc){
  uint16_t idx[NLEVEL];
  int indicator = sector_idx_set(idx, sectors);
  block_sector_t* addr = NULL;
  struct buffer * buf  = NULL;
  block_sector_t bk_no = 0;
  for(int level=0; level<=indicator; ++level){
    if(level)
      {
        buf = bread(fs_device, bk_no);
        addr = boffset(buf, bk_no);
      }
    else
      {
        addr = d_inode->addr;
      }
    bk_no = addr[idx[level]];
    if(bk_no == -1){
      if(alloc){
        free_map_allocate(1, &bk_no);
        addr[idx[level]] = bk_no;
        if(level!=indicator)
          buffer_set(fs_device, bk_no, 0, -1, BLOCK_SECTOR_SIZE);
        if(!level)
          buffer_write(fs_device, inode_sector, idx[level]*sizeof(*addr), &addr[idx[level]], sizeof(*addr));
      }else{
        return -1;
      }
    }
    if(level)
      {
        brelse(buf);
      }
  }
  return bk_no;
}

/** Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
/**
 *  根据inode对应的文件偏移量，返回相应块设备扇区号
 */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    {
      return walk_to_sector(&inode->data, inode->sector, pos/BLOCK_SECTOR_SIZE, 0);
    }
  else
    return -1;
}

/** List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/** Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

//inode块分配
static bool 
inode_allocate(struct inode_disk *disk_inode, size_t sectors){
  block_sector_t bk_no = 0;
  printf("l1 index:\n");
  if(sectors<=NDIRECT){
    for(int sec_no=0; sec_no<sectors; ++sec_no){
      free_map_allocate (1, &bk_no);
      printf("%d ", bk_no);
      disk_inode->addr[sec_no] = bk_no;
    }
    printf("\n");
  }else{
    for(int sec_no=0; sec_no<NDIRECT; ++sec_no){
      free_map_allocate (1, &bk_no);
      printf("%d ", bk_no);
      disk_inode->addr[sec_no]=bk_no;
    }
    sectors-=NDIRECT;
    free_map_allocate (1, &disk_inode->addr[NDIRECT]);
    printf("%d ", disk_inode->addr[NDIRECT]);
    printf("\n");
    uint32_t* addr = malloc(BLOCK_SECTOR_SIZE);
    printf("l2 index:\n");
    if(sectors<=NINDIRECT*NENTRY){
      for(int sec_no=0; sec_no<sectors; ++sec_no){
        free_map_allocate (1, &addr[sec_no]);
        printf("%d ", addr[sec_no]);
      }
      printf("\n");
      buffer_write(fs_device, disk_inode->addr[NDIRECT], 0, addr, BLOCK_SECTOR_SIZE);
      // block_write(fs_device, disk_inode->addr[NDIRECT], addr);
    }else{
      for(int sec_no=0; sec_no<NENTRY; ++sec_no){
        free_map_allocate (1, &addr[sec_no]);
          printf("%d ", bk_no);
      }
      printf("\n");
      buffer_write(fs_device, disk_inode->addr[NDIRECT], 0, addr, BLOCK_SECTOR_SIZE);
      // block_write(fs_device, disk_inode->addr[NDIRECT], addr);
      sectors-=NENTRY;
      uint16_t count = sectors/NENTRY;
      uint32_t * s_addr = malloc(BLOCK_SECTOR_SIZE);
      for(int sec_no=0; sec_no<count; ++sec_no){
        free_map_allocate (1, &addr[sec_no]);
        for(uint32_t s_no=0;s_no<sectors; ++s_no){
          free_map_allocate (1, &s_addr[s_no]);
          printf("l3 index %d ", sec_no);
        }
        buffer_write(fs_device, addr[sec_no], 0, s_addr, BLOCK_SECTOR_SIZE);
        // block_write(fs_device, addr[sec_no], s_addr);
        sectors-=NENTRY;
      }
      free(s_addr);
    }
    free(addr);
  }
  return true;
}

static void inode_free(struct inode_disk *d_inode){
  for(int i=0; i<NDIRECT; ++i){
    if(!d_inode->addr[i])return;
    free_map_release(d_inode->addr[i], 1);
  }
  for(int i=NDIRECT; i<NDIRECT+NINDIRECT; ++i){
    if(!d_inode->addr[i])return;
    struct buffer * buf = bread(fs_device, d_inode->addr[i]);
    uint32_t* addr = buf->cache;
    for(int sec_no; sec_no<NENTRY; ++sec_no){
      if(!addr[sec_no])return;
      free_map_release(addr[i], 1);
    }
    brelse(buf);
    free_map_release(d_inode->addr[i], 1);
  }
  for(int i=NDIRECT+NINDIRECT; i<NDIRECT+NINDIRECT+NDINDIRECT; ++i){
    if(!d_inode->addr[i])return;
    struct buffer * buf = bread(fs_device, d_inode->addr[i]);
    uint32_t* addr = buf->cache;
    for(int sec_no=0; sec_no<NENTRY; ++sec_no){
      if(!addr[sec_no])return;
      struct buffer * sbuf = bread(fs_device, addr[sec_no]);
      uint32_t* s_addr = sbuf->cache;
      for(int bk_no=0; bk_no<NENTRY; ++bk_no){
        if(!s_addr[bk_no])return;
        free_map_release(s_addr[bk_no], 1);
      }
      brelse(sbuf);
      free_map_release(addr[sec_no], 1);
    }
    brelse(buf);
    free_map_release(d_inode->addr[i], 1);
  }
}
/** Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
/** 使用 LENGTH 字节的数据初始化inode，并将新 inode 写入文档系统设备上的扇区SECTOR。 */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  // TODO: 待修改使一个inode_disk大小为64B
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);


  disk_inode = calloc (1, sizeof *disk_inode);
  memset(disk_inode->addr, -1, sizeof(block_sector_t)*NADDR);
  if (disk_inode != NULL)
    {
      //所需扇区数量
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      if (inode_allocate (disk_inode, sectors)) 
        {
          //将disk_inode写入文件系统 ???在buffer cache中是否有备份
          // block_write (fs_device, sector, disk_inode);
          buffer_write(fs_device, sector, 0, disk_inode, sizeof(*disk_inode));
          if (sectors > 0) 
            {

              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              for (i = 0; i < sectors; i++) {
                buffer_write(fs_device, walk_to_sector(disk_inode, sector, i, 0), 0, zeros, BLOCK_SECTOR_SIZE);
                // block_write (fs_device, walk_to_sector(disk_inode, sector, i, 0), zeros);
              }
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/** Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
/** 从扇区中读取一个inode，并返回一个struct inode结构体*/
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      //如果inode已经打开，增加inode引用计数
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }
  //inode第一次打开，创建inode
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. 初始化inode*/
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  //从块设备中读取数据，fs_device为块设备， inode->sector为扇区号， inode->data为数据
  // block_read (fs_device, inode->sector, &inode->data);
  
  buffer_read(fs_device, inode->sector, 0, &inode->data, BLOCK_SECTOR_SIZE);
  return inode;
}

/** Reopens and returns INODE. */
/** 重新打开并返回inode */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/** Returns INODE's inode number. */
/** 返回INODE的inode扇区号 */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/** Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          // free_map_release (inode->data.addr[0],
          //                   bytes_to_sectors (inode->data.length)); 
          //释放所有的块
          inode_free(&inode->data);
        }
      else
        {
        //将inode内部修改的内容写回磁盘
        buffer_write(fs_device, inode->sector, 0, &inode->data, sizeof(inode->data));
        }
      free (inode); 
    }
}

/** Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/** Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
/** 从inode中读取SIZE比特到BUFFER中，从位置 OFFSET 开始 */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  off_t inode_remain = inode_length(inode) - offset;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      /* 磁盘扇区读取， 从offset开始读取*/
      block_sector_t sector_idx = byte_to_sector (inode, offset); //获取扇区号
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;                //扇区偏移

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_remain < BLOCK_SECTOR_SIZE ? inode_remain:BLOCK_SECTOR_SIZE;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      // 每次都从扇区起始位置开始读
      buffer_read(fs_device, sector_idx, sector_ofs, buffer + bytes_read, chunk_size);
      /* Advance. */
      size -= chunk_size;
      inode_remain -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

static void 
inode_length_set(struct inode * inode, off_t length){
  buffer_write(fs_device, inode->sector, offsetof(struct inode_disk, length), &length, sizeof(length));
  inode->data.length = length;
}
/** Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
/**
 * 将 SIZE 字节从 BUFFER 写入 INODE，从 OFFSET 开始。
 * 返回实际写入的字节数，如果到达文档末尾或发生错误，则可能小于 SIZE。
 * （通常，在文档末尾写入会扩展 inode，但尚未实现增长)
 * size 为写入字节大小， offset为文件偏移量
 */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      /* 选中 需要写的扇区号，以及偏移量*/
      // offset为文件偏移量
      block_sector_t index = offset/BLOCK_SECTOR_SIZE;
      block_sector_t sector_idx = walk_to_sector (&inode->data, inode->sector, index, 1);;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
 
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      //计数inode对应的文件剩余的字节数量
      off_t inode_left = size < BLOCK_SECTOR_SIZE ? size: BLOCK_SECTOR_SIZE;
      //计算对应扇区剩余的字节数量
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      //计算二者最小值
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      /* 实际写入的字节数量， 如果剩余需要写的字节数量小于inode的剩余字节数量，
        则写入的字节数量变成inode的剩余字节数量*/
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      
      buffer_write(fs_device, sector_idx, sector_ofs, buffer + bytes_written, chunk_size);
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  if(offset > inode_length(inode)){
    inode_length_set(inode, offset);
  }
  return bytes_written;
}

/** Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/** Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/** Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
