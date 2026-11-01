#pragma once
#include <kernel/scheduler.h>
#include <kernel/exception.h>
#include <kernel/frame_allocator.h>

#define ARM64_USER_MIN 0x10000UL
#define ARM64_USER_LIMIT 0x08000000UL
#define ARM64_USER_STACK 0x07fff000UL

/* Anonymous mappings are page aligned, fallible and W^X. Processes own all
 * backing frames; their last thread must be reaped before destruction. */
/* Initialize once on the boot CPU before any secondary or process can run. */
void arm64_user_initialize(void);
bool arm64_user_map(process_t *process, uintptr_t address, size_t length, unsigned int protection);
bool arm64_user_protect(
    process_t *process, uintptr_t address, size_t length, unsigned int protection);
bool arm64_user_unmap(process_t *process, uintptr_t address, size_t length);

/* Loader writes are permitted only before threads exist. Ordinary copies check
 * user permissions and resolve anonymous demand pages without dereferencing EL0. */
bool arm64_user_copy(process_t *process, uintptr_t address, void *buffer, size_t length,
    bool write_user, bool loader);
bool arm64_user_access(process_t *process, uintptr_t address, unsigned int access);
void arm64_user_validate(frame_allocator_t *allocator);

/* Normal-context allocation returns caller-owned, unpublished storage or NULL.
 * Destruction may free frames/heap storage and requires no active CPU users.
 * Root lookup borrows permanent kernel storage when space is NULL; otherwise
 * the returned root remains valid only while the address space remains alive. */
struct user_address_space *arm64_user_space_create(void);
void arm64_user_space_destroy(struct user_address_space *space);
uint64_t *arm64_user_root(const struct user_address_space *space);

/* Checked copy preflight may allocate demand pages under the process lock. */
bool arm64_user_range(process_t *process, uintptr_t address, size_t length, unsigned int access);
