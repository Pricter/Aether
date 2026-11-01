#pragma once

/*
 * types.h - canonical kernel-wide type definitions
 *
 * All subsystems must use these types when working with identifiers,
 * addresses, or error codes so that physical vs virtual and kernel vs
 * userspace distinctions are explicit at the type level.
 *
 * Rule: never silently coerce between paddr_t and vaddr_t.
 *       never return a negative paddr_t.
 *       use errno_t for error returns, not raw int.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* -- Identity types ---------------------------------------------------- */

typedef int32_t pid_t;     /* process identifier; -1 = invalid            */
typedef int32_t tid_t;     /* thread identifier;  -1 = invalid            */
typedef uint32_t cpu_id_t; /* logical CPU identifier              */

/* -- Address types ----------------------------------------------------- */

typedef uintptr_t vaddr_t; /* kernel or user virtual address              */
typedef uintptr_t paddr_t; /* physical frame address (no implicit virtual offset)     */

/* -- Error type -------------------------------------------------------- */

/*
 * errno_t: signed return value convention.
 *   0        = success
 *   negative = -(POSIX errno), e.g. -ENOMEM
 *   positive = subsystem-defined success value
 */
typedef int errno_t;

#define EAGAIN 11       /* nonblocking operation would block */
#define ECHILD 10       /* no matching child process */
#define EXDEV 18        /* cross-filesystem operation */
#define ESPIPE 29       /* nonseekable stream */
#define EOK 0           /* success                                            */
#define EPERM 1         /* operation not permitted                            */
#define ENOENT 2        /* no such file or directory                          */
#define EBADF 9         /* bad file descriptor                                */
#define ESRCH 3         /* no such process                                    */
#define EINTR 4         /* interrupted system call                            */
#define E2BIG 7         /* argument list too long                              */
#define ENOEXEC 8       /* invalid executable format                          */
#define EACCES 13       /* permission denied                                  */
#define EFAULT 14       /* bad address                                        */
#define EBUSY 16        /* device or resource busy                            */
#define EINVAL 22       /* invalid argument                                   */
#define ENOMEM 12       /* out of memory                                      */
#define ENOSYS 38       /* function not implemented                           */
#define EDEADLK 35      /* resource deadlock would occur                      */
#define ENOTTY 25       /* inappropriate ioctl for device                     */
#define ENOTSUP 95      /* operation not supported                            */
#define EEXIST 17       /* file exists                                        */
#define ENOTDIR 20      /* not a directory                                    */
#define EISDIR 21       /* is a directory                                     */
#define EMFILE 24       /* too many open files                                */
#define ENOSPC 28       /* no space left on device                            */
#define EROFS 30        /* read-only filesystem                               */
#define ENAMETOOLONG 36 /* filename too long                              */
#define ENOTEMPTY 39    /* directory not empty                            */
#define EOVERFLOW 75    /* value too large                                */
#define EPIPE 32        /* broken pipe                                    */
#define ERANGE 34       /* result too large                               */

/* -- Path limit -------------------------------------------------------- */

#define PATH_MAX 512 /* maximum absolute path length including NUL      */

/* -- IRQ state --------------------------------------------------------- */

/*
 * irq_state_t: snapshot of the interrupt-enable flag at the time of
 * irq_save().  Passed back to irq_restore() to re-enable interrupts only
 * if they were enabled when the save occurred.
 *
 * Use irq_save() / irq_restore() for non-lock critical sections.
 * Use spinlock_acquire() / spinlock_release() when mutual exclusion is
 * also required - those calls embed irq_save/restore semantics.
 */
typedef bool irq_state_t;
