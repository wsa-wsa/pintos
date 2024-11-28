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
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), T_DIR);
}

struct dir *
directory_open (block_sector_t sector){
  return dir_open (inode_open (sector));
}

bool directory_create(struct inode *ip, const char *name)
{
  ASSERT(inode_type(ip)==T_DIR);
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open(inode_reopen(ip));
  bool success = (dir != NULL
                  && !inode_will_remove(dir_get_inode(dir))
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, MAX_DIR_ENTRY)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  if (!success)
    goto done;
  struct dir * cur = directory_open(inode_sector);
  success = (dir_add(cur, ".", inode_sector)
            &&dir_add(cur, "..", inode_get_inumber(ip)));
  dir_close(cur);
done:
  dir_close (dir);
  return success;
}

// void  
// print_all_content(){
//   struct dir * dir = dir_open_root();

// }
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
// 查找文件和查找DIR需要分清
static bool
dirlookup (const struct inode *inode, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (inode != NULL);
  ASSERT (name != NULL);
  ASSERT (inode_type(inode)==T_DIR);
  for (ofs = 0; inode_read_at (inode, &e, sizeof e, ofs) == sizeof e;
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
  return dirlookup(dir->inode, name, ep, ofsp);
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
  if(inode_type(inode)==T_DIR){
    struct dir* cur_dir = dir_open(inode_reopen(inode));
    //跳过.和..这两个目录项
    cur_dir->pos = 2*sizeof(struct dir_entry);
    if(dir_readdir(cur_dir, name)){
      dir_close(cur_dir);
      return false;
    }
    /* 如果是dir则需要删除自引用*/
    struct dir_entry de;
    de.in_use = false;
    strlcpy(de.name, ".", 2);
    if (inode_write_at (inode, &de, sizeof de, 0) != sizeof de) 
      {
        dir_close(cur_dir);
        goto done;
      }
    strlcpy(de.name, "..", 3);
    if (inode_write_at (inode, &de, sizeof de, sizeof(de)) != sizeof de)
      {
        dir_close(cur_dir);
        goto done;
      } 
    dir_close(cur_dir);
  }
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

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len > NAME_MAX)
    memmove(name, s, NAME_MAX + 1);
  else {
    memmove(name, s, len);
    name[len] = '\0';
  }
  while(*path == '/')
    path++;
  return path;
}
// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;
  struct dir_entry e;
  if(*path == '/')
    ip = inode_open (ROOT_DIR_SECTOR);
  else
    ip = inode_reopen(thread_current()->ipwd);

  while((path = skipelem(path, name)) != 0){
    if(inode_type(ip) != T_DIR){
      inode_close(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      return ip;
    }
    if(!dirlookup(ip, name, &e, 0)){
      inode_close(ip);
      return 0;
    }
    next = inode_open(e.inode_sector);
    inode_close(ip);
    if(!next){
      return 0;
    }
    ip = next;
  }
  if(nameiparent){
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[NAME_MAX + 1];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}

bool sys_chdir (const char *dir){
  struct inode * ip = namei(dir);
  if(!ip){
    return false;
  }
  inode_close(thread_current()->ipwd);
  
  thread_current()->ipwd = ip;
  return true;
}

int sys_mkdir (const char *dir){
  char path[128];
  strlcpy(path, dir, 128);
  if(dir==NULL||strlen(path)==0){
    return false;
  }
  char name[NAME_MAX + 1];
  struct inode * ip = nameiparent(path, name);
  if(!ip){
    return -1;
  }
  bool suceess = directory_create(ip, name);
  inode_close(ip);
  return suceess;
}
struct dir *open_dir_file(struct file * file){
  ASSERT(inode_type(file_get_inode(file))==T_DIR);
  struct inode *ip = inode_reopen(file_get_inode(file));
  return dir_open(ip);
}
bool sys_readdir (int fd, char *name){
  // dir_readdir(, );
  struct file * file = get_file(fd);
  struct dir * dir = open_dir_file(file);
  // char name[NAME_MAX+1];
  dir->pos = file_tell(file);
  dir->pos = dir->pos > 2* sizeof(struct dir_entry)? \
              dir->pos: 2*sizeof(struct dir_entry);
  bool success = dir_readdir(dir, name);
  file_seek(file, dir->pos);
  dir_close(dir);
  return success;
}
bool sys_isdir (int fd){
  struct file * f = get_file(fd);
  return inode_type(file_get_inode(f))==T_DIR;
}
int sys_inumber (int fd){
  struct file * f = get_file(fd);
  return inode_get_inumber(file_get_inode(f));
}