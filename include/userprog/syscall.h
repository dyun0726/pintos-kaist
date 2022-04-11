#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"
#include "filesys/off_t.h"

void syscall_init (void);

// P2-2-1 user memory access 확인 함수
void check_address (void *addr);

// P2-3-1 System call 변수
struct lock syscall_lock;

int current_fd; // 현재 fd 개수

typedef int pid_t;

struct fd_list_elem { // fd_list에 사용할 elem
    int fd;
    struct list_elem elem;
    struct file *file_ptr;
};

// P2-3-1 System call 함수
void halt (void);
void exit (int status);
pid_t fork (const char *thread_name);
int exec (const char *cmd_line);
int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
#endif /* userprog/syscall.h */
