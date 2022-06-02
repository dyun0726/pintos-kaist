#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;
	unsigned int fat_start;
	unsigned int fat_sectors; /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs {
	struct fat_boot bs;
	unsigned int *fat;
	unsigned int fat_length;
	disk_sector_t data_start;
	cluster_t last_clst;
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

void
fat_init (void) {
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	// Write FAT boot sector
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	// Create FAT boot
	fat_boot_create ();
	fat_fs_init ();

	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// Set up ROOT_DIR_CLST
	// root directory FAT에서 1
	fat_put (ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

void
fat_fs_init (void) {
	/* TODO: Your code goes here. */

	// P4-1-1 fat_fs_init 구현, FAT file system init
	// fat_length, data_start, lock init 해야함

	// fat_length: how many clusters in the filesystem
	fat_fs->fat_length = (fat_fs->bs.total_sectors - fat_fs->bs.fat_sectors)/ SECTORS_PER_CLUSTER;

	// data_start: which sector we can start to store files
	fat_fs->data_start = fat_fs->bs.fat_start + fat_fs->bs.fat_sectors + 1;

	// lock init
	lock_init(&fat_fs->write_lock);
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t
fat_create_chain (cluster_t clst) {
	/* TODO: Your code goes here. */
	// P4-1-2 fat_create_chain 함수 구현

	cluster_t ept_clst = first_empty_cluster();
	if (ept_clst == 0){ // empty cluster 없으면 return 0;
		return 0;
	}

	if (clst != 0){ // clst에 ept_clst 넣고, ept_clst엔 EOChain
		fat_put(ept_clst, EOChain);
		fat_put(clst, ept_clst);
	} else { // clst 0이면 new chain start
		fat_put(ept_clst, EOChain);
	}

	// 추가한 cluster, disk에 할당
	static char ept_disk[DISK_SECTOR_SIZE];
	disk_write(filesys_disk, cluster_to_sector(ept_clst), ept_disk);
	return ept_clst;
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */
	
	// P4-1-3 fat_remove_chain 함수 구현
	// pclst 가 0 이거나(clst가 처음인 fat) pclst 다음이 clst 여야함
	ASSERT(pclst == 0 || fat_get(pclst) == clst);

	if (pclst != 0){ // clst가 처음이 아니면
		fat_put(pclst, EOChain); // pclst 를 마지막으로 만들어야함
	}

	cluster_t i = clst;
	while (i != EOChain){
		cluster_t tmp = fat_get(i);
		fat_put(i, 0);
		i = tmp;
	}
}

/* Update a value in the FAT table. */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
	// P4-1-4 fat_put 구현

	// clst 0이하이면 return (0은 boot sector)
	if (clst <= 0){
		return;
	}

	// fat 크기보다 clst 크면 return
	if (clst >= fat_fs->fat_length){
		return;
	}

	lock_acquire(&fat_fs->write_lock);
	fat_fs->fat[clst] = val;
	lock_release(&fat_fs->write_lock);

}

/* Fetch a value in the FAT table. */
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
	// P4-1-5 fat_get 함수 구현

	// clst 0이하이면 (0은 boot sector)
	if (clst <= 0){
		PANIC("clst <= 0");
	}

	// fat 크기보다 clst 크면
	if (clst >= fat_fs->fat_length){
		PANIC ("clst too large");
	}

	return fat_fs->fat[clst];
}

/* Covert a cluster # to a sector number. */
// FAT 1부터 시작, 1은 root directory
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	// P4-1-6 cluster_to_sector 함수 구현
	if (clst == 0){
		return 0;
	}
	return fat_fs->data_start + (clst-2) * SECTORS_PER_CLUSTER;
}

// P4-1-2 보조함수, 첫번째로 비어있는 cluster return, 비어있는 cluster 없으면 0
static cluster_t
first_empty_cluster (void){
	for (cluster_t i = 2; i < fat_fs->fat_length; i++){
		if (fat_get(i) == 0){
			return i;
		}
	}
	// 비어있는 cluster 없음
	return 0;
}

// P4-2 보조함수, sector를 cluster로
cluster_t
sector_to_cluster (disk_sector_t sector) {
   return ((sector - fat_fs->data_start) / SECTORS_PER_CLUSTER) + 2;
}
