#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"
#include "filesys/off_t.h"

void syscall_init (void);

// P2-2-1 user memory access 확인 함수
void check_address (void *addr);


// P2-3-1 System call 함수
void halt (void);
void exit (int status);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int write(int fd, const void *buffer, unsigned size);



#endif /* userprog/syscall.h */
