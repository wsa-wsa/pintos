#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
// #ifdef VM
#include "vm/vm.h"
#include "vm/swap.h"
#include "filesys/off_t.h"
// #endif

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
struct parameter
{
  char * cmd_line;
  bool * success;
};

struct semaphore process_sema;
struct lock process_lock;
/** Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *args) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, args, PGSIZE);
  char file_name[16];
  for(int i=0; i<16; ++i){
    if(args[i]==' '){
      file_name[i]='\0';
      break;
    }
    file_name[i]=args[i];
  }
  file_name[15]='\0';

  struct parameter arg;
  bool success=false;
  arg.cmd_line=fn_copy;
  arg.success=&success;
  // sema_init(&process_sema, 0);
  /* Create a new thread to execute FILE_NAME. */
  // lock_acquire(&process_lock);

  tid = thread_create (file_name, PRI_DEFAULT, start_process, &arg);
  
  // lock_release(&process_lock);
  sema_down(&process_sema);
  if (tid == TID_ERROR){
    palloc_free_page (fn_copy);
  }
  if(!success){
    tid = TID_ERROR;
  }
  // enum intr_level old_level;
  // old_level = intr_disable ();
  // intr_set_level (old_level);
  return tid;
}

/** A thread function that loads a user process and starts it
   running. */
