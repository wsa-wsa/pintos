#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

static void sys_defalut(void) NO_RETURN;

extern void sys_halt (void) NO_RETURN;
extern void sys_exit (int status) NO_RETURN;
extern unsigned sys_exec (const char *file);
extern int sys_wait (pid_t);
extern bool sys_create (const char *file, unsigned initial_size);
extern bool sys_remove (const char *file);
extern int sys_open (const char *file);
extern int sys_filesize (int fd);
extern int sys_read (int fd, void *buffer, unsigned length);
extern int sys_write (int fd, const void *buffer, unsigned length);
extern void sys_seek (int fd, unsigned position);
extern unsigned sys_tell (int fd);
extern void sys_close (int fd);
typedef uint32_t mapid_t;
extern mapid_t sys_mmap (int fd, void *addr);
extern void sys_munmap (mapid_t mapping);

extern bool sys_chdir (const char *dir);
extern bool sys_mkdir (const char *dir);
extern bool sys_readdir (int fd, char *name);
extern bool sys_isdir (int fd);
extern int  sys_inumber (int fd);
static uint32_t (*syscall_table[])(void)={
  /* Projects 2 and later. */
    [SYS_HALT]      sys_halt,
    [SYS_EXIT]      sys_exit,                  /**< Terminate this process. */
    [SYS_EXEC]      sys_exec,                   /**< Start another process. */
    [SYS_WAIT]      sys_wait,                   /**< Wait for a child process to die. */
    [SYS_CREATE]    sys_create,                 /**< Create a file. */
    [SYS_REMOVE]    sys_remove,                 /**< Delete a file. */
    [SYS_OPEN]      sys_open,                   /**< Open a file. */
    [SYS_FILESIZE]  sys_filesize,               /**< Obtain a file's size. */
    [SYS_READ]      sys_read,                   /**< Read from a file. */
    [SYS_WRITE]     sys_write,                  /**< Write to a file. */
    [SYS_SEEK]      sys_seek,                   /**< Change position in a file. */
    [SYS_TELL]      sys_tell,                   /**< Report current position in a file. */
    [SYS_CLOSE]     sys_close,                  /**< Close a file. */

    /* Project 3 and optionally project 4. */
    [SYS_MMAP]      sys_mmap,                   /**< Map a file into memory. */
    [SYS_MUNMAP]    sys_munmap,                 /**< Remove a memory mapping. */

    /* Project 4 only. */
    [SYS_CHDIR]     sys_chdir,                  /**< Change the current directory. */
    [SYS_MKDIR]     sys_mkdir,                  /**< Create a directory. */
    [SYS_READDIR]   sys_readdir,                /**< Reads a directory entry. */
    [SYS_ISDIR]     sys_isdir,                  /**< Tests if a fd represents a directory. */
    [SYS_INUMBER]   sys_inumber                /**< Returns the inode number for a fd. */
};

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}
/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t *esp = f->esp; 
  if(get_user(esp)==-1||get_user(esp+3)==-1)sys_exit(-1);
  int syscall_num = *esp; 
  asm volatile("pushl %[arg2]; pushl %[arg1]; pushl %[arg0];"
              ::[arg0]"r"(*(esp+1)), [arg1]"r"(*(esp+2)), [arg2]"r"(*(esp+3))
              :
              );
  f->eax=syscall_table[syscall_num]();
  asm volatile("addl $12, %esp");
}
void sys_defalut(){
  PANIC("The syscall didn't realize!!!\n");
}
