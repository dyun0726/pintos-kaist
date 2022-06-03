#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/fat.h" // P4-2-0 FAT 추가
#include "threads/thread.h" // P4-4-2 추가

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();

	// P4-4-2 working dir 설정
	// 맨처음 thread는 root dir에서 시작
	thread_current()->working_dir = dir_open_root();

#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */

// P4-2-1 filesys_create 함수 추가 (project4 용)
// FAT 사용하도록 free_map을 fat으로 수정
#ifdef EFILESYS
bool
filesys_create (const char *name, off_t initial_size) {
	disk_sector_t inode_sector = 0;

	// P4-4-2 파일 뿐만 아니라 디렉토리도 생성가능
	// 또한 root dir에만 생성 하는게 아니라 하위 디렉토리도 가능하게 수정
	char *name_file = (char *) malloc(NAME_MAX+1);
	// name 분석해서 그 dir로 이동
	struct dir *dir = get_dir(name, name_file);

	// dir 실제 dir인지 확인
	// if(dir == NULL){
	// 	goto clean:
	// }
	
	// rootd dir 에만 파일 생성하는게 아님 -> 주석 처리
	// struct dir *dir = dir_open_root ();

	bool success = (dir != NULL
			&& (inode_sector = cluster_to_sector(fat_create_chain(0)))
			&& inode_create (inode_sector, initial_size, true) // P4-4-2 inode_create 수정
			&& dir_add (dir, name_file, inode_sector));
	if (!success && inode_sector != 0)
		fat_remove_chain(sector_to_cluster(inode_sector), 0);

	free (name_file);
	dir_close (dir);
	return success;

// clean:
// 	free(name_file);
// 	dir_close(dir);
// 	return false;
}

#else

bool
filesys_create (const char *name, off_t initial_size) {
	disk_sector_t inode_sector = 0;
	struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size, true) // P4-4-2 inode_create 수정
			&& dir_add (dir, name, inode_sector));
	if (!success && inode_sector != 0)
		free_map_release (inode_sector, 1);
	dir_close (dir);

	return success;
}

#endif

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
// P4-4-3 filesys_open 수정 - syscall open
#ifdef EFILESYS
struct file *
filesys_open (const char *name) {

	if (strcmp(name, "/") == 0){ //root 입력시
		return dir_open_root();
	}

	struct file *result = NULL;

	// file, work_dir 찾기
	char *file_name = (char *) malloc(NAME_MAX+1);
	struct dir *work_dir = get_dir(name, file_name);

	// work_dir 못찾았으면 정리후 return NULL
	if (work_dir == NULL){
		free(file_name);
		dir_close(work_dir);
		return result;
	}

	// file이 work_dir에 있는지 확인한다
	struct inode *inode = NULL;
	dir_lookup(work_dir, file_name, &inode);

	// 파일 없으면 정리후 return NULL
	if (inode == NULL){
		free(file_name);
		dir_close(work_dir);
		return result;
	}

	// P4-5-2 추가: 열 파일이 soft_link 이면
	if (inode_is_soft_link(inode)){
		return filesys_open(inode_soft_link_path(inode));
	}

	// 해당파일 open
	result = file_open(inode);

	// 정리 후 return
	free(file_name);
	dir_close(work_dir);
	return result;
}

#else
struct file *
filesys_open (const char *name) {
	struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);

	return file_open (inode);
}
#endif

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
// P4-4-3 filesys_remove 수정 (dir)
#ifdef EFILESYS
bool
filesys_remove (const char *name) {

	if (strcmp(name, "/") == 0){ //root 지우는 것 false
		return false;
	}
	
	// 이름 임시 저장
	char *tmp_name = (char *) malloc(strlen(name) + 1);
	if (strlen(name) < NAME_MAX){
		memcpy(tmp_name, name, strlen(name) + 1);
	} else {
		memcpy(tmp_name, name, NAME_MAX + 1);
	}

	char *file_name = (char *) malloc(NAME_MAX+1);

	// name parsing
	struct dir *work_dir = get_dir(tmp_name, file_name);

	// 해당 file(or dir) 지우기 시도
	bool success = work_dir != NULL && dir_remove (work_dir, file_name);
	dir_close (work_dir);

	free(tmp_name);
	free(file_name);

	return success;
}

#else
bool
filesys_remove (const char *name) {
	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;
}
#endif

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();

	// P4-4-1 root directory 생성 구현
	bool dir_create_succ = dir_create(cluster_to_sector(ROOT_DIR_CLUSTER), 2);
	if (!dir_create_succ){
		PANIC("root directory creation failed");
	}

	// // P4-2-2 dir_create 추가, 4-4-1추가하면서 주석
	// if (!dir_create(cluster_to_sector(ROOT_DIR_CLUSTER), 16)){
	// 	PANIC("root directory creation failed");
	// }

	fat_close ();

	// disk에 저장한 root directory open
	struct dir *root_dir;
	root_dir = dir_open_root();
	disk_sector_t root_inode_sector = inode_get_inumber(dir_get_inode(root_dir));

	// 초기화 (., .. 추가)
	dir_add(root_dir, ".", root_inode_sector);
	dir_add(root_dir, "..", root_inode_sector);
	
	// directory close
	dir_close(root_dir);
	
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}

