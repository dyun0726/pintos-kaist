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
#include "threads/vaddr.h" // is_user_vaddr(addr) - check address
#include "threads/init.h" // power_off
#include "filesys/filesys.h" // filesys_create() , filesys_remove() 함수

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	// P2-3-1 system call 구현

	// f에서 레지스터 R 참조
	// R.rax -> system call number
	// argument 순서 rdi, rsi, rdx, r10, r8, r9
	// system call 함수 반환 값 rax에 저장
	// printf("-----%d \n", f->R.rax);
	switch (f->R.rax) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		// case SYS_FORK:
		// 	break;
		// case SYS_EXEC:
		// 	break;
		// case SYS_WAIT:
		// 	break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;


		
		default:
			printf ("system call!\n");
			thread_exit ();
			break;

	}
	
}

// P2-2-1 user memory access 확인 함수
// 주어진 포인터가 user memory 가르키는지 확인 (kern_base - 0)
void check_address (void *addr){
	if (!is_user_vaddr(addr)){
		exit(-1);
	}
}

// P2-3-1 System call 함수 추가
// pintos 종료
void halt (void){
	power_off();
}

// 현재 프로세스 종료
void exit (int status){
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n", t->name, status); //process termination message
	// 정상적으로 종료 -> status 0
	thread_exit();
}

// file 이름으로 initial_size로 크기
bool create (const char *file, unsigned initial_size){
	check_address(file); // pointer 니까 check

	if (file == NULL){
		exit(-1);
	}

	bool success= filesys_create(file, initial_size);

	return success;
}

// file 파일 삭제
bool remove (const char *file){
	check_address(file); //pointer 니까 check

	if (file == NULL){
		exit(-1);
	}

	bool success = filesys_remove(file);

	return success;
}

// write 함수
int write(int fd, const void *buffer, unsigned size){
	check_address(buffer); // pointer

	if (fd == 1){ // fd가 1 = 출력하라
		putbuf(buffer, size); // buffer에 size만큼 console에 출력

		return size;
	} else {
		exit(-1);
	}

	NOT_REACHED();
}