#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"

struct bitmap;

void inode_init (void);
bool inode_create (disk_sector_t, off_t, bool); // P4-4-2 수정
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

// P4-4-2 추가 보조 함수
bool inode_is_dir (const struct inode *inode);
// P4-4-3 추가 보조 함수
int inode_open_cnt (const struct inode *inode);
// P4-5-2 추가 보조 함수
bool inode_set_soft_link (disk_sector_t inode_sector, const char *target);
bool inode_is_soft_link (const struct inode *inode);
char *inode_soft_link_path (const struct inode* inode);


#endif /* filesys/inode.h */
