#include <kernel/userspace.h>
#include <kernel/cpu.h>
#include <kernel/strings.h>
#include <kernel/panic.h>
#include <kernel/heap.h>

static spinlock_t process_tree_lock = SPINLOCK_ZERO;

/* Publication happens only after ELF/stack/descriptor preparation succeeds.
 * Parent lifetime is protected by adoption at completion under this same lock. */
void arm64_process_attach(process_t *process, process_t *parent) {
    bool interrupts_enabled = spinlock_acquire(&process_tree_lock);
    KERNEL_ASSERT(!process->managed_process);
    process->managed_process = true;
    process->parent_process = parent;
    list_push_back(&parent->children, &process->child_node);
    spinlock_release(&process_tree_lock, interrupts_enabled);
}

/* Called by the reaper after freeing the final thread stack and publishing its
 * accounting. Completion is later than physical TTBR/stack handoff on every CPU. */
void arm64_process_thread_reaped(process_t *process) {
    if (!process->managed_process) {
        return;
    }

    bool interrupts_enabled = spinlock_acquire(&process->lock);
    bool complete = !process->thread_count;
    spinlock_release(&process->lock, interrupts_enabled);

    if (!complete) {
        return;
    }

    fd_table_destroy(&process->fd_table);

    if (process->cwd) {
        inode_unref(process->cwd);
        process->cwd = NULL;
    }

    interrupts_enabled = spinlock_acquire(&process_tree_lock);

    /* An exiting parent cannot retain dangling child->parent pointers. */
    while (!list_empty(&process->children)) {
        process_t *child = list_entry(list_pop_front(&process->children), process_t, child_node);
        child->parent_process = &kernel_process;
        list_push_back(&kernel_process.children, &child->child_node);
        __atomic_fetch_add(&kernel_process.child_sequence, 1, __ATOMIC_RELEASE);
    }

    process->process_completed = true;

    process_t *parent = process->parent_process;
    __atomic_fetch_add(&parent->child_sequence, 1, __ATOMIC_RELEASE);

    /* Waking does not take a process lock; scheduler code never takes the tree
     * lock while holding its own state lock. No lock survives a context switch. */
    wait_queue_wake_all(&parent->child_wait);
    wait_queue_wake_all(&kernel_process.child_wait);
    spinlock_release(&process_tree_lock, interrupts_enabled);
}

/* The predicate is allocation-free and takes no lock under scheduler locking. */
struct child_condition {
    process_t *parent;
    unsigned long sequence;
};

static bool child_changed(void *context) {
    struct child_condition *condition = context;

    return __atomic_load_n(&condition->parent->child_sequence, __ATOMIC_ACQUIRE) !=
           condition->sequence;
}

/* Consume a zombie only after off-CPU completion. WNOHANG does not consume a
 * running child. Negative process groups other than -1 are not implemented. */
pid_t arm64_process_wait(process_t *parent, pid_t identifier, int *status, int options) {
    if ((options & ~1) || identifier == 0 || identifier < -1) {
        return -EINVAL;
    }

    for (;;) {
        bool interrupts_enabled = spinlock_acquire(&process_tree_lock);
        bool has_child = false;
        list_node_t *node;
        list_for_each(node, &parent->children) {
            process_t *child = list_entry(node, process_t, child_node);

            if (identifier != -1 && child->pid != identifier) {
                continue;
            }

            has_child = true;

            if (!child->process_completed) {
                continue;
            }

            list_remove(&child->child_node);

            pid_t result = child->pid;
            *status = child->wait_status;
            spinlock_release(&process_tree_lock, interrupts_enabled);
            process_destroy(child);

            return result;
        }

        struct child_condition condition = {
            parent, __atomic_load_n(&parent->child_sequence, __ATOMIC_RELAXED)};
        spinlock_release(&process_tree_lock, interrupts_enabled);

        if (!has_child) {
            return -ECHILD;
        }

        if (options & 1) {
            return 0;
        }

        thread_wait_until(&parent->child_wait, child_changed, &condition);
    }
}

/* User-created threads/clone are not supported yet, so a loaded process owns
 * exactly one thread. Both Linux exit and exit_group complete that process. */
__attribute__((noreturn)) void arm64_process_exit(int status, bool fault) {
    process_t *process = this_cpu()->current_thread->parent;
    bool interrupts_enabled = spinlock_acquire(&process->lock);
    process->wait_status = fault ? status & 127 : (status & 255) << 8;
    process->user_status = status;

    if (!fault) {
        __atomic_fetch_add(&process->user_exits, 1, __ATOMIC_RELEASE);
    }

    spinlock_release(&process->lock, interrupts_enabled);
    thread_exit();
}

/* Adoption can race getppid on another CPU. Snapshot the identifier while the
 * tree lock still prevents the previous parent from being detached and freed. */
pid_t arm64_process_parent_pid(process_t *process) {
    bool interrupts_enabled = spinlock_acquire(&process_tree_lock);
    pid_t identifier = process->parent_process ? process->parent_process->pid : 0;
    spinlock_release(&process_tree_lock, interrupts_enabled);

    return identifier;
}

/* Allocate an unpublished process and its owned address space. Name is borrowed
 * and must outlive the process. Heap allocation zeroes all initial state. */
process_t *process_create(const char *name) {
    process_t *process = malloc(sizeof(*process));

    if (!process) {
        return NULL;
    }

    process->address_space = arm64_user_space_create();

    if (!process->address_space) {
        free(process);

        return NULL;
    }

    process->page_table_root = arm64_user_root(process->address_space);
    process->name = name;
    process->pid = scheduler_alloc_pid();
    process->mmap_base = MMAP_BASE;
    list_head_init(&process->children);
    list_node_init(&process->child_node);
    wait_queue_init(&process->child_wait);
    process->cwd_path[0] = '/';
    process->umask = 0022;

    return process;
}

/* Caller owns an unpublished or collected process. Scheduler handoff must
 * finish on every CPU before its mappings and filesystem references are freed. */
void process_destroy(process_t *process) {
    if (!process) {
        return;
    }

    KERNEL_ASSERT(!process->thread_count);

    for (unsigned int cpu_index = 0; cpu_index < coreCount; ++cpu_index) {
        KERNEL_ASSERT(__atomic_load_n(
                          &cpu_core_local[cpu_index].active_process, __ATOMIC_ACQUIRE) != process);
    }

    arm64_user_space_destroy(process->address_space);
    fd_table_destroy(&process->fd_table);

    if (process->cwd) {
        inode_unref(process->cwd);
    }

    free(process);
}
