#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "filesys/fat.h"
#include "threads/thread.h" // P4-4-3 추가

/* A directory. */
struct dir {
	struct inode *inode;                /* Backing store. */
	off_t pos;                          /* Current position. */
};

/* A single directory entry. */
struct dir_entry {
	disk_sector_t inode_sector;         /* Sector number of header. */
	char name[NAME_MAX + 1];            /* Null terminated file name. */
	bool in_use;                        /* In use or free? */
};

/* Creates a directory with space for ENTRY_CNT entries in the
 * given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) {
	// P4-4-2 inode_create 수정 (is_file = false)
	return inode_create (sector, entry_cnt * sizeof (struct dir_entry), false);
}

/* Opens and returns the directory for the given INODE, of which
 * it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) {
	struct dir *dir = calloc (1, sizeof *dir);
	if (inode != NULL && dir != NULL) {
		
		// inode가 dir이어야함, P4-4-4 dir 함수 수정
		ASSERT(inode_is_dir(inode));

		dir->inode = inode;
		dir->pos = 0;
		return dir;
	} else {
		inode_close (inode);
		free (dir);
		return NULL;
	}
}

/* Opens the root directory and returns a directory for it.
 * Return true if successful, false on failure. */
struct dir *
dir_open_root (void) {
	#ifdef EFILESYS // P4-2-0 추가 filesys 일때 
	return dir_open(inode_open(cluster_to_sector(ROOT_DIR_CLUSTER)));

	#else
	return dir_open (inode_open (ROOT_DIR_SECTOR));
	#endif
}

/* Opens and returns a new directory for the same inode as DIR.
 * Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) {
	return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) {
	if (dir != NULL) {
		inode_close (dir->inode);
		free (dir);
	}
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) {
	return dir->inode;
}

/* Searches DIR for a file with the given NAME.
 * If successful, returns true, sets *EP to the directory entry
 * if EP is non-null, and sets *OFSP to the byte offset of the
 * directory entry if OFSP is non-null.
 * otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
		struct dir_entry *ep, off_t *ofsp) {
	struct dir_entry e;
	size_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (e.in_use && !strcmp (name, e.name)) {
			if (ep != NULL)
				*ep = e;
			if (ofsp != NULL)
				*ofsp = ofs;
			return true;
		}
	return false;
}

/* Searches DIR for a file with the given NAME
 * and returns true if one exists, false otherwise.
 * On success, sets *INODE to an inode for the file, otherwise to
 * a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
		struct inode **inode) {
	struct dir_entry e;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	if (lookup (dir, name, &e, NULL))
		*inode = inode_open (e.inode_sector);
	else
		*inode = NULL;

	return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
 * file by that name.  The file's inode is in sector
 * INODE_SECTOR.
 * Returns true if successful, false on failure.
 * Fails if NAME is invalid (i.e. too long) or a disk or memory
 * error occurs. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) {
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
	 * If there are no free slots, then it will be set to the
	 * current end-of-file.

	 * inode_read_at() will only return a short read at end of file.
	 * Otherwise, we'd need to verify that we didn't get a short
	 * read due to something intermittent such as low memory. */
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

/* Removes any entry for NAME in DIR.
 * Returns true if successful, false on failure,
 * which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) {
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

	// P4-4-3 dir를 제거할 때 추가
	if (inode_is_dir(inode)){
		struct dir *rmv_dir = dir_open(inode);

		struct dir_entry check;

		// dir에 다른 폴더 남아 있으면 false
		while (inode_read_at(rmv_dir->inode, &check, sizeof(check), rmv_dir->pos) == sizeof(check)){
			rmv_dir->pos += sizeof(check);
			if (check.in_use){
				if (strcmp(check.name, ".") && strcmp(check.name, "..")){
					return false;
				}
			}
		}

		// 현재 작업중인 dir 제거 하려하면 false
		struct dir *work_dir = thread_current()->working_dir;
		if (inode == dir_get_inode(work_dir)){
			dir_close(rmv_dir);
			return false;
		}

		// dir의 inode 여러번 열려 있으면
		if (inode_open_cnt(inode) > 2) {
			dir_close(rmv_dir);
			return false;
		}
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

/* Reads the next directory entry in DIR and stores the name in
 * NAME.  Returns true if successful, false if the directory
 * contains no more entries. */
// P4-4-3 dir_readdir 내부 수정 (., .. 넘기게)
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1]) {
	struct dir_entry e;

	if (dir->pos == 0){
		dir->pos += sizeof(e) * 2;
	}

	while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
		dir->pos += sizeof e;
		if (e.in_use) {
			
			// ., ..은 넘기게
			if (strcmp(e.name, ".") && strcmp(e.name, "..")){
				strlcpy (name, e.name, NAME_MAX + 1);
				return true;
			}
			
		}
	}
	return false;
}

// P4-4-3 dir_change 구현(syscall chdir에서 사용)
bool
dir_change (const char* dir){

	// root로 이동할때
	if (strcmp(dir, "/") == 0){ 
		dir_close(thread_current()->working_dir);
		thread_current()->working_dir = dir_open_root();
		return true;
	}

	// dir 얻는 과정
	char *name_dir = (char *) malloc(NAME_MAX + 1);
	if (name_dir == NULL){
		return false;
	}
	struct dir *new_dir = get_dir(dir, name_dir);

	// dir 얻었는데 NULL이면 clean하고 false
	if(new_dir == NULL){
		dir_close(new_dir);
		free(name_dir);
		return false;
	}

	// new_dir에 name_dir 있는지 확인
	struct inode *inode;
	dir_lookup(new_dir, name_dir, &inode);
	if (inode == NULL || !inode_is_dir(inode)){ // 없거나 파일이면 false
		inode_close(inode);
		dir_close(new_dir);
		free(name_dir);
		return false;
	}

	dir_close(thread_current()->working_dir);
	thread_current()->working_dir = dir_open(inode);
	dir_close(new_dir);
	free(name_dir);
	return true;
}

// P4-4-3 dir_make 구현
bool
dir_make(const char* dir){
	// 작업할 dir, 만들 dir name 얻는 과정
	char *name_make_dir = (char *) malloc(NAME_MAX + 1);
	if (name_make_dir == NULL){
		return false;
	}
	struct dir *work_dir = get_dir(dir, name_make_dir);

	// dir 얻었는데 NULL이면 clean하고 false
	if(work_dir == NULL){
		free(name_make_dir);
		dir_close(work_dir);
		return false;
	}

	// 이미 dir가 존재 하는 경우
	struct inode *inode;
	dir_lookup(work_dir, name_make_dir, &inode);
	if (inode != NULL){
		inode_close(inode);
		free(name_make_dir);
		dir_close(work_dir);
		return false;
	}
	
	// dir 생성
	disk_sector_t inode_sector = 0;
	bool succ = (work_dir != NULL
			&& (inode_sector = cluster_to_sector(fat_create_chain(0)))
			&& dir_create (inode_sector, 0)
			&& dir_add (work_dir, name_make_dir, inode_sector));

	if (!succ && inode_sector != 0){
		fat_remove_chain(sector_to_cluster(inode_sector), 0);
	}

	// dir에 ., .. 추가
	struct dir *make_dir = dir_open(inode_open(inode_sector));
	dir_add(make_dir, ".", inode_sector);
	dir_add(make_dir, "..", inode_get_inumber(dir_get_inode(work_dir)));

	// 마무리
	dir_close(make_dir);
	free(name_make_dir);
	dir_close(work_dir);
	return succ;

}

