#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "filesys/buffer.h"
/** A directory. */
struct dir 
  {
    struct inode *inode;                /**< Backing store. */
    off_t pos;                          /**< Current position. */
  };

/** A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /**< Sector number of header. */
    char name[NAME_MAX + 1];            /**< Null terminated file name. */
    bool in_use;                        /**< In use or free? */
  };

#define MAX_DIR_ENTRY (BLOCK_SECTOR_SIZE/sizeof (struct dir_entry))

/** Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
}

struct dir *
directory_open (block_sector_t sector){
  return dir_open (inode_open (sector));
}

bool directory_create(struct dir *dir, const char *name)
{
  block_sector_t inode_sector = 0;
  struct dir *root = dir_reopen(dir);
  bool success = (root != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, MAX_DIR_ENTRY)
                  && dir_add (root, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (root);
  struct dir * cur = directory_open(inode_sector);
  success = (dir_add(cur, ".", inode_sector)
            &&dir_add(cur, "..", inode_get_inumber(dir->inode)));
  set_inode_type(cur->inode, T_DIR);
  dir_close(cur);
  return success;
}

bool 
path_create(struct dir *dir, const char *path){

}
/** Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/** Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/** Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/** Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/** Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/** Searches DIR for a file with the given NAME.
 * 在 DIR 中搜索具有给定 NAME 的文件。
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
// 查找文件和查找DIR需要分清
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}
struct inode* namei(struct dir * dir, const char *path){
  char *token, *save_ptr;
  struct dir_entry e;
  off_t ofs;
  struct dir * cur_dir = dir_reopen(dir);
  for (token = strtok_r (path, "/", &save_ptr); token != NULL; token = strtok_r (NULL, "/", &save_ptr)){
    if(!lookup(cur_dir, token, &e, &ofs)){
      dir_close(cur_dir);
      return NULL;
    }
    dir_close(cur_dir);
    cur_dir = directory_open(e.inode_sector);
  }
  return inode_open (e.inode_sector);
}
/** Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   在 当前DIR 中搜索具有给定 NAME 的文件，
   如果存在，则返回 true，否则返回 false。
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/** Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
/** 将名为 NAME 的文件添加到 当前的DIR 中，该文档不得包含具有该名称的文档。 
 * 该文档的 inode 位于扇区 INODE_SECTOR 中。 DIR中无名字，但有扇区号
 * 并且区分是文件夹还是文件*/
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/** Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/** Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}


bool sys_chdir (const char *dir){
  struct dir_entry;
  // namei(NULL, dir);
  struct dir *pwd = dir_reopen(thread_current()->ipwd);
  thread_current()->ipwd = NULL;
  directory_create(pwd, "hhhh");
  printf("%s\n", dir);
  namei(pwd, dir);
  return true;
}

int sys_mkdir (const char *dir){
  // struct inode *ipwd = thread_current()->ipwd;
  // ipwd变成dpwd
  char path[128];
  strlcpy(path, dir, 128);
  if(dir==NULL||strlen(path)==0){
    return false;
  }
  // // char path[MAX_DIR_LEN];
  // struct dir *pwd = dir_reopen(thread_current()->ipwd);
  asm ("movl $0, %eax");
  // return 0;
}

bool sys_readdir (int fd, char *name){
  return true;
}
bool sys_isdir (int fd){
  struct file * f = get_file(fd);
  return true;
}
int sys_inumber (int fd){
  struct file * f = get_file(fd);
  return inode_get_inumber(file_get_inode(f));
}