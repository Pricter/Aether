#include <kernel/userspace.h>
#include <kernel/cpu.h>
#include <kernel/strings.h>
#include <kernel/panic.h>
#include <kernel/heap.h>
#include <kernel/syscall_abi.h>

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

/* Shared-VM user threads are not supported, so a loaded process owns
 * exactly one thread. Both exit and exit_group complete that process. */
__attribute__((noreturn)) void arm64_process_exit(int status, bool fault) {
    thread_t *current = this_cpu()->current_thread;
    process_t *process = current->parent;

    if (current->clear_child_tid) {
        int zero = 0;
        arm64_user_copy(process, current->clear_child_tid, &zero, sizeof(zero), true, false);
    }

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

extern void arm64_resume_user(arm64_exception_frame_t *frame) __attribute__((noreturn));
extern void arm64_capture_simd(context_regs_t *context);

static void fork_thread_start(void) {
    thread_t *thread = this_cpu()->current_thread;
    arm64_resume_user(
        (arm64_exception_frame_t *)(thread->kstack_top - sizeof(arm64_exception_frame_t)));
}

/* Only fork-style clone is supported: no shared VM, alternate child stack, or
 * user threads. Reject all unsupported flags before allocating or publishing. */
long arm64_process_clone(arm64_exception_frame_t *frame) {
    unsigned long flags = frame->general_registers[0];
    const unsigned long child_settid = 0x01000000UL, child_cleartid = 0x00200000UL;

    if ((flags & 255) != 17 || (flags & ~(255UL | child_settid | child_cleartid)) ||
        frame->general_registers[1]) {
        return -EINVAL;
    }

    process_t *parent = this_cpu()->current_thread->parent;
    process_t *child = process_create(parent->name);

    if (!child) {
        return -ENOMEM;
    }

    if (!arm64_user_clone(parent, child)) {
        process_destroy(child);

        return -ENOMEM;
    }

    bool parent_interrupts = spinlock_acquire(&parent->lock);
    child->brk = parent->brk;
    child->minimum_break = parent->minimum_break;
    child->mmap_base = parent->mmap_base;
    memcpy(child->cwd_path, parent->cwd_path, sizeof(child->cwd_path));
    child->umask = parent->umask;
    child->cwd = parent->cwd;

    if (child->cwd) {
        inode_ref(child->cwd);
    }

    for (unsigned int index = 0; index < FD_TABLE_SIZE; ++index) {
        child->fd_table.files[index] = parent->fd_table.files[index];
        child->fd_table.cloexec[index] = parent->fd_table.cloexec[index];

        if (child->fd_table.files[index]) {
            file_ref(child->fd_table.files[index]);
        }
    }

    spinlock_release(&parent->lock, parent_interrupts);
    thread_t *thread = thread_create(child, fork_thread_start);

    if (!thread) {
        process_destroy(child);

        return -ENOMEM;
    }

    thread->tid = child->pid;
    thread->user_entry = frame->return_address;
    thread->ustack_top = frame->user_stack;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(thread->user_tls));
    arm64_capture_simd(&thread->context);
    arm64_exception_frame_t *copy = (arm64_exception_frame_t *)(thread->kstack_top - sizeof(*copy));
    memcpy(copy, frame, sizeof(*copy));
    copy->general_registers[0] = 0;
    copy->interrupted_stack = thread->kstack_top;

    /* Reserve the copied exception frame above the fresh trampoline's stack. */
    thread->context.stack_pointer = (uintptr_t)copy;
    uintptr_t child_tid = frame->general_registers[4];

    if ((flags & child_settid) &&
        !arm64_user_copy(child, child_tid, &child->pid, sizeof(child->pid), true, false)) {
        thread_destroy(thread);
        process_destroy(child);

        return -EFAULT;
    }

    thread->clear_child_tid = flags & child_cleartid ? child_tid : 0;
    pid_t identifier = child->pid;
    arm64_process_attach(child, parent);
    thread_ready(thread);

    return identifier;
}

