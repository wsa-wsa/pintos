#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/** Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /**< Free map file inode sector. 文件映射扇区号*/
#define ROOT_DIR_SECTOR 1       /**< Root directory file inode sector. 根目录文件映射扇区号 */

/** Block device that contains the file system. */
struct block *fs_device;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);

#endif /**< filesys/filesys.h */
