#pragma once
#include <kernel/user.h>
#include <kernel/vfs.h>
#include <kernel/syscall.h>

/* Execution-layer operations: switch with IRQs masked, enter a fresh user
 * thread, or handle its borrowed synchronous exception frame. */
void arm64_process_switch(thread_t *outgoing, thread_t *incoming);
__attribute__((noreturn)) void arm64_user_thread_start(void);
void arm64_user_exception(arm64_exception_frame_t *frame);

/* Whole-process lifecycle: the parent owns a completed child until wait consumes
 * its status. No process is destroyed while a CPU or kernel stack still uses it. */
pid_t arm64_process_parent_pid(process_t *process);
void arm64_process_attach(process_t *process, process_t *parent);
void arm64_process_thread_reaped(process_t *process);
pid_t arm64_process_wait(process_t *parent, pid_t identifier, int *status, int options);
__attribute__((noreturn)) void arm64_process_exit(int status, bool fault);

/* Loader returns an unpublished, caller-owned process and CREATED thread. */
process_t *arm64_elf_load(const void *image, size_t length, const char *argument, process_t *parent,
    thread_t **thread_out);
void arm64_userspace_boot(frame_allocator_t *allocator);

/* Mount the shared root/devices once during boot, without validation workloads. */
bool arm64_filesystems_initialize(void);
