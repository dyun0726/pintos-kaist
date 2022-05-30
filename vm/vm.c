/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include <hash.h>
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include <string.h>

// P3-victim
struct list victim_list;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	// P3-victim-1 victim_list 초기화
	list_init(&victim_list);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	//printf("upage: %d\n", upage);

	/* Check wheter the upage is already occupied or not. */
	// upage(va)가 spt 에 있는지 체크
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		// P3-2-1 vm_alloc_page_with_initializer 내부 구현
		struct page *page = (struct page *) malloc(sizeof(struct page));
		if (page == NULL){
			return false;
		}

		// vm_type으로 initializer 설정
		bool (*initializer) (struct page *, enum vm_type, void *);
		if (VM_TYPE(type) == VM_ANON){
			initializer = anon_initializer;
		} else if (VM_TYPE(type) == VM_FILE){
			initializer = file_backed_initializer;
		} else {
			PANIC("unvaild vm_type");
			return false;
		}

		// page를 uninit으로 초기화
		uninit_new(page, upage, init, type, aux, initializer);
		
		// 받은 argument로 page 상태 설정
		page->writable = writable;
		page->page_vm_type = type;

		/* TODO: Insert the page into the spt. */
		// spt에 새로 만들어진 page 삽입
		if(spt_insert_page(spt, page)){
			return true;
		}

	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	/* TODO: Fill this function. */
	// P3-1-3 spt_find_page 함수 구현
	// spt에서 va를 갖는 page 리턴
	
	// va가 속해있는 페이지 시작 주소를 갖는 page 생성

	struct page p;
	p.va = pg_round_down(va);

	// hash_find로 page_hash_elem 있는지 찾기
	struct hash_elem *e = hash_find(&thread_current()->spt.hash_table, &p.page_hash_elem);
	// printf("e: %d\n", e);

	if (e == NULL){
		return NULL;
	} else {
		return hash_entry(e, struct page, page_hash_elem);
	}


}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	/* TODO: Fill this function. */
	// P3-1-4 spt_insert_page 함수 구현
	if (hash_insert(&spt->hash_table, &page->page_hash_elem) == NULL){
		// insert 성공 -> NULL
		return true;
	}
	return false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	 struct list_elem *victim_elem = list_front(&victim_list);


	// victim_list 에서 제거할 frame 선택
	 while (1){
			struct page *victim_page = list_entry (victim_elem, struct page, victim_list_elem);
			void *victim_addr = victim_page->va;

			// 해당 페이지에 access 하지 않은 경우
			if (pml4_is_accessed(&thread_current()->pml4, victim_addr) == false){
				list_remove(victim_elem);
				return victim_page->frame;
			} else { // accesss 한경우
				// 페이지의 accesssed bit 0으로 설정
				pml4_set_accessed(&thread_current()->pml4, victim_addr, 0);
				// victim_elem 다음으로 설정
				victim_elem = list_next(victim_elem);

				// victim_elem 마지막 element면 처음부터 다시
				if(victim_elem == list_end(&victim_list)){
					victim_elem = list_front(&victim_list);
				}

			}
		
	 }

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	// victim을 swap out 하기
	if (swap_out(victim->page) == false){ // swap_out 실패(error)시 NULL 반환
		return NULL;
	}

	// victim의 page 초기화
	victim->page = NULL;
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	/* TODO: Fill this function. */
	// P3-1-5 vm_get_frame 함수 구현
	void *new = palloc_get_page(PAL_USER);

	// 메모리 가득차서 새로운 프레임 생성 못하면 evict
	if (new == NULL){
		return vm_evict_frame();
	}

	// 새로 할당된 메모리와 페이지 연결
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	ASSERT (frame != NULL);

	// 프레임 정보 init
	frame->page = NULL; // 페이지와의 연결 아직 안함
	frame->kva = new; // 프레임에 새로만든 va 저장

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	// P3-3-2 vm_stack_growth 함수 구현
	// 조건에 맞을시 stack growth 실행하는 것

	void *round_addr = pg_round_down(addr);

	struct page *page;
	while((page = spt_find_page(&thread_current()->spt, round_addr)) == NULL){
		if((vm_alloc_page(VM_ANON|VM_MARKER_0, round_addr, true)) && vm_claim_page(round_addr)){
			memset(round_addr, 0, PGSIZE);
			round_addr += PGSIZE;
		} else { // alloc, claim 실패시
			palloc_free_page(round_addr);
			PANIC("alloc, claim fail in growth stack");
		}
	}

}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	// P3-2-5 page_fault 관리, 
	// exception.c의 page_fault 함수서 호출됨
	// 해당 fault가 vaild 한 fault인지 확인
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	void *stack_pointer = NULL;
	void *user_rsp = thread_current()->parent_if.rsp;

	// user가 kernel va에 접근하려고 하면 false
	if (user && is_kernel_vaddr(addr)){
		return false;
	}

	// user의 stack pointer 가져오기
	// f가 kernel에서 발생했으면 kernel_vaddr이므로
	if (is_kernel_vaddr(f->rsp)){
		stack_pointer = user_rsp;
	} else {
		stack_pointer = f->rsp;
	}

	// spt에서 addr 찾기
	struct page *page = spt_find_page(spt, addr);
	if (page == NULL){ // spt에 없는 page면
		// P3-3-1 stack growth 조건 추가
		// stack 다 찰경우 page 더 만들어서 추가하게 구현
		// stack pointer 8bytes 아래에서 page fault 발생가능
		// stack은 1MB까지 커질수 있게 구현
		if (user && write){
			if ( (USER_STACK - (1<<20)) < addr && addr < USER_STACK){
				if (((int)stack_pointer)-32 <= (int) addr){ //addr가 stack_pointer 8byte 아래 가르키면
					vm_stack_growth(addr); // stack growth
					return true;
				}
			}
		}

		return false;
	} else { // spt에 있는 page 경우
		// writable 금지 인데 write하려하면 false
		if (page->writable == 0 && write){
			return false;
		}
		return vm_do_claim_page (page);
	}

}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	// P3-1-7 vm_claim_page 구현
	page = spt_find_page(&thread_current()->spt, va);

	if (page == NULL){
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	if (frame == NULL){
		printf("frame is NULL\n");
		return false;
	}

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// P3-1-6 vm_do_claim_page 함수 내부 구현
	// Claim: 페이지를 프레임에 할당하는 것
	if (pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable) == false){
		return false;
	}
	
	// P3-victim-2 claim 할때 victim list에 추가
	list_push_back(&victim_list, &page->victim_list_elem);

	bool succ = swap_in (page, frame->kva);
	return succ;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	// P3-1-2. spt init 함수 구현
	hash_init(&spt->hash_table, page_hash_hash, page_hash_less, NULL);
	
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	// P3-2-6 supplemental_page_table_copy
	// src를 dst로 복사, do_fork에서 호출
	ASSERT(src != NULL);
	ASSERT(dst != NULL);

	struct hash_iterator iter;
	// iter를 설정
	hash_first(&iter, &src->hash_table);

	// hash_table 순회
	bool succ = true;
	struct page_load_info *aux= NULL;

	while (hash_next(&iter) != NULL){
		struct page *p = hash_entry(hash_cur(&iter), struct page, page_hash_elem);
		enum vm_type p_type = p->operations->type;

		switch (p_type){
			case VM_UNINIT: // lazy_loading이 한번도 일어나지 않음
				aux = (struct page_load_info *) malloc(sizeof(struct page_load_info));
				memcpy(aux, p->uninit.aux, sizeof(struct page_load_info));
				if (!vm_alloc_page_with_initializer(p->page_vm_type, p->va, p->writable, p->uninit.init, aux)){
					return false;
				}
				break;
			case VM_ANON:
				// 똑같은 va가리킬 page 복제
				if(!vm_alloc_page(VM_ANON|VM_MARKER_0, p->va, p->writable)){
					return false;
				}
				if(!vm_claim_page(p->va)){
					return false;
				}
				struct page *child_p = spt_find_page(&thread_current()->spt, p->va);
				memcpy(child_p->va, p->frame->kva, PGSIZE);
				break;
			case VM_FILE: // P3-4-3 추가 수정
				// aux 복제
				aux = (struct page_load_info *) malloc(sizeof(struct page_load_info));
				aux->file = p->file.file;
				aux->is_first_page = p->file.is_first_page;
				aux->num_left_page = p->file.num_left_page;
				aux->ofs = p->file.ofs;
				aux->read_bytes = p->file.read_bytes;
				aux->zero_bytes = p->file.zero_bytes;

				if(!vm_alloc_page_with_initializer(VM_FILE, p->va, p->writable, NULL, aux)){
					return false;
				}
				break;
			
			default:
				break;
				
		
		}

	}
	return succ;
}

// P3-2-7 spt_kill 구현 보조 함수
void spt_destroy_func(struct hash_elem *e, void *aux){
	struct page *page = hash_entry(e, struct page, page_hash_elem);
	vm_dealloc_page(page);
}


/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	
	// P3-2-7 spt_kill 함수 내부 구현
	hash_destroy(&spt->hash_table, spt_destroy_func);
}

// P3-1-2 보조 함수 구현
// bool hash_init (struct hash *, hash_hash_func *, hash_less_func *, void *aux)
// hash_hash_func: hash value 구해주는 함수
uint64_t
page_hash_hash (const struct hash_elem *e, void *aux){
	const struct page *p = hash_entry(e, struct page, page_hash_elem);
	
	//hash_bytes: Returns a hash of the SIZE bytes in BUF. */
	return hash_bytes(&p->va, sizeof(p->va));
}

// hash_less_func: hash_elem의 크기 비교 함수, hash_find()서 사용됨
bool
page_hash_less (const struct hash_elem *x, const struct hash_elem *y, void *aux){
	struct page *p_x = hash_entry(x, struct page, page_hash_elem);
	struct page *p_y = hash_entry(y, struct page, page_hash_elem);

	return p_x->va < p_y->va;
}


