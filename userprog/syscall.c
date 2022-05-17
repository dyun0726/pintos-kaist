#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// P2 
// #include "threads/vaddr.h" // is_user_vaddr(addr) - check address
#include "threads/init.h" // power_off
#include "filesys/filesys.h" // filesys_create() , filesys_remove() 함수
#include "filesys/file.h" // close()
#include "devices/input.h" // input_getc()
#include <string.h> // memcpy()
#include "userprog/process.h" // pid_t
#include "vm/file.h" // do_mmap, do_munmap

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
// P2-3 System call 추가 함수
static bool fd_cmp(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
static struct file * fd_to_file(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	// P2-3 system call 
	current_fd = 3;
	lock_init(&syscall_lock); // synchronize lock 초기화
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// P2-3-fork-1 fork 위해 intr_frame f를 parent_if에 복사
	memcpy(&thread_current()->parent_if, f, sizeof(struct intr_frame));

	// printf("syscall, rax = %d\n", f->R.rax);

	// P2-3 system call 구현
	
	// f에서 레지스터 R 참조
	// R.rax -> system call number
	// argument 순서 rdi, rsi, rdx, r10, r8, r9
	// system call 함수 반환 값 rax에 저장

	switch (f->R.rax) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi);
			break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		// project 3추가
		// P3-4-1 mmap, munmap 추가
//#ifdef VM
		case SYS_MMAP:
			// printf("sys_mmap\n");
			f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
			break;
		case SYS_MUNMAP:
			// printf("sys_munmap\n");
			munmap(f->R.rdi);
			break;

//#endif

		default:
			printf ("system call!\n");
			thread_exit ();
			break;

	}
	
}

// P2-2-1 user memory access 확인 함수
// 주어진 포인터가 user memory 가르키는지 확인 (kern_base - 0)
// pointer 제대로 mapping 되어 있나?
void check_address (void *addr){
	//if (!is_user_vaddr(addr) || !pml4_get_page(thread_current()->pml4, addr)){
	if (!is_user_vaddr(addr)){
		exit(-1);
	}
}

// P2-3-1 System call 함수 추가
// pintos 종료
void halt (void){
	power_off();
}

// 현재 프로세스 종료 정상적이면 status 0
void exit (int status){
	struct thread *t = thread_current();
	t->exit_status = status;
	//P2-4 process termination message
	printf("%s: exit(%d)\n", t->name, status); 
	thread_exit();
}

// P2-3-fork-2 현재 프로세스 fork 해서 자식 프로세스 생성
pid_t fork (const char *thread_name){
	check_address(thread_name);
	// printf("first fork : %d", thread_current()->running_file);

	pid_t child_pid = (pid_t) process_fork(thread_name, &thread_current()->parent_if);
	// printf("%d \n", child_pid);

	if (child_pid == TID_ERROR){
		return TID_ERROR;
	}

	struct thread *child = NULL;
	for (struct list_elem *e = list_begin(&thread_current()->child_list); e != list_end(&thread_current()->child_list); e=list_next(e)){
		struct thread *tmp = list_entry(e, struct thread, child_elem);
		if (tmp->tid == child_pid){
			child = tmp;
			break;
		}
	} // child fork 하고 찾기

	if (child == NULL){ // process_fork
		return TID_ERROR;
	} else {
		// P2-3-fork-3 do_fork가 실행 되서 sema_up 될때까지 기다리기
		sema_down(&child->_do_fork_sema);

		if (child->exit_status == TID_ERROR)
			return TID_ERROR;
		
		sema_up(&thread_current()->_do_fork_sema); // 추추가
	}	
	return child_pid;
}

// 현재 프로세스를 cmd_line으로 변경 
int exec (const char *cmd_line){
	check_address(cmd_line);

	char *copy = (char *) malloc(strlen(cmd_line)+1);
	strlcpy(copy, cmd_line, strlen(cmd_line)+1);

	int result = process_exec(copy);
	free(copy);
	if (result == -1){
		exit(-1);
	}
	
	thread_current() -> exit_status = result;
	return result;
}

