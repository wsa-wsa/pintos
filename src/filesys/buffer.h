#ifndef BUFFER_H
#define BUFFER_H
#include "lib/kernel/list.h"
#include "devices/block.h"
#include "threads/synch.h"
#define BUFFER_SECTOR_COUNT 64
#define BUFFER_MAX (BLOCK_SECTOR_SIZE*BUFFER_SECTOR_COUNT)

//cache默认大小为1KB
#define BUFFER_SIZE 2048
#define BUFFER_LENGTH (BUFFER_MAX/BUFFER_SIZE)

struct buffer{
  block_sector_t blockno;
  struct block *dev;
  uint16_t refcnt;
  uint16_t dirty;
  uint16_t valid;
  uint8_t cache[BUFFER_SIZE];
  struct lock lock;
  struct list_elem elem;
};
void buffer_init();

struct buffer* bread(struct block *dev, block_sector_t blockno);
void bwrite(struct buffer *b);
void brelse(struct buffer *b);
void *boffset(struct buffer *buf, block_sector_t bno);
void buffer_write(struct block * dev, block_sector_t sector, size_t ofs, const void * buf, size_t size);
void buffer_set(struct block * dev, block_sector_t sector, size_t ofs, uint8_t byte, size_t size);
void buffer_read(struct block * dev, block_sector_t sector, size_t ofs, const void * buf, size_t size);
void buffer_write_to_disk();

#endif