/* Copy argv/envp into bounded kernel-owned storage before replacing mappings. */
struct exec_vectors {
    char path[PATH_MAX];
    const char *arguments[EXEC_VECTOR_MAXIMUM + 1];
    const char *environment[EXEC_VECTOR_MAXIMUM + 1];
    char strings[EXEC_STRING_BYTES];
    size_t used;
};

static errno_t exec_string(process_t *process, uintptr_t address, char *buffer, size_t capacity) {
    for (size_t index = 0; index < capacity; ++index) {
        if (address >= ARM64_USER_LIMIT || index >= ARM64_USER_LIMIT - address ||
            !arm64_user_copy(process, address + index, &buffer[index], 1, false, false)) {
            return -EFAULT;
        }

        if (!buffer[index]) {
            return EOK;
        }
    }

    return -E2BIG;
}

static errno_t exec_vector(
    process_t *process, uintptr_t address, const char **output, struct exec_vectors *vectors) {
    if (!address) {
        output[0] = NULL;

        return EOK;
    }

    for (size_t index = 0; index <= EXEC_VECTOR_MAXIMUM; ++index) {
        uintptr_t value;

        if (address >= ARM64_USER_LIMIT || index * sizeof(value) >= ARM64_USER_LIMIT - address ||
            !arm64_user_copy(
                process, address + index * sizeof(value), &value, sizeof(value), false, false)) {
            return -EFAULT;
        }

        if (!value) {
            output[index] = NULL;

            return EOK;
        }

        if (index == EXEC_VECTOR_MAXIMUM) {
            return -E2BIG;
        }

        char *string = vectors->strings + vectors->used;
        errno_t status =
            exec_string(process, value, string, sizeof(vectors->strings) - vectors->used);
        if (status < 0) {
            return status;
        }

        output[index] = string;
        vectors->used += strlen(string) + 1;
    }

    return -E2BIG;
}

long arm64_process_exec(arm64_exception_frame_t *frame) {
    thread_t *current = this_cpu()->current_thread;
    process_t *process = current->parent;
    struct exec_vectors *vectors = malloc(sizeof(*vectors));

    if (!vectors) {
        return -ENOMEM;
    }

    errno_t status =
        exec_string(process, frame->general_registers[0], vectors->path, sizeof(vectors->path));
    if (status == -E2BIG) {
        status = -ENAMETOOLONG;
    }

    if (status == EOK) {
        status = exec_vector(process, frame->general_registers[1], vectors->arguments, vectors);
    }

    if (status == EOK) {
        status = exec_vector(process, frame->general_registers[2], vectors->environment, vectors);
    }

    process_t *replacement = NULL;
    thread_t *prepared = NULL;

    if (status == EOK) {
        if (!vectors->arguments[0]) {
            vectors->arguments[0] = "";
        }

        status = arm64_elf_load_path(process, vectors->path, vectors->arguments,
            vectors->environment, &replacement, &prepared);
    }

    free(vectors);

    if (status < 0) {
        return status;
    }

    uintptr_t entry = prepared->user_entry, stack = prepared->ustack_top;
    thread_destroy(prepared);

    /* No other thread can observe this single-threaded process mid-commit.
     * IRQ masking prevents switching with a half-updated root. */
    bool saved_interrupts = irq_save();
    struct user_address_space *old_space = process->address_space;
    process->address_space = replacement->address_space;
    process->page_table_root = replacement->page_table_root;
    replacement->address_space = old_space;
    replacement->page_table_root = arm64_user_root(old_space);
    process->brk = replacement->brk;
    process->minimum_break = replacement->minimum_break;
    process->mmap_base = replacement->mmap_base;
    current->user_entry = entry;
    current->ustack_top = stack;
    current->clear_child_tid = 0;
    arm64_process_switch(current, current);
    current->user_tls = 0;
    __asm__ volatile("msr tpidr_el0, xzr");
    irq_restore(saved_interrupts);
    process_destroy(replacement);

    for (unsigned int index = 0; index < FD_TABLE_SIZE; ++index) {
        if (process->fd_table.cloexec[index]) {
            vfs_close(process, index);
        }
    }

    arm64_user_thread_start();
}