// P4-4-2 보조함수
struct dir *get_dir(char *name, char *file_name){
	// name, file_name NULL아니어야함
	ASSERT( name != NULL && file_name != NULL);

	// name 전체 복사해고
	char *path = (char *) malloc(strlen(name) + 1);
	memcpy(path, name, strlen(name) + 1);

	// parsing 해서 dir 얻기
	struct dir *dir = path_parsing(path, file_name);

	// malloc - free
	free(path);

	return dir;
}

// path parsing 해서 작업을 진행할 dir 리턴
// file_name 이름(파일 or dir) 저장할 주소
struct dir *path_parsing(char *path_name, char *file_name){
	// path_name과 file_name NULL이면 안됨
	ASSERT(path_name != NULL && file_name != NULL);

	struct dir *working_dir = thread_current()->working_dir;

	// path_name 아무것도 없으면 return NULL;
	if (strlen(path_name) == 0){
		return NULL;
	}

	struct dir *dir;
	if (path_name[0] == '/'){ // 절대경로 일때
		dir = dir_open_root();
		path_name += 1;

		// path_name이 "/" 뿐이면 return "/"
		if (strlen(path_name) == 0){
		return dir; 
		}

	} else { // 상대 경로 일때
		dir = dir_reopen(working_dir);
	}

	// 남은 path parsing
	char *save, *parsing, *remain;
	parsing = strtok_r(path_name, "/", &save);
	remain = strtok_r(NULL, "/", &save);

	struct inode *return_inode;
	while (parsing != NULL && remain != NULL){
		// dir 에서 parsing한거 찾아서 return_inode에 저장
		dir_lookup(dir, parsing, &return_inode);

		// parsing이 경로 없으면
		if (return_inode == NULL){
			goto done;
		}
		// dir close
		dir_close(dir);

		// soft link 후에 추가
		// P4-5-2 찾은 inode가 soft link 일 경우 추가
		if (inode_is_soft_link(return_inode)){
			char *name_file = (char *) malloc(NAME_MAX+1);
			char *soft_link_path = inode_soft_link_path(return_inode);

			dir = get_dir(soft_link_path, name_file);

			// dir 얻기 오류
			if (dir == NULL){
				free(name_file);
				PANIC("PATH PARSING: soft_link error");
			}

			inode_close(return_inode);

			// 얻은 dir에서 파일 찾기
			dir_lookup(dir, name_file, &return_inode);

			// 없으면 return false
			if (return_inode == NULL){
				dir_close(dir);
				inode_close(return_inode);
				return NULL;
			}
		}

		// return inode가 dir가 아니면
		if(inode_is_dir(return_inode) == false){
			dir_close(dir);
			inode_close(return_inode);
			return NULL;
		}

		//dir 에 새로 찾은 dir 저장
		dir = dir_open(return_inode);

		// parsing 변수들 새로고침
		parsing = remain;
		remain = strtok_r(NULL, "/", &save);

	}
	// while문 끝나면 parsing에 file 이름, remain은 NULL 일 것

	// parsing 크기 측정
	int len_file_name;
	if (NAME_MAX > strlen(parsing)){
		len_file_name = strlen(parsing) + 1;
	} else {
		len_file_name = NAME_MAX + 1;
	}

	// file_name에 parsing 저장
	memcpy(file_name, parsing, len_file_name);

	goto clean;
	
done:
	dir_close(dir);
	inode_close(return_inode);
	return NULL;

clean:
	return dir;

}

// P4-5-1 systemcall symlink 구현
int filesys_make_symlink(const char* target, const char* linkpath){
	// target, linkpath 이상할때
	if (target == NULL || linkpath == NULL || strlen(target) == 0 || strlen(linkpath)== 0){
		return -1;
	}

	// linkpath parsing
	char *name_file = (char *) malloc(NAME_MAX+1);
	struct dir *work_dir = get_dir(linkpath, name_file);

	// work_dir 얻기 실패
	if (work_dir == NULL){
		free(name_file);
		dir_close(work_dir);
		return -1;
	}

	disk_sector_t inode_sector;
	bool succ = ((inode_sector = cluster_to_sector(fat_create_chain(0)))
					&& inode_create(inode_sector, 0, true)
					&& dir_add(work_dir, name_file, inode_sector));
	
	if(!succ && inode_sector != 0){
		fat_remove_chain(sector_to_cluster(inode_sector), 0);
		free(name_file);
		dir_close(work_dir);
		return -1;
	}


	// set soft link
	if (!inode_set_soft_link(inode_sector, target)){ // set softlink 실패
		free(name_file);
		dir_close(work_dir);
		return -1;
	} else { // set softlink 성공
		free(name_file);
		dir_close(work_dir);
		return 0;
	}
	
}