// P2-3-wait-1 함수
int wait (pid_t pid){
	return process_wait((tid_t) pid);
}

// file 이름으로 initial_size로 크기
bool create (const char *file, unsigned initial_size){
	check_address(file); // pointer 니까 check

	if (file == NULL){
		exit(-1);
		//return false;
	}

	lock_acquire(&syscall_lock); // synchronization
	bool success= filesys_create(file, initial_size);
	lock_release(&syscall_lock);

	return success;
}

// file 파일 삭제
bool remove (const char *file){
	check_address(file); //pointer 니까 check

	if (file == NULL){
		return false;
	}

	lock_acquire(&syscall_lock);
	bool success = filesys_remove(file);
	lock_release(&syscall_lock);

	return success;
}

// open
int open (const char *file){
	check_address(file);

	if (file == NULL){ // 파일 이름 오류
		return -1;
	}
		
	lock_acquire(&syscall_lock);
	struct file *file_open = filesys_open(file);
	lock_release(&syscall_lock);

	if (file_open == NULL){ //open 에러
		return -1;
	} else if (list_size(thread_current()->fd_list) <= 130) {
		struct fd_list_elem *fd_elem = malloc(sizeof(struct fd_list_elem));

		fd_elem -> fd = current_fd;
		fd_elem -> file_ptr = file_open;
		current_fd++;

		//P2-5 Denying writes to excutables
		// user program이 file을 수정하는 것을 금지
		if (!strcmp(file, thread_current()->name)){
			file_deny_write(file_open); //	allow는 file_close 함수 안에있음
		}

		list_insert_ordered(thread_current()->fd_list, &fd_elem->elem, fd_cmp, NULL);

		return fd_elem -> fd;

	} else { //파일 너무 많이 열면 에러
		lock_acquire(&syscall_lock);
		file_close(file_open);
		lock_release(&syscall_lock);
		return -1;
	}

}

// fd 번호인 filesize 반환
int filesize (int fd){
	struct file *file_ptr = fd_to_file(fd);

	if (file_ptr == NULL){
		return -1;
	} else {
		return file_length(file_ptr);
	}

}

// read 함수 fd 구별 해서 작동
int read (int fd, void *buffer, unsigned size){
	// printf("read first\n");
	check_address(buffer);
	// printf("check succ\n");
	int count = 0;

	if (fd == 0) { // 0(STDIN)일때 input_getc
		lock_acquire(&syscall_lock);
		count = input_getc();
		lock_release(&syscall_lock);

		return count; 
	} else if (fd == 1) {// 1(STDOUT) 일때 return -1
		exit(-1);
		return -1;
	} else {
		struct file *fd_file = fd_to_file(fd); //fd로 file 변환

		// P3 pt-write-code2 PASS 수정
		#ifdef VM
		if (spt_find_page(&thread_current()->spt, buffer) != NULL && spt_find_page(&thread_current()->spt, buffer)->writable == 0){
			exit(-1);
		}
		#endif

		if (fd_file != NULL){
			lock_acquire(&syscall_lock);
			count = file_read(fd_file, buffer, size);
			lock_release(&syscall_lock);

			return count;
		} else {
			exit(-1);
			return -1;
		}
	}
}

// write 함수, 버퍼에서 size만큼 불러와 파일에 작성
int write(int fd, const void *buffer, unsigned size){
	check_address(buffer); // pointer

	if (fd == 1){ // fd가 1 = write to console (출력), putbuf 사용
		lock_acquire(&syscall_lock);
		putbuf(buffer, size); // buffer에 size만큼 console에 출력
		lock_release(&syscall_lock);

		return size;
	} else if (fd == 0) {
		return -1;
	} else {
		struct file *fd_file = fd_to_file(fd); //fd로 file 변환

		if (fd_file == NULL){
			return -1;
		} else { // fd_file 정상적이면
			int count;

			lock_acquire(&syscall_lock);
			count = file_write(fd_file, buffer, size);
			lock_release(&syscall_lock);

			return count;
		}
	}
}

