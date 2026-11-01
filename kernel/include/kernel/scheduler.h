#pragma once
#include <kernel/types.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>

/* A process owns an address space; a thread owns its kernel stack/context.
 * One scheduler-state lock protects every queue and lifecycle transition. No
 * lock crosses a physical switch. Only the incoming stack clears on_cpu and
 * publishes deferred wakes or dead threads for the CPU-0 reaper. */
typedef enum {
    THREAD_CREATED = 0,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_DEAD
} thread_state_t;

/* switch.S preserves x19..x30/SP and the entire FP/SIMD bank. Vectors separately
 * preserve caller-saved integer registers and exception return state. */
typedef struct {
    uintptr_t saved_registers[12];
    uintptr_t stack_pointer;
    uintptr_t reserved_alignment;
    uint64_t simd_registers[64];
    uint64_t floating_control;
    uint64_t floating_status;
} context_regs_t;

#define KSTACK_SIZE (16u * 1024u)
/* Return the top of four owned contiguous pages, or zero on allocation failure. */
uintptr_t kstack_alloc(void);

/* Release only an off-CPU thread's stack; never change shared kernel mappings. */
void kstack_free(uintptr_t stack_top);

struct file;
struct inode;
#define FD_TABLE_SIZE 256
/* Each installed slot owns a file reference; close-on-exec belongs to the slot. */
typedef struct {
    struct file *files[FD_TABLE_SIZE];
    uint8_t cloexec[FD_TABLE_SIZE];
} fd_table_t;

/* Queue membership and check/sleep publication use the scheduler-state lock. */
typedef struct {
    list_head_t waiters;
} wait_queue_t;

/* Initialize an unpublished wait queue without allocating or taking locks. */
static inline void wait_queue_init(wait_queue_t *queue) {
    list_head_init(&queue->waiters);
}

typedef struct thread {
    tid_t tid;
    struct process *parent;
    thread_state_t state;
    context_regs_t context;
    uintptr_t kstack_top, kstack_bottom, ustack_top;
    cpu_id_t owner_cpu;
    uintptr_t user_entry, user_tls, clear_child_tid;
    bool on_cpu;
    list_node_t list_node; /* ready/dead list; protected by scheduler state */
    list_node_t wq_node;   /* wait list; protected by scheduler state */
} thread_t;

#define PROT_NONE 0u
#define PROT_READ 1u
#define PROT_WRITE 2u
#define PROT_EXEC 4u
#define MMAP_BASE ((uintptr_t)0x01000000UL)
#define USTACK_TOP ((uintptr_t)0x07fff000UL)
#define USTACK_SIZE ((size_t)(128u * 1024u))

/* The process lock owns mapping metadata, thread count, CWD and descriptor
 * slots. The process-tree lock owns parent/children and completion state.
 * Wait retains zombie ownership until the final stack has been reaped. */
typedef struct process {
    pid_t pid;
    const char *name; /* borrowed name; caller retains its storage */
    struct user_address_space *address_space;
    unsigned long user_exits, user_faults, demand_faults, user_interrupts, user_switches;
    unsigned long last_fault_address, last_syndrome;
    long user_status;
    struct process *parent_process;
    list_head_t children;
    list_node_t child_node;
    wait_queue_t child_wait;
    unsigned long child_sequence;
    bool managed_process, process_completed;
    int wait_status;
    uintptr_t minimum_break;
    uint64_t *page_table_root;
    spinlock_t lock;
    uint32_t thread_count;
    uintptr_t mmap_base, brk;
    char cwd_path[PATH_MAX];
    struct inode *cwd;
    uint32_t umask;
    fd_table_t fd_table;
} process_t;
extern process_t kernel_process;

/* Initialize queues and idle contexts on CPU 0 before secondary startup. */
void scheduler_init(void);

/* Publish the single global reaper on CPU 0, exactly once. */
void scheduler_start_reaper(void);

/* Return a caller-owned CREATED thread. Returning from entry exits the thread. */
thread_t *thread_create(process_t *parent, void (*entry)(void));

/* Cancel only an unpublished CREATED thread; release its descriptor/stack. */
void thread_destroy(thread_t *thread);

/* Validate RX entry and writable aligned stack before constructing EL0 entry. */
thread_t *thread_create_user(process_t *process, uintptr_t entry, uintptr_t user_stack);

/* Transfer a CREATED thread to the current CPU's queue and scheduler ownership. */
void thread_ready(thread_t *thread);

/* Fix affinity before publication; all future wakes preserve the chosen CPU. */
void thread_ready_on(thread_t *thread, cpu_id_t cpu_identifier);

/* Enter the first ready/idle context with IRQs masked during publication. */
__attribute__((noreturn)) void scheduler_enter(void);

/* Yield without holding any lock or executing in interrupt context. */
void schedule(void);

/* Publish DEAD only after the subsequent physical stack handoff. */
__attribute__((noreturn)) void thread_exit(void);

/* Wake one queued thread; a still-on-CPU wake defers its enqueue until handoff. */
void wait_queue_wake_one(wait_queue_t *queue);

/* Wake all currently queued threads without allocating or blocking. */
void wait_queue_wake_all(wait_queue_t *queue);

/* Unconditional sleep; callers must arrange a producer that will wake them. */
void thread_block(wait_queue_t *queue);

/* Atomically check and sleep. Predicates cannot allocate, block, take locks or
 * enable IRQs. Producers publish condition data before issuing the wake. */
void thread_wait_until(wait_queue_t *queue, bool (*ready)(void *context), void *context);

/* Return a unique process identifier independent of thread/CPU affinity. */
pid_t scheduler_alloc_pid(void);

/* Create an unpublished process with a private user translation tree. */
process_t *process_create(const char *name);

/* Destroy only after all threads are cancelled/reaped and every CPU switched away. */
void process_destroy(process_t *process);

/* Reserve zero-on-demand anonymous pages. Explicit hints are exact; zero uses
 * the watermark allocator. Return zero on allocation/validation failure. */
uintptr_t process_mmap(process_t *process, uintptr_t hint, size_t length, uint32_t protection);

/* Remove aligned subranges; revocation completes on all CPUs before frame reuse. */
void process_munmap(process_t *process, uintptr_t address, size_t length);

/* Reserve the fixed user stack above an unmapped guard; return its top or zero. */
uintptr_t process_alloc_ustack(process_t *process);

/* Resolve readable anonymous backing and return its physical page, or zero.
 * Allowed in synchronous fault context, never IRQ context. */
uintptr_t process_ensure_page(process_t *process, uintptr_t address);

/* Change an existing whole range's permissions; reject writable executable pages. */
errno_t process_mprotect(process_t *process, uintptr_t address, size_t length, uint32_t protection);

/* IRQ hooks request scheduling; switching occurs only after EOI/depth teardown. */
void scheduler_timer_tick(void *context);
void scheduler_interrupt_exit(void);

/* Called from assembly on the incoming stack, with IRQs masked and no state lock. */
void scheduler_switch_complete(void);

/* Allocation-free diagnostic snapshots; waiter membership uses state locking. */
unsigned long scheduler_reaped_threads(void);
unsigned long scheduler_context_switches(void);
unsigned long scheduler_timer_switches(void);
unsigned long scheduler_idle_entries(void);
unsigned long scheduler_waiter_count(wait_queue_t *queue);
unsigned long scheduler_deferred_wakeups(void);
