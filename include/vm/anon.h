#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include <bitmap.h>
#include "threads/synch.h"
struct page;
enum vm_type;

struct anon_page {
    void *padding; // 왜 있지?
    enum vm_type type;
    struct page_load_info *aux;
    // P3-5-0 swap 필요 변수
    int num_swap_table;
};

// P3-5-0 swap_table 변수 선언
struct args_swap {
    struct bitmap *swap_table;
    struct lock lock_swap;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
