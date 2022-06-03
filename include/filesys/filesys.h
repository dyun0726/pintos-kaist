#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Disk used for file system. */
extern struct disk *filesys_disk;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);

// P4-4-0 보조함수
struct dir *get_dir(char *name, char *file_name);
struct dir *path_parsing(char *path_name, char *file_name);

// P4-5-1 systemcall symlink 구현
int filesys_make_symlink(const char* target, const char* linkpath);

#endif /* filesys/filesys.h */
