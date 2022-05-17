/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "filesys/file.h" 
#include "threads/vaddr.h" // pg_round_up
#include "userprog/process.h"
#include "threads/mmu.h" // pml4_is_dirty

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	// P3-4-3 file.c 함수들 수정
	struct page_load_info *aux = page->uninit.aux;
	file_page->file = aux->file;
	file_page->ofs = aux->ofs;
	file_page->read_bytes = aux->read_bytes;
	file_page->zero_bytes = aux->zero_bytes;
	file_page->is_first_page = aux->is_first_page;
	file_page->num_left_page = aux-> num_left_page;

}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	// P3-5-6 file_swap_in 구현
	struct page_load_info *aux = (struct page_load_info *) malloc (sizeof(struct page_load_info));
	aux->file = file_page->file;
	aux->is_first_page = file_page->is_first_page;
	aux->num_left_page = file_page->num_left_page;
	aux->ofs = file_page->ofs;
	aux->read_bytes = file_page->read_bytes;
	aux->zero_bytes = file_page->zero_bytes;

	return file_lazy_load_segment(page, (void *) aux);
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	// P3-5-5 file_swap_out 구현
	// page가 수정된 적 있으면 파일도 수정 해야됨
	uint64_t *cur_pml4 = thread_current()->pml4;
	bool is_dirty = pml4_is_dirty(cur_pml4, page->va);
	
	// 수정된 이력이 있다면 파일 수정
	if (is_dirty == true){
		file_seek(file_page->file, file_page->ofs);
		file_write(file_page->file, page->va, file_page->read_bytes);
		pml4_set_dirty(cur_pml4, page->va, 0); // dirty bit도 원래대로
	} 
	pml4_clear_page(cur_pml4, page->va);
	page->frame = NULL;

	return true;

}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	// P3-4-3 file.c 내부 함수들 수정
	// 메모리에 불러온 내용이 수정되었으면 끌때 실제 파일에도 수정시켜야함
	if (pml4_is_dirty(thread_current()->pml4, page->va)){ // dirty(수정되었으면)하면
		file_seek(file_page->file, file_page->ofs);
		file_write(file_page->file, page->va, file_page->read_bytes);
	}
	page->writable = true;
	memset(page->va, 0, PGSIZE); // va의 메모리 0으로 초기화
	hash_delete(&thread_current()->spt.hash_table, &page->page_hash_elem);
	
	if (page->frame){
		free(page->frame);
	}

	page->frame = NULL;
	page->file.file = NULL;
	page->file.is_first_page = NULL;
	page->file.num_left_page = NULL;
	page->file.ofs = NULL;
	page->file.read_bytes = NULL;
	page->file.zero_bytes = NULL;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	// P3-4-2 do_mmap 구현 (syscall.c mmap 함수서 호출)
	// load_segment의 file 형태

	// ASSERT(addr != NULL);
	// ASSERT(length != 0);
	// ASSERT(file != NULL);
	// ASSERT(pg_round_down(addr) == addr);

	// 읽어야할 bytes 수
	uint32_t real_read_bytes;
	if (length < file_length(file)){
		real_read_bytes = length;
	} else {
		real_read_bytes = file_length(file);
	}

	// zero로 채워야할 bytes
	uint32_t zero_bytes = pg_round_up(real_read_bytes) - real_read_bytes;
	
	// 만들 페이지 개수
	int page_num = (int) pg_round_up(real_read_bytes) / PGSIZE;

	// 만들 페이지가 이미 존재 하는지 확인
	for (int i = 0; i < page_num; i++){
		if(spt_find_page(&thread_current()->spt, addr + i * PGSIZE) != NULL){
			return NULL;
		}
	}

	bool is_first = true; // 첫번째 page인가?
	void *upage = addr; // return을 처음 addr 해야되서

	while (real_read_bytes > 0 || zero_bytes > 0){
		// page에 read할 bytes
		size_t page_read_bytes;
		if (real_read_bytes > PGSIZE){
			page_read_bytes = PGSIZE;
		} else {
			page_read_bytes = real_read_bytes;
		}
		// page에 zero넣을 bytes
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		// initializer에 필요한 aux 설정
		struct page_load_info *aux = (struct page_load_info *) malloc(sizeof(struct page_load_info));
		struct file *reopen_file = file_reopen(file);
		aux->file = reopen_file;
		aux->ofs = offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;
		aux->is_first_page = is_first;
		aux->num_left_page = page_num -1;

		// is_first, page_num, offset update
		if (is_first){
			is_first = false;
		}
		page_num = page_num -1;
		offset = offset + page_read_bytes;

		// VM_FILE -> vm_alloc_page_with_init
		if (!vm_alloc_page_with_initializer (VM_FILE, upage, writable, file_lazy_load_segment, aux)){
			//printf("file load segment vm_alloc fail\n");
			return NULL;
		}

		// read_bytes, zero_bytes, addr update
		real_read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;

	}

	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct page *first_page = spt_find_page(&thread_current()->spt, addr);
	int munmap_page_num = first_page->file.num_left_page;

	struct file *file = first_page->file.file;

	for (int i =0 ; i<= munmap_page_num; i++){
		struct page *del =spt_find_page(&thread_current()->spt, addr + i * PGSIZE);
		if (del == NULL){
			PANIC("In munmap, no page");
		}
		spt_destroy_func(&del->page_hash_elem, NULL);

	}
	file_close(file);
}

// P3-4-2 initializer에 필요한 함수 file_lazy_load_segment
static bool file_lazy_load_segment (struct page *page, void *aux){
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
		free(aux);
		return true;
	}

}