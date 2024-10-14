#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "threads/loader.h"

/** Functions and macros for working with virtual addresses.

   See pte.h for functions and macros specifically for x86
   hardware page tables. */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/** Page offset (bits 0:12). */
#define PGSHIFT 0                          /**< Index of first offset bit. */
#define PGBITS  12                         /**< Number of offset bits. */
#define PGSIZE  (1 << PGBITS)              /**< Bytes in a page. */
#define PGMASK  BITMASK(PGSHIFT, PGBITS)   /**< Page offset bits (0:12). */

/** Offset within a page. */
/** 页内偏移量 */
static inline unsigned pg_ofs (const void *va) {
  return (uintptr_t) va & PGMASK;
}

/** Virtual page number. */
/** 虚拟页号 */
static inline uintptr_t pg_no (const void *va) {
  return (uintptr_t) va >> PGBITS;
}

/** Round up to nearest page boundary. */
/** 向上取最近的页边界 */
static inline void *pg_round_up (const void *va) {
  return (void *) (((uintptr_t) va + PGSIZE - 1) & ~PGMASK);
}

/** Round down to nearest page boundary. */
/** 向下取最近的页边界 */
static inline void *pg_round_down (const void *va) {
  return (void *) ((uintptr_t) va & ~PGMASK);
}

/** Base address of the 1:1 physical-to-virtual mapping.  Physical
   memory is mapped starting at this virtual address.  Thus,
   physical address 0 is accessible at PHYS_BASE, physical
   address address 0x1234 at (uint8_t *) PHYS_BASE + 0x1234, and
   so on.

   This address also marks the end of user programs' address
   space.  Up to this point in memory, user programs are allowed
   to map whatever they like.  At this point and above, the
   virtual address space belongs to the kernel. */
/** 基址 */
#define	PHYS_BASE ((void *) LOADER_PHYS_BASE)

/** Returns true if VADDR is a user virtual address. */
/** 判断是否是用户虚拟地址 */
static inline bool
is_user_vaddr (const void *vaddr) 
{
  return vaddr < PHYS_BASE;
}

/** Returns true if VADDR is a kernel virtual address. */
/** 判断是否是内核虚拟地址 */
static inline bool
is_kernel_vaddr (const void *vaddr) 
{
  return vaddr >= PHYS_BASE;
}

/** Returns kernel virtual address at which physical address PADDR
   is mapped. */
/** 返回物理地址PADDR映射的虚拟地址 */
static inline void *
ptov (uintptr_t paddr)
{
  ASSERT ((void *) paddr < PHYS_BASE);

  return (void *) (paddr + PHYS_BASE);
}

/** Returns physical address at which kernel virtual address VADDR
   is mapped. */
/** 返回内核虚拟地址VADDR映射的物理地址 **/
static inline uintptr_t
vtop (const void *vaddr)
{
  ASSERT (is_kernel_vaddr (vaddr));

  return (uintptr_t) vaddr - (uintptr_t) PHYS_BASE;
}

#endif /**< threads/vaddr.h */
