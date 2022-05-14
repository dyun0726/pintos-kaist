#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/syscall.h"
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;
	
	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	// P2-1-3 file_name 토큰화 변수
	char *save_ptr;
	strtok_r(file_name, " ", &save_ptr); // 'arg-multiple a b c' 에서 맨 앞에거만 넘겨주기

	/* Create a new thread to execute FILE_NAME. */
	//file_name 이름으로하고 우선순위 default 인 스레스 생성 및 반환
	// 스레드는 fn_copy를 인자로 받는 initd 실행
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy); 
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);

	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0){
		PANIC("Fail to launch initd\n");
	}
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	//* Clone current thread to new thread.*/
	return thread_create (name, PRI_DEFAULT, __do_fork, thread_current());
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable; // pte (parent page writable?)

	// P2-3-fork-4 do_fork에서 parent page 복사
	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va)){
		return true;
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL) {
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page (PAL_USER);
	if (newpage == NULL){
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy (newpage, parent_page, PGSIZE); // newpage에 copy
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		palloc_free_page(newpage);
		return false;

	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	// P2-3-fork-5 부모의 intr_frame 복사해서 전달
	struct intr_frame *parent_if = &parent -> parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	// 파일 오브젝트 복제
	// 부모 fd_list 돌면서 복제
	struct list_elem *e;
	for (e = list_begin(parent->fd_list); e != list_end(parent->fd_list); e = list_next(e)){
		struct fd_list_elem *tmp = list_entry(e, struct fd_list_elem, elem);
		struct file *dup_file = file_duplicate(tmp->file_ptr); //파일 복사
		if (dup_file == NULL){
			goto error;
		}
		
		struct fd_list_elem *dup = (struct fd_list_elem *) malloc(sizeof(struct fd_list_elem));

		if (dup == NULL){
			goto error;
		}

		dup->file_ptr = dup_file;
		dup->fd = tmp->fd;
		list_push_back(current->fd_list, &dup->elem);
	} // current의 fd_list에 복제 완료
	// printf("current : %d\n", current->running_file);

	process_init ();

	if_.R.rax = 0; // 자식 프로세스 return 0

	// 복제 끝났으므로 current (child) 그만 기다려도됨
	sema_up(&current->_do_fork_sema);
	// parent는 do_fork 끝날때 까지 block
	sema_down(&parent->_do_fork_sema); // 추추가

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	sema_up(&current->_do_fork_sema);
	current->exit_status = TID_ERROR;
	sema_down(&parent->_do_fork_sema); // 추추가
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name; // f_name void로 넘겨 받았으므로 char *로 변환
	bool success;

	// P2_1_1. argument parsing 변수 선언 
	// 파일명 추출하고 다른 인자들 stack에 저장
	char *parsing, *save;
	int argc = 0; 
	char *argv[64];

	// f_name parsing 해서 argv에 저장
	parsing = strtok_r(f_name, " ", &save);
	argv[argc] = parsing;

	while (parsing) {
		parsing = strtok_r(NULL, " ", &save);
		argc = argc + 1;
		argv[argc] = parsing;
	}

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	// P3-1 추가
	#ifdef VM
		supplemental_page_table_init(&thread_current()->spt);
	#endif

	/* And then load the binary */
	success = load(argv[0], &_if); // P2-1-1 file_name이 아니라 argv[0] load

	//sema_up(&thread_current()->parent->initd_sema); // initd

	/* If load failed, quit. */
	if (!success){ 
			//printf("load failed\n");
			return -1;
	}

	// P2-1-2 argument stack
	// 변수 선언
	int arg_len = 0; // alignment 하기위해 arg 차지하는 byte 필요
	char *arg_addr[64]; // stack에서 각 arg 주소 저장 배열
	
	for (int i = argc -1; i >= 0; i--){
		int len = strlen(argv[i]) + 1; // \0도 포함하기위해 +1
		arg_len = arg_len + len; // 각 arg 길이 합계
		// _if.rsp는 현재 위치 가리키는 stack pointer
		_if.rsp = _if.rsp - len; // user stack의 상단부터 argv[i] 크기만큼 이동
		memcpy(_if.rsp, argv[i], len); // argv[i]에서 arg_len 만큼읽어서 _if.rsp에 복사
		arg_addr[i] = _if.rsp; // stock pointer 주소 저장
	}

	// word-align 
	if ((arg_len % 8) != 0){
		_if.rsp = (char *)_if.rsp - 8 + (arg_len % 8);
		*(uint8_t *) _if.rsp = 0;
	}

	// stack에 문자열 주소 삽입, 처음에는 0
	_if.rsp = _if.rsp - 8;
	memset(_if.rsp, 0, sizeof(char **));
	// memset: sizeof(char **) 만큼 0 채우기
	for (int i = argc -1; i>=0; i--){
		_if.rsp = _if.rsp - 8;
		memcpy(_if.rsp, &arg_addr[i], sizeof(char **));
	}
	
	//fake return address 삽입
	_if.rsp = _if.rsp - 8;
	memset(_if.rsp, 0, sizeof(void *));

	_if.R.rdi = argc;
	_if.R.rsi = _if.rsp + 8;

	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);
	// stack 확인 함수

	/* Start switched process. */
	do_iret (&_if);
	palloc_free_page (file_name);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	// while(1);
	// for (int i = 0; i<1000000000; i++); //fork 완성 전까지 무한루프해제
	
	// P2-3-wait-2
	// child_tid 의 스레드를 찾아서 기다리고 종료되면 child_list에서 제거
	struct list_elem *e;
	for (e = list_begin(&thread_current()->child_list); e != list_end(&thread_current()->child_list); e = list_next(e)){
		struct thread *child = list_entry(e, struct thread, child_elem);

		if (child->tid == child_tid){
			// 부모는 child가 status 나올때까지 중지
			// sema_up(&thread_current()->_do_fork_sema);
			sema_down(&child->wait_status_sema);

			int child_status = child->exit_status;
			list_remove(&child->child_elem);
			
			// child 종료 되도 된다고 sema UP
			sema_up(&child->exit_child_sema);

			return child_status;
		}

	}

	return -1;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	// P2-3-exit
	// 1. 프로세스가 open 한 파일들 모두 닫기
	while (!list_empty(curr->fd_list)){
		struct fd_list_elem *tmp = list_entry(list_pop_front(curr->fd_list), struct fd_list_elem, elem);
		file_close(tmp->file_ptr);
		free(tmp); //malloc 한거 free
	}
	file_close(curr->running_file);
	curr->running_file = NULL;
	free(curr->fd_list);

	//2. orphan 고려 , child_list에서 빼기, 자식들 parent 삭제
	while (!list_empty(&curr->child_list)){
		struct thread *child = list_entry(list_pop_front(&curr->child_list), struct thread, elem);
		if (child->parent == curr){
			child->parent = NULL;
		}
	}

	process_cleanup();

	// 3.자식의 status 확정됬으니 wait_status semaphore up
	sema_up(&curr->wait_status_sema);
	// 4. 부모한테 status 넘겨 주고 free 해야되니까 sema_down; process_wait()에서 풀어줌
	sema_down(&curr->exit_child_sema);
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	// P2-5 denying writes to executables. 실행중인 파일의 변경 막아야함!
	t->running_file = file;
	file_deny_write(file);
	// allow_write는 file_close()에서 됨 -> process_exit

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable)){
									// printf("fail to load_segment\n");
									goto done;
								}
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// P2-5 denying writes to executables
	if (file != thread_current()->running_file){
		file_close (file);
	}
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	// P3-2-3 lazy_load_segment 내부 구현
	// segment를 파일에서 불러오기, 이 함수는 첫번째 page fault 발생시 호출됨
	
	uint8_t *pa = (page->frame)->kva; //실제 메모리 주소
	struct page_load_info *args = aux;
	uint32_t read_bytes = args->read_bytes;

	//파일에서 ofs만큼 커서이동 후 파일 읽기
	file_seek(args->file, args->ofs);
	uint32_t real_read_bytes = (uint32_t) file_read(args->file, pa, read_bytes);
	
	// 만약 실제 읽은 bytes랑 읽어햐하는 bytes 다르면 free and return false
	if (real_read_bytes != read_bytes){
		palloc_free_page(pa);
		return false;
	} else { // 같으면 0으로 zero_bytes만큼 초기화
		memset(pa+read_bytes, 0, args->zero_bytes);
		return true;
	}

}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		// P3-2-2 load_segment 함수 구현
		// project 2에선 바로 로드 했음 but project3에선
		// lazy_loading 구현 위해서 vm_alloc_page_with_initaializer 함수 호출
		// 이때 aux로 필요한 정보 넘겨주기

		// aux 설정
		struct page_load_info *aux = (struct page_load_info *) malloc(sizeof(struct page_load_info));
		aux->file = file;
		aux->ofs = ofs;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;

		// ofs 업데이트
		ofs += page_read_bytes;
		// printf("%d\n", ofs);

		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux)){
			//printf("load segment vm_alloc fail\n");
			return false;
		}
			

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	// P3-2-4 setup stack 수정
	// 첫번째 stack은 lazy loading 필요 없음. 바로 alloc, claim 호출
	bool alloc_succ = vm_alloc_page(VM_ANON|VM_MARKER_0, stack_bottom, true);
	if (!alloc_succ){
		// 에러나면 페이지 탐색 후 메모리 해제
		struct page *fail_page = spt_find_page(&thread_current()->spt, stack_bottom);
		palloc_free_page(fail_page);
		return false;
	}

	bool claim_succ = vm_claim_page(stack_bottom);
	if (!claim_succ){
		struct page *fail_page = spt_find_page(&thread_current()->spt, stack_bottom);
		palloc_free_page(fail_page);
		return false;
	}

	memset(stack_bottom, 0, PGSIZE);
	success = true;
	if_->rsp = USER_STACK;

	return success;
}
#endif /* VM */