static void
start_process (void *parameters)
{
  struct parameter* parameter=parameters;
  char *args = (struct parameter*)parameter->cmd_line;
  char *token, *save_ptr;
  static char *argv[LOADER_ARGS_LEN / 2 + 1];
  int argc=0;
  for (token = strtok_r (args, " ", &save_ptr); token != NULL; token = strtok_r (NULL, " ", &save_ptr)){
    argv[argc++]=token;
  }
  argv[argc]=NULL;

  char *file_name=argv[0];
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  *(parameter->success)=success;
  sema_up(&process_sema);
  if (!success) {
    /* If load failed, quit. */
    palloc_free_page (file_name);
    sys_exit(-1);
  }
  thread_yield (); 
  for(int i=argc-1; ~i; --i){
    int n = strlen(argv[i]);
    if_.esp -= (n + 1);
    strlcpy (if_.esp, argv[i], n+1);
    argv[i]=if_.esp;
  }
  if_.esp=ROUND_DOWN((uint32_t)if_.esp, sizeof(uint32_t));
  for(int i=argc; ~i; --i){
    if_.esp-=sizeof (char*);
    memcpy(if_.esp, argv+i, sizeof (char*));
  }
  char *start_argv=if_.esp;
  if_.esp-=sizeof (char**);
  memcpy(if_.esp, &start_argv, sizeof (char**));
  if_.esp-=sizeof (char*);
  memcpy(if_.esp, &argc, sizeof (int));
  if_.esp-=sizeof (void*);
  *(int *)if_.esp = 0;
  palloc_free_page (file_name);
  // thread_wakeup()
  // struct thread * cur = thread_current();
  // thread_wakeup(cur->parent);
  // hex_dump(if_.esp, if_.esp, p-(char*)if_.esp, true);
  // push_argument(p, argc, argv);
  // printf("file name is '%s'\n", file_name);
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/** Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{

  enum intr_level old_level;
  old_level = intr_disable ();
  thread_wait(child_tid);
  intr_set_level (old_level);

  return thread_current()->xstatus;
}

/** Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  printf("%s: exit(%d)\n", cur->name, cur->xstatus);
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/** Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/** We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/** ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/** For use with ELF types in printf(). */
#define PE32Wx PRIx32   /**< Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /**< Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /**< Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /**< Print Elf32_Half in hexadecimal. */

/** Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/** Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
/*
p_type: 段的类型，决定了段的用途。例如，它可以表示这是一个可加载的代码段、数据段，或者动态链接器需要的信息。常见值包括：

PT_LOAD: 可加载的段。
PT_DYNAMIC: 动态链接信息。
PT_INTERP: 指定程序解释器路径。
PT_NOTE: 记录附加信息。

p_offset: 段在文件中的偏移量，表示从 ELF 文件开头到该段内容在文件中的字节偏移。

p_vaddr: 段的虚拟地址，表示该段在内存中加载时的起始地址（通常是进程的虚拟地址空间中的地址）。

p_paddr: 段的物理地址，表示该段加载到物理内存中的起始地址。
在现代操作系统中通常被忽略，因为它们一般使用虚拟内存。

p_filesz: 段在文件中的大小（以字节为单位），用于从文件中读取的内容长度。
如果段未使用整个 p_memsz 指定的内存，剩余部分会用零填充。

p_memsz: 段在内存中的大小（以字节为单位）。
如果 p_memsz 大于 p_filesz，表示该段在加载到内存后需要分配更多空间，额外的空间一般初始化为零。

p_flags: 段的标志，控制该段的权限。常见值包括：

p_align: 段的对齐要求，表示该段在内存和文件中的起始地址应当符合的对齐约束。
*/
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/** Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /**< Ignore. */
#define PT_LOAD    1            /**< Loadable segment. */
#define PT_DYNAMIC 2            /**< Dynamic linking info. */
#define PT_INTERP  3            /**< Name of dynamic loader. */
#define PT_NOTE    4            /**< Auxiliary info. */
#define PT_SHLIB   5            /**< Reserved. */
#define PT_PHDR    6            /**< Program header table. */
#define PT_STACK   0x6474e551   /**< Stack segment. */

/** Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /**< Executable. */
#define PF_W 2          /**< Writable. */
#define PF_R 4          /**< Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (void * vm, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/** Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  /* 读取并验证可执行文件头 */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  /* 读取文件头*/
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {

              bool writable = (phdr.p_flags & PF_W) != 0;
              /* 段在文件中的偏移量的页表项*/
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              /* 用户虚拟内存地址的页表项*/
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              /* 用户虚拟内存地址的偏移量*/
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  /* 普通段，从磁盘中读取初始部分并将其余部分归零。*/
                  /* 读取的字节数量*/
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  /* BSS段， 不需要从磁盘中读取，直接将分配的页全部置为0 */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
#define VM
#ifndef VM
              //将可执行文件的各个段加载到内存中mem_page->upage
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
#else

    // pagedir_set_page(t->pagedir, mem_page, NULL, writable);
    struct vm_eara *vma =(struct vm_eara *)malloc(sizeof(*vma));
    vma->flags = phdr.p_flags;
    vma->start = pg_round_down(phdr.p_vaddr);
    vma->end   = phdr.p_vaddr+phdr.p_filesz;
    vma->offset= pg_round_down(phdr.p_offset);
    // vma->inode = file->inode;
    vma->file  = file_reopen(file);
    // file_close(file);
    list_push_back(&t->vm_list, &vma->elem);
    // printf("vm %p---%p off_t %p\n", vma->start, vma->end, vma->offset);
    if(phdr.p_filesz!=phdr.p_memsz){
      struct vm_eara *vma_bss =(struct vm_eara *)malloc(sizeof(*vma_bss));
      vma_bss->flags = phdr.p_flags;
      vma_bss->start = pg_round_up(vma->end);
      vma_bss->end   = phdr.p_vaddr + phdr.p_memsz;
      vma_bss->file     = NULL; 
      vma_bss->offset    = vma->offset;
      list_push_back(&t->vm_list, &vma_bss->elem);
      // printf("vm_bss %p---%p off_t %p\n", vma_bss->start, vma_bss->end, vma_bss->offset);
    }

    // printf("writable %d vaddr %p---%p\n", writable, phdr.p_vaddr, phdr.p_vaddr+phdr.p_memsz);
    
#endif
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;
  t->exec = file;
  file_deny_write(file);
  // inode_allow_write (file->inode);
  // inode_close (file->inode);
  return success;
 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/** load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/** Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/** Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool

load_segment (void * vm, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);
#define VM
#ifndef VM
    struct file *file = (struct file *)vm;
    file_seek (file, ofs);
    while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      /* 计算如何填充分配的页，每次分配PGSIZE个字节。
         我们将会从文件中读取PGEGE_READ_BYTES个字节
         并将后续的PAGE_ZERO_BYTES个字节填充为0。
      */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      /* 获得一页内存*/
      /* 内存不够 */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      /* 加载给的页 */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          /* 分配失败直接返回false*/
          palloc_free_page (kpage);
          return false; 
        }
      /* 并将后续的PAGE_ZERO_BYTES个字节填充为0。 */
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      /* 将分配的页加入到用户内存中 */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      // 需要读取的read_bytes和zero_bytes分别处理
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
#else
    // size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    // size_t page_zero_bytes = PGSIZE - page_read_bytes;
    struct vm_eara *vma = (struct vm_eara *)vm;
    struct thread * t= thread_current();
    struct page_frame *pf = get_page_eviction(t, upage, writable);
    pf->vma = vm;
    ASSERT(pf->vma->file==vma->file);
    if(swap_out(t, upage, pf->kpage));
    else{
      if(read_bytes){
        file_seek (vma->file, ofs);
        if (file_read (vma->file, pf->kpage, read_bytes) != (int) read_bytes)
        {
          palloc_free_page (pf->kpage);
          return false;
        }
      }
      memset (pf->kpage + read_bytes, 0, zero_bytes);
    }
    if (!install_page (upage, pf->kpage, writable)) 
    {
      palloc_free_page (pf->kpage);
      return false; 
    }
  

    // struct page_table_entry* pte = pagedir_get_pte(t->pagedir, upage);
    // struct list_elem *e = NULL;
    // for(e=list_begin(&t->vm_list); e!=list_end(&t->vm_list); e=list_next(e)){
    //   // struct swap_frame *sf = list_entry(e, struct swap_frame, elem);
    //   // printf("upage: %p and bs: %d\n", sf->upage, sf->bs);
    //   struct vm_eara* vma = list_entry(e, struct vm_eara, elem);
    //   printf("vm %p---%p off_t %p\n", vma->start, vma->end, vma->offset);
    // }
#endif
  
  return true;
}
// static struct ;
bool load_vm (struct thread *t , struct vm_eara *vma, uint32_t fault_addr){
   bool writable = (vma->flags & PF_W) != 0;
   uint32_t v_offset    = (fault_addr - vma->start)& ~PGMASK;
   uint32_t file_page   = vma->offset+v_offset;
   uint32_t mem_page    = fault_addr & ~PGMASK;
   uint32_t page_offset = fault_addr & PGMASK;

  //  printf("%s' map is write %d: %p---%p\n", t->name, writable, mem_page, mem_page+PGSIZE-1);
   uint32_t read_bytes, zero_bytes;
   if(vma->file){
      read_bytes = vma->end-mem_page < PGSIZE?vma->end-mem_page:PGSIZE;
      zero_bytes = PGSIZE-read_bytes;
   }else{
      read_bytes=0;
      zero_bytes=PGSIZE;
   }
  //  lock_acquire(&process_lock);
   if(!load_segment (vma, file_page, (void *) mem_page,
                           read_bytes, zero_bytes, writable)){
      return false;
   }
  //  lock_release(&process_lock);
  //  thread_yield();
   return true;
}

