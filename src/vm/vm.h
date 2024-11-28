#ifndef VM_H
#define VM_H
#include <stddef.h>
#include <stdint.h>
#include "lib/kernel/list.h"
#include "threads/thread.h"
#include "filesys/off_t.h"
struct page_directory_entry {
    uint32_t present : 1;      // 有效位
    uint32_t rw : 1;           // 读/写位
    uint32_t user : 1;         // 用户/内核位
    uint32_t reserved : 2;     // 保留位
    uint32_t accessed : 1;     // 引用位
    uint32_t dirty : 1;        // 修改位
    uint32_t pat : 1;          // 页属性表位
    uint32_t global : 1;       // 全局页位
    uint32_t available : 3;    // 可用位
    uint32_t page_table_addr : 20; // 指向页表的物理地址
}__attribute__((__packed__));

struct page_table_entry {
    uint32_t present : 1;      // 有效位
    uint32_t rw : 1;           // 读/写位
    uint32_t user : 1;         // 用户/内核位
    uint32_t reserved : 2;     // 保留位
    uint32_t accessed : 1;     // 引用位
    uint32_t dirty : 1;        // 修改位
    uint32_t pat : 1;          // 页属性表位
    uint32_t global : 1;       // 全局页位
    uint32_t available : 3;    // 可用位
    uint32_t frame : 20;       // 页框号（指向物理页框的地址）
}__attribute__((__packed__));

struct vm_eara{
    uint32_t start;
    uint32_t end;
    struct inode* inode;
    struct file * file;
    uint32_t flags;
    uint32_t offset;
    struct list_elem elem;
};
struct page_frame{
    uint8_t* upage;
    uint8_t* kpage;
    struct vm_eara* vma;
    struct list_elem elem;
};
//page-parallel 自动重启的BUG
#define NUM_FRAMES_W 160
#define NUM_FRAMES_R 16

void flush_tlb(uint32_t upage);
struct page_frame* page_eviction(struct list* list, uint32_t* pd, bool writable);
struct page_frame* get_page_eviction(struct thread* t, uint32_t upage, bool writable);
struct vm_eara* find_vma(struct list* list, uint32_t upage);
void free_page_frame(struct thread *t);
void free_vma(struct thread *t);
typedef int32_t mapid_t;
void *mmap(void *addr, size_t length, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
mapid_t sys_mmap (int fd, void *addr);
void sys_munmap (mapid_t mapping);
#endif