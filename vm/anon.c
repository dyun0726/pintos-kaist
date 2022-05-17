/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

// P3-5-0 args_swap 변수 선언
static struct args_swap anon_args_swap;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	
	// P3-5-1 bitmap(swap_table) 초기화 및 swap할 disk 설정
	// disk 설정 (1,1)-> use for swap
	swap_disk = disk_get(1,1);

	// swap disk의 사이즈 반환 (disk_sector_t로),  8sector가 하나의 페이지, 페이지 개수만큼 bitmap 생성
	anon_args_swap.swap_table = bitmap_create( disk_size(swap_disk) / 8 ); 

	// lock_init
	lock_init(&anon_args_swap.lock_swap);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;

	// P3-5-2 anon_initializer 수정 - num_swap_table 초기화(-1로 초기화)
	anon_page->num_swap_table = -1;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	// P3-5-4 anon_swap_in 구현
	
	int sect_size = PGSIZE / 8;
	size_t idx = anon_page->num_swap_table;

	if (bitmap_test(anon_args_swap.swap_table, idx) == false){
		PANIC("anon_swap_in NOT in swap_table");
		return false;
	}
	
	// sector read 하기
	for (int i = 0 ; i < 8; i++){
		void *addr = page->frame->kva + sect_size * i;
		disk_read(swap_disk, 8*idx+i, addr);
	}

	// swap_table 수정
	bitmap_set_multiple(anon_args_swap.swap_table, idx, 1, false);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// P3-5-3 anon_swap_out 구현
	
	// swap_table에서 bit 0인곳 찾기
	lock_acquire(&anon_args_swap.lock_swap);
	size_t bit = bitmap_scan_and_flip(anon_args_swap.swap_table, 0, 1, false); // 0부터 false 갖는 1개 bit 찾고, 그값 바꾸고 first bit 반환
	anon_page->num_swap_table = bit; // page에 위치 저장
	lock_release(&anon_args_swap.lock_swap);

	if (bit == BITMAP_ERROR){
		PANIC("anon_swap_out BITMAP_ERROR\n");
		return false;
	}

	// disk에 page 삽입
	int sect_size = PGSIZE /8;
	for (int i = 0 ; i < 8; i++){
		void *addr = page->frame->kva + sect_size * i;
		disk_write(swap_disk, 8*bit + i, addr);
	}

	// page frame 변경, pml4에서 지우기
	pml4_clear_page(thread_current()->pml4, page->va);
	page->frame = NULL;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	
	// P3-2-9 anon_destory 내부 구현
	if (page->frame != NULL){
		free(page->frame);
	}
	if (anon_page->aux != NULL){
		free(anon_page->aux);
	}
}