struct vm_eara * alloc_stack(struct thread *t, uint32_t vaddr){
  struct vm_eara *vma = (struct vm_eara *)malloc(sizeof(*vma));
  vma->start = pg_round_down(vaddr);
  vma->end   = pg_round_up(vaddr);
  vma->file  = NULL;
  vma->offset= 0;
  vma->flags = PF_W;
  list_push_back(&t->vm_list, &vma->elem);
  return vma;
}
bool is_stack_push_vaddr (struct thread *t, bool user, const void* vaddr){
  if(!user)return false;
  return vaddr+4==t->esp||vaddr+32==t->esp;
}
/** Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/** Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
/** 将用户虚拟地址UPAGE对应的KPAGE（内核虚拟地址）的映射添加到页表中。*/
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  //验证该虚拟地址中是否还没有页面，然后将我们的页面映射到那里。
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

void sys_halt(void){
  shutdown_power_off();
}

void sys_exit(int status){
  struct thread *t=thread_current();
  if(status<-1){
    status=-1;
  }else if(status>255)status%=256;
  t->xstatus=status;
  thread_wakeup(t->parent);
  // intr_disable();
  // release_mmap(t);
  free_page_frame(t);
  file_close(t->exec);

  // intr_enable();
  thread_exit();
}

int sys_wait (tid_t tid){
  return process_wait(tid);
}
unsigned sys_exec (const char *file){
  if(!file)sys_exit(-1);
  char args[128];
  if(get_user(file)==-1)sys_exit(-1);
  // TODO： 待修正
  for(int i=0; i<128&&file[i]!='\0'; ++i){
    if(get_user(file+i+1)==-1)sys_exit(-1);
    args[i]=file[i];
  }
  // thread_sleep(thread_current());
  lock_acquire(&process_lock);
  int ret = process_execute(file);
  lock_release(&process_lock);
  return ret;
}