void close (int fd){
	bool find_fd = false;
	struct fd_list_elem * close_fd_list_elem = NULL;

	struct list_elem *e;
	for (e = list_begin(thread_current()->fd_list); e != list_end(thread_current()->fd_list); e=list_next(e)){
		struct fd_list_elem *tmp = list_entry(e, struct fd_list_elem, elem);
		if (fd == tmp->fd){
			close_fd_list_elem = tmp;
			break;
		}
	}

	if (close_fd_list_elem == NULL){
		exit(-1);
	} else {
		list_remove(e); // fd_list에서 elem 제거

		lock_acquire(&syscall_lock);
		file_close(close_fd_list_elem->file_ptr);
		lock_release(&syscall_lock);

		// open 에서 malloc 했던거 free
		free(close_fd_list_elem); 
	}

}

// fd의 file 수정 위치 position으로 이동
void seek (int fd, unsigned position){
	file_seek(fd_to_file(fd), position); //file.c
}

// fd의 file 수정 위치 반환
unsigned tell (int fd){
	return (unsigned) file_tell(fd_to_file(fd)); //file.c
}


// P3-4-1 mmap, munmap 구현
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset){
	// offset 부터 length bytes만큼 addr에 mapping 함수

	// addr fail 조건: page-align안되있을때, NULL 일때
	if ( is_kernel_vaddr(addr) ){ // addr가 kernel address면 fail
		return NULL;
	}
	if ( addr == NULL || (pg_round_down(addr)!= addr)){
		return NULL;
	}

	// length fail 조건: 0일때, (추가) 0보다 작을때, Kern_base 보다 클때(mmap-kernel)
	if (length <= 0 || length >= KERN_BASE){
		return NULL;
	}

	// fd 조건: 0,1 이면 안됨(stdin, stdout) - Stdin,stdout mapping 금지
	if (fd <= 1){
		return NULL;
	}
	// offset 조건
	if (pg_round_down(offset)!=offset){
		return NULL;
	}

	struct file *fd_file = fd_to_file(fd);
	if (fd_file == NULL){ // fd로 file 못찾으면 fail
		return NULL;
	}
	// printf("addr: %d\n", addr);
	// printf("file_length: %d\n", file_length(fd_file));
	// file 크기가 0, offset이 file 크기보다 크면 fail
	if (file_length(fd_file) == 0 || file_length(fd_file) <= offset){
		return NULL;
	}
	// printf("do mmap before!\n");

	return do_mmap(addr, length, writable, fd_file, offset);

}

void munmap (void *addr){
	// addr가 align 안되있을 경우
	if (pg_round_down(addr) != addr){
		return;
	}

	struct page *page = spt_find_page(&thread_current()->spt, addr);
	// page를 찾을 수 없다면 -> addr로 된 page 없음 -> return
	if ( page == NULL ){
		return;
	}

	// page type VM_File 아니면 fail
	if (!page->operations->type != VM_FILE){
		return;
	}

	// page가 첫번째가 아니면 fail
	if (!page->file.is_first_page){
		return;
	}

	do_munmap(addr);
}



// helper 함수들
// fd 비교 함수
static bool fd_cmp(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	return (list_entry(a, struct fd_list_elem, elem) -> fd < list_entry(b, struct fd_list_elem, elem) -> fd);
}

// fd로 file 찾는 함수
static struct file * fd_to_file(int fd){
	struct list_elem *e;
	for (e = list_begin(thread_current()->fd_list); e != list_end(thread_current()->fd_list); e=list_next(e)){
		if (fd == list_entry(e, struct fd_list_elem, elem)->fd){
			return list_entry(e, struct fd_list_elem, elem)->file_ptr;
		}
	}
	return NULL;
}