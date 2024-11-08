#ifndef SWAP_H
#define SWAP_H
#include "filesys/free-map.h"
#include "kernel/bitmap.h"
#include "kernel/list.h"
#include "threads/thread.h"
#include "kernel/hash.h"
struct swap_frame{
    uint32_t upage;
    uint32_t bs;
    // struct list_elem elem;
    struct hash_elem elem;
};

void swap_table_init(struct hash *h);
static struct bitmap *free_swap_map;
static struct block * swap_device;
void free_swap_map_init (void);
void free_swap_frame(struct thread* );
uint32_t write_swap(void*, int32_t);
bool read_sawp(uint32_t, void *, int32_t);
struct swap_frame* find_swap(struct hash*, uint32_t);
bool swap_out(struct thread*, uint32_t, uint32_t);
void swap_in(struct thread*, uint32_t,  uint32_t);
#endif