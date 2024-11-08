#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
/** Partition that contains the file system. */
struct block *fs_device;

struct lock filesys_lock;
static void do_format (void);

/** Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/** Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/** Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/** Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  lock_acquire(&filesys_lock);
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;
  if (dir != NULL)dir_lookup (dir, name, &inode);
  dir_close (dir);
  lock_release(&filesys_lock);
  return file_open (inode);
}

/** Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/** Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
void remove_fd(int fd){
  struct thread * cur = thread_current();
  if(fd<0||fd>=NOFILE||cur->ofile[fd]==0)PANIC("The fd is incorrect!!!");
  cur->ofile[fd]=0;
}
int get_fd(struct file *file){
  struct thread * cur = thread_current();
  for(int fd=0; fd<NOFILE; ++fd){
    if(cur->ofile[fd]==0){
      cur->ofile[fd]=file;
      return fd;
    }
  }
  return -1;
}
struct file *get_file(int fd){
  struct thread * cur = thread_current();
  if(fd<0||fd>=NOFILE||cur->ofile[fd]==0)sys_exit(-1);
  return cur->ofile[fd];
}

bool sys_create (const char *file, unsigned initial_size){
  if(!file||get_user(file)==-1){
    sys_exit(-1);
  }
  return filesys_create(file, initial_size);
}

bool sys_remove (const char *file){
  return filesys_remove(file);
}

int sys_open (const char *file){
  if(!file||get_user(file)==-1){
    sys_exit(-1);
  }
  struct file * f = filesys_open(file);
  if(!f)return -1;
  int fd = get_fd(f);
  return fd;
}

int sys_filesize (int fd){
  struct file * f = get_file(fd);
  return file_length(f);
}

int sys_read (int fd, char *buffer, unsigned length){
  if(length==0)return 0;
  if(!buffer||!put_user(buffer, 0)||(uint32_t)buffer >= PHYS_BASE){
    sys_exit(-1);
  }

  char* buf=(char*)malloc(length*sizeof(char));
  if(fd==STDOUT_FILENO){
    // struct file *f =get_file(fd);
    // f->sw.read(STDOUT_FILENO, buffer, length);
      // printf("begin:");
      int i;
      for(i=0; i<length; ++i){
        // buffer[i] = input_getc();
        // if(c=='\n')return i;
        // =c;
      }
      return i+1;
  }else{
    struct file* file = get_file(fd);
    if(!file)return -1;
    lock_acquire(&filesys_lock);
    // read file
    length=file_read(file, buf, length);

    lock_release(&filesys_lock);
    memcpy(buffer, buf, length);
    free(buf);
    return length;
  }

}

int sys_write (int fd, const void *buffer, unsigned length){
  if(!buffer||get_user(buffer)==-1){
    sys_exit(-1);
  }
  char* buf=(char*)malloc(length*sizeof(char));
  memcpy(buf, buffer, length);
  // for(unsigned i=0; i<length; ++i){
  //   if(get_user(buffer)==-1){
  //     sys_exit(-1);
  //   }
  // }
  if(fd==STDOUT_FILENO){
    putbuf(buf, length);
  }else if(fd==STDIN_FILENO){
    
  }else{
    struct file *file = get_file(fd);
    if(!file)sys_exit(-1);
    lock_acquire(&filesys_lock);
    length = file_write(file, buf, length);
    lock_release(&filesys_lock);
  }
  free(buf);
  return length;
}

void sys_seek (int fd, unsigned position){
  struct file *file=get_file(fd);
  file_seek(file, position);
}
unsigned sys_tell (int fd){
  struct file *file=get_file(fd);
  return file_tell(file);
}
void sys_close (int fd){
  if(fd==STDIN_FILENO){
    return;
  }else if(fd==STDOUT_FILENO){
    return;
  }
  struct file *file=get_file(fd);
  remove_fd(fd);
  return file_close(file);
}