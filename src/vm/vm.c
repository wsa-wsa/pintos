#include "vm/vm.h"
#include "threads/pte.h"
#include "threads/thread.h"
#include "devices/block.h"
#include "threads/palloc.h"
#include "lib/kernel/bitmap.h"
#include "vm/swap.h"
void flush_tlb(uint32_t upage)
{
    asm volatile("invlpg (%0)"::"r"(upage):"memory");
}

struct page_frame* clock_plus(struct list* list, uint32_t* pd){
    struct list_elem *e = NULL;
    for(e=list_begin(list); e!=list_end(list); e=list_next(e)){
        struct page_frame* pf = list_entry(e, struct page_frame, elem);
        struct page_table_entry* pte = pagedir_get_pte(pd, pf->upage);
        if(!pte->accessed&&!pte->dirty){
          pte->present=0;
          list_splice(list_end(list), list_begin(list), list_next(e));
          return pf;
        }
    }
    for(e=list_begin(list); e!=list_end(list); e=list_next(e)){
        struct page_frame* pf = list_entry(e, struct page_frame, elem);
        struct page_table_entry* pte = pagedir_get_pte(pd, pf->upage);
        if(!pte->accessed){
          pte->present=0;
          list_splice(list_end(list), list_begin(list), list_next(e));
          return pf;
        }else{
            pte->accessed=0;
        }
    }
    return clock_plus(list, pd);
}
struct page_frame* clock(struct list* list, uint32_t* pd){
    struct list_elem *e = NULL;
    for(e=list_begin(list); e!=list_end(list); e=list_next(e)){
        struct page_frame* pf = list_entry(e, struct page_frame, elem);
        struct page_table_entry* pte = pagedir_get_pte(pd, pf->upage);
        if(!pte->accessed){
          pte->present=0;
          list_splice(list_end(list), list_begin(list), list_next(e));
          return pf;
        }else{
          pte->accessed=0;
        }
    }
    for(e=list_begin(list); e!=list_end(list); e=list_next(e)){
        struct page_frame* pf = list_entry(e, struct page_frame, elem);
        struct page_table_entry* pte = pagedir_get_pte(pd, pf->upage);
        if(!pte->accessed){
          pte->present=0;
          list_splice(list_end(list), list_begin(list), list_next(e));
          return pf;
        }
    }
    NOT_REACHED();
    return NULL;
}
struct page_frame* get_page(struct list* list, uint32_t* pd, bool rw){
  if(rw)return clock_plus(list, pd);
  else return clock(list, pd);
  return NULL;
}

struct page_frame* page_eviction(struct list* list, uint32_t* pd, bool writable){
    int sz = writable ? NUM_FRAMES_W: NUM_FRAMES_R; 
    if(list_size(list)<sz){
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL){
        return get_page(list, pd, writable);
      }
      struct page_frame* pf=(struct page_frame*)malloc(sizeof(struct page_frame));
      pf->kpage            = kpage;
      pf->upage            = 0;
      list_push_back(list, &pf->elem);
      return pf;
    }else{
      return get_page(list, pd, writable);
    }
}

struct page_frame* get_page_eviction(struct thread* t, uint32_t upage, bool writable){
    struct page_frame* pf = NULL;
    if(writable){
      pf=page_eviction(&t->wpage_list, t->pagedir, writable);
    }else{
      pf=page_eviction(&t->rpage_list, t->pagedir, writable);
    }
    if(!pf->upage){
      pf->upage=upage;
      return pf;
    }
    flush_tlb(pf->upage);
    swap_in(t, pf->upage, pf->kpage);
    pf->upage=upage;
    return pf;
}

struct vm_eara* find_vma(struct list* list, uint32_t upage){
  struct list_elem* e=NULL;
  for(e=list_begin(list); e!=list_end(list); e=list_next(e)){
      struct vm_eara* vma= list_entry(e, struct vm_eara, elem);
      if(vma->start<=upage&&pg_round_up(vma->end)>upage){
        return vma;
      }
  }
  return NULL;
}

void free_page_frame(struct thread *t){

  while (!list_empty(&t->wpage_list)) {
      struct list_elem *e = list_pop_front(&t->wpage_list);  // 获取并移除第一个元素
      struct page_frame *pf = list_entry(e, struct page_frame, elem);  // 转换为实际数据类型
      // palloc_free_page(pf->kpage);
      free(pf);  // 释放数据结构内存
  }
  while (!list_empty(&t->rpage_list)) {
      struct list_elem *e = list_pop_front(&t->rpage_list);  // 获取并移除第一个元素
      struct page_frame *pf = list_entry(e, struct page_frame, elem);  // 转换为实际数据类型
      // palloc_free_page(pf->kpage);
      free(pf);  // 释放数据结构内存
  }
}