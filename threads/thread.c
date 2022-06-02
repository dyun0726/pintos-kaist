#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/fixed_point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
#ifdef EFILESYS
#include "filesys/directory.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

static struct list sleep_list; //sleep thread list
static int64_t next_wakeup_tick; // wakeup 실행할 다음 틱

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

// advanced schedular default
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

int load_avg;

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);

	list_init (&sleep_list); //init sleep_list 

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();

	// P4-4-2 working_dir init
#ifdef EFILESYS
	initial_thread->working_dir = NULL;
#endif
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);
	load_avg = LOAD_AVG_DEFAULT;

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

// P4-4-2 추가
#ifdef EFILESYS
	if(thread_current()->working_dir != NULL){
		t->working_dir = dir_reopen(thread_current()->working_dir);
	}
#endif

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

// P2-3 system call fd_list 메모리 할당 및 초기화
#ifdef USERPROG
	t -> fd_list = (struct list *) malloc(sizeof(struct list));
	if (t->fd_list == NULL) {
		return TID_ERROR;
	}
	list_init(t->fd_list);

	// P2-3-fork-2 parent thread의 child list 에 현재 thread 추가
	t->parent = thread_current();
	list_push_back(&thread_current()->child_list, &t->child_elem);

#endif
	/* Add to run queue. */
	thread_unblock (t);

	// create된 thread가 current thread보다 우선순위가 높다면
	int curr_priority = thread_get_priority();
	if (priority > curr_priority){
		thread_yield(); // current thread cpu 양보
	}

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	
	list_insert_ordered (&ready_list, &t->elem, thr_cmp_priority, NULL);
	// priority에 따라 ready list에 insert // list_push_back (&ready_list, &t->elem);

	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, thr_cmp_priority, NULL); // ready_list 넣을때 순서대로
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	if (thread_mlfqs){ // advanced에선 priority 마음대로 바꿀수 없음
		return ;
	}

	thread_current ()-> init_priority = new_priority;

	update_priority(); // 바뀐  init_priority와 donation_list로 priority 결정(update)

	test_highest_priority(); // priority 바뀐 후 가장 높은 priority인지 확인
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level = intr_disable();
	thread_current() -> nice = nice;
	cal_priority(thread_current());
	test_highest_priority();
	intr_set_level(old_level);
	
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level = intr_disable();
	int nice = thread_current()->nice;
	intr_set_level(old_level);
	return nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level = intr_disable();
	int return_load_avg = fp_to_n_rounding(mul_fp_n(load_avg,100));
	intr_set_level(old_level);
	return return_load_avg;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level = intr_disable();
	int recent_cpu = fp_to_n_rounding(mul_fp_n(thread_current()->recent_cpu,100));
	intr_set_level(old_level);
	return recent_cpu;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	// priority donation 관련
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&t->donation_list);

	// advanced scheduler
	t-> nice = NICE_DEFAULT;
	t-> recent_cpu = RECENT_CPU_DEFAULT;

#ifdef USERPROG
	// 2-3-fork 변수 초기화
	list_init(&t->child_list);
	sema_init(&t->_do_fork_sema, 0);
	sema_init(&t->wait_status_sema, 0);
	sema_init(&t->exit_child_sema, 0);

#endif

}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used bye the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

/* next_wakeup_tick 이 ticks 보다 크면 ticks로 update*/
void
update_next_wakeup_tick(int64_t ticks){
	if (next_wakeup_tick > ticks){
		next_wakeup_tick = ticks;
	}
}

/* next_wakeup_tick 반환 함수*/
int64_t
get_next_wakeup_tick(void){
	return next_wakeup_tick;
}

/* ticks까지 thread sleep 함수 */
void
sleep_thread(int64_t ticks){
	struct thread *curr;

	// inturrupt 금지, 이전 intr_level 저장
	enum intr_level old_level;
	old_level = intr_disable(); // inter_disable -> intr off 하고 이전 intr_state 반환

	curr = thread_current();
	ASSERT (curr != idle_thread); // idle thread -> sleep 되면 안됨

	curr -> wakeup_tick = ticks; // curr thread의 wakeup_tick에 ticks 저장
	update_next_wakeup_tick(ticks); //next_wakeup_tick update;

	list_push_back(&sleep_list, &curr->elem); //curr thread sleep_list에 삽입 후 스케쥴

	thread_block(); //이 스레드 블락

	intr_set_level(old_level); //intr_level 원래로 
}

// ticks시각에 일어날 thread 모두 awake 하는 함수
void
awake_thread(int64_t ticks){
	next_wakeup_tick = INT64_MAX;
	struct list_elem *e = list_begin(&sleep_list); //sleep_list 처음 elem 가져오기

	while (e!= list_end(&sleep_list)){
 		struct thread *t = list_entry(e, struct thread, elem);

		if (ticks >= t-> wakeup_tick){ // ticks 보다 thread의 wakeup_tick 작으면
			e = list_remove(&t -> elem); // sleep_list에서 제거후 다음 elem
			thread_unblock(t); // t는 unblock
		} else {
			e = list_next(e); // 다음 elem
			update_next_wakeup_tick(t -> wakeup_tick); // next_wakeup_tick을 t의 wakeup_tick으로 update
		}
	}
}

// x가 y보다 우선순위 높은지 반환
bool
thr_cmp_priority (const struct list_elem *x, const struct list_elem *y, void *aux UNUSED){  
	int priority_x = list_entry(x, struct thread, elem) -> 	priority;
	int priority_y = list_entry(y, struct thread, elem) -> 	priority;
	return (priority_x > priority_y);
}

// current thread와 ready_list의 높은 순위 thread 비교해서 yield 결정
void
test_highest_priority (void){ 
	if (!list_empty(&ready_list)){
		struct thread *highest_ready = list_entry(list_front(&ready_list), struct thread, elem);
		int curr_priority = thread_get_priority();
		if (curr_priority < highest_ready -> priority){
			thread_yield();
		}
	}
}

// donation
//donation_elem 이용하여 스레드의 우선도 비교 함수
bool
thr_donation_cmp_priority (const struct list_elem *x, const struct list_elem *y, void *aux UNUSED){ 
	int priority_x = list_entry(x, struct thread, donation_elem) -> priority;
	int priority_y = list_entry(y, struct thread, donation_elem) -> priority;
	return (priority_x > priority_y);
}

//priority donate (nested)
void
donate_priority(void) {
	struct thread *t = thread_current();
	
	for (int i = 0; i < 8; i++){ //depth = 8
		if (t->wait_on_lock == NULL) break; // wait_on_lock 더이상 없으면 for문 나오기
		struct thread *lock_holder = t->wait_on_lock->holder;
		lock_holder -> priority = t -> priority; // priority donate
		t = lock_holder; // lock_holder의 wait_on_lock 확인해서 donate 해주기
	}
	
}

// cur_thread의 donation_list에서 lock에 해당된 thread remove
void
remove_with_lock_in_donation_list(struct lock *lock){
	struct thread *curr = thread_current();

	struct list_elem *e;
	for (e = list_begin(&curr->donation_list); e != list_end(&curr->donation_list); e = list_next(e)){ // donation_list 에서 lock 풀린거 찾기
		struct thread *t = list_entry(e, struct thread, donation_elem);
		if (t->wait_on_lock == lock) // donations에서 이 lock 풀리기 기다리는 스레드면
			list_remove(e); // donations에서 e(donation_elem)를 삭제
	}

}

// donation 끝났으니 priority 재설정
void
update_priority (void) {
	struct thread *curr = thread_current();

	// 원래 priority로 되돌림
	curr -> priority = curr -> init_priority; 

	// donation_list 에 donation_elem 있으면
	if (!list_empty(&curr->donation_list)) {
		list_sort (&curr->donation_list, thr_donation_cmp_priority, NULL); // sort

		struct thread *high = list_entry(list_front(&curr->donation_list), struct thread, donation_elem); // 우선순위 높은것(첫번째) 뽑아서

		if (high -> priority > curr -> priority){ // high의 priority가 더 크면 donation 받음
			curr -> priority = high -> priority;
		}


	}

}

// advanced scheduler
void 
cal_priority (struct thread *t) {
	if (t == idle_thread) return;
	t->priority = fp_to_n(add_fp_n(div_fp_n(t->recent_cpu, -4), PRI_MAX - t->nice * 2));
}

void
cal_recent_cpu (struct thread *t) {
	if (t == idle_thread) return;
	t -> recent_cpu = add_fp_n(div_fp_fp(mul_fp_fp(mul_fp_n(load_avg, 2), t->recent_cpu), add_fp_n(mul_fp_n(load_avg, 2),1)), t->nice);
}

void
cal_load_avg (void) {
	int num_ready_threads = list_size(&ready_list);

	if (thread_current() != idle_thread)
		num_ready_threads++;
	
	load_avg = add_fp_fp(mul_fp_fp(div_fp_n(n_to_fp(59),60),load_avg), div_fp_n(n_to_fp(num_ready_threads),60));
}

//running thread의 recent_cpu 1tick마다 1씩 증가
void
running_thr_incr_recent_cpu (void){
	struct thread *curr = thread_current();
	if (curr != idle_thread){
		curr -> recent_cpu = add_fp_n(curr-> recent_cpu, 1);
	}
	
}

// all thread의 recent_cpu 재계산
void
recal_all_recent_cpu (void){
	struct list_elem *e;

	cal_recent_cpu(thread_current());

	for (e= list_begin(&ready_list); e != list_end(&ready_list); e=list_next(e)){
		struct thread *t = list_entry(e, struct thread, elem);
		cal_recent_cpu(t);
	}

	for (e= list_begin(&sleep_list); e != list_end(&sleep_list); e=list_next(e)){
		struct thread *t = list_entry(e, struct thread, elem);
		cal_recent_cpu(t);
	}
}

// all thread의 priority 재계산
void
recal_all_priority(void){
	struct list_elem *e;

	cal_priority(thread_current());

	for (e= list_begin(&ready_list); e != list_end(&ready_list); e=list_next(e)){
		struct thread *t = list_entry(e, struct thread, elem);
		cal_priority(t);
	}

	for (e= list_begin(&sleep_list); e != list_end(&sleep_list); e=list_next(e)){
		struct thread *t = list_entry(e, struct thread, elem);
		cal_priority(t);
	}
}