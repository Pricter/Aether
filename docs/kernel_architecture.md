# Aether Kernel Architecture

This is the authoritative implementation reference for the supported AArch64
kernel. [README](../README.md) defines the design rules. Platform and early-boot
contracts are expanded in [Boot and platform](boot.md). AArch64 is the only implementation. The former x86 backend has been removed,
not retained as a build option.

## Subsystem map

| Concern | Implementation | Contract |
|---|---|---|
| Entry / DTB | `kernel.c`, `boot/entry.S`, `boot/memory.c` | EL1, valid DTB; reserve all live boot objects |
| RAM / heap | `boot/frames.c`, `memory/pages.c`, `memory/slab.c` | Fallible caller-owned frames; no IRQ allocations |
| Translation | `memory/mmu.c`, `memory/user.c` | Privileged shared kernel branches, private user branches |
| CPU / SMP | `sys/cpu.c`, `sys/smp.c`, `sys/secondary.S` | Permanent CPU storage; PSCI startup; fixed affinity |
| IRQ / time | `sys/interrupts.c`, `time/timer.c` | EOI before scheduling, CPU-local timer state |
| Threads | `sys/scheduler.c`, `sys/switch.S` | No scheduler lock crosses a stack switch |
| Processes | `sys/process.c`, `sys/user.c` | Wait owns zombie collection after off-CPU reaping |
| ELF | `sys/elf_loader.c` | Validated static AArch64 images; unpublished rollback |
| ABI | `sys/syscalls.c`, `include/kernel/linux_abi.h` | Linux ARM numbers/layouts; checked-copy boundary |
| Files | `fs/vfs.c`, `fs/tmpfs.c`, `fs/devfs.c`, `fs/pipe.c`, `fs/init.c` | Refcounted inodes/open descriptions; process-owned slots |
| Diagnostics | `debug/exception.c`, `debug/panic.c` | Allocation-free fatal paths; SMP ownership |
| Normal startup | `sys/startup.c`, `../userspace/init/init.c` | Start CPUs/filesystems and one init; no validation workers |
| Tests | `test/`, `../userspace/tests/init.c` | Explicit kernel/musl regressions |

Paths in this table are relative to `kernel/`. Kernel subsystem interfaces live under `kernel/include/kernel/`; the platform
console interface lives under `kernel/include/platform/`. No architecture wrapper
or alternate backend is required. `kernel/build.mk` is the canonical build definition.

## Boot and address-space layout

Boot follows the ARM64 Image protocol and receives the DTB in x0. The kernel is
linked at physical/virtual `0x40080000`; RAM starts at `0x40000000`. Four-level
4 KiB page tables identity-map discovered RAM and required UART/GIC MMIO. Kernel
text is privileged RX, rodata privileged R/NX and other RAM privileged RW/NX.
Reservations prevent allocation, rather than removing mappings.

User mappings occupy `[0x10000, 0x08000000)`, below the GIC device range. A process
privately owns the first three ancestors of that window, its user leaf tables,
reservation metadata, and anonymous physical frames. Other kernel table branches
are shared and immutable. Stacks end at `0x07fff000`, reserve 128 KiB and have an
unmapped page below. ELF loading moves the anonymous mmap watermark above the
image when necessary. This is a transitional layout, not a higher-half kernel.

PTEs always set PXN on user pages, set UXN except for executable mappings and reject
W+X. Write/execute imply read. Demand allocation zeroes frames; PROT_NONE retains
backing ownership. Empty leaf tables remain owned until process destruction.
Permission revocation uses break-before-make and `TLBI VAAE1IS` with completion
barriers before physical reuse. TTBR0 switches currently flush ASID zero locally.
Executable publication cleans data to PoU and invalidates instruction caches.

## Scheduler, interrupts and FP/SIMD

Each CPU owns a run queue, idle thread, current/previous context, timer and GIC
private interface. TPIDR_EL1 selects CPU storage. The sole scheduler-state lock
protects run/wait/dead lists and lifecycle transitions. Ready publication fixes
affinity; remote wakeups request rescheduling by SGI. No load balancing or hotplug
is implemented.

An outgoing context remains `on_cpu` until assembly saves its state, changes SP,
and calls `scheduler_switch_complete` on the incoming stack. A wake before that
point marks it ready but defers queue publication. Dead contexts become visible
to the CPU-0 reaper only after that handoff. No scheduler lock survives a switch.

Exception frames preserve all integer registers, ELR/SPSR and separate SP_EL0 and
kernel-stack values. IRQ dispatch finishes GIC EOI and clears interrupt depth
before tail preemption. Synchronous user syscalls enable IRQs on their owned
kernel stacks and mask them again before vector return. Kernel C remains built
with `-mgeneral-regs-only`; assembly context switches preserve all q0–q31, FPCR,
and FPSR. CPACR_EL1 enables user FP/SIMD on every CPU. Initial entry clears that
bank and integer registers before ERET. TPIDR_EL0 is saved per thread for musl TLS.

## Process ownership, completion and waiting

`arm64_elf_load` returns an unpublished process and CREATED thread owned by its
caller. The caller must attach the process to a live parent before making its
thread ready. Cancelling an unpublished thread precedes destroying its process.
A loaded process has one user thread; user clone/fork are not implemented.

The process-tree lock protects parent pointers, child lists and completion state.
It is never acquired under the scheduler-state lock. Wait releases it before
sleeping; its scheduler predicate only reads an atomic parent sequence. Completion
increments that sequence before waking waiters, closing the lost-wakeup window.
getppid snapshots its result under the tree lock so adoption cannot race parent
reclamation.

The reaper frees the final kernel stack/descriptor, publishes thread accounting,
then closes process descriptor references and releases its CWD reference. Only
then does it mark the process completed and wake its parent. Address-space pages
remain zombie-owned until wait removes the child and destroys it. Destruction
asserts no CPU still advertises that process as active. Wait consumes status once;
WNOHANG returns zero for an unfinished matching child, and no matching child gives
-ECHILD. Exiting parents transfer children to the kernel process, including already
completed children. Kernel-owned children still require a kernel wait caller.

Normal exit encodes `(status & 255) << 8`; fatal EL0 faults encode signal-style
status 11 without signal-handler delivery. exit and exit_group coincide under the
current one-user-thread model. They are not a multithreaded group-exit implementation.

## ELF and initial stack

Only little-endian ELF64 ET_EXEC, EM_AARCH64 static binaries are supported. Loader
validation bounds program tables and file slices, checks file/memory sizes,
alignment/congruence, address limits, executable entry and mapped readable program
headers, and rejects page-overlapping LOAD segments, W+X, dynamic interpreters,
PT_DYNAMIC and executable stacks. TLS initialized bytes must be file-backed by a
readable LOAD; pure TBSS may be outside LOAD storage. TLS size/alignment are bounded.
Failures destroy all unpublished mappings and descriptor references.

LOAD pages receive final user permissions before publication; loader copies use
privileged physical aliases. Anonymous zero-fill supplies BSS. The aligned initial
stack provides argc/argv, terminated envp and auxv, including AT_PHDR/PHENT/PHNUM,
PAGESZ, ENTRY, identities, SECURE, EXECFN and RANDOM. Musl discovers TLS via these
headers and initializes TPIDR_EL0 itself. AT_RANDOM currently contains a predictable
counter/PID-derived bootstrap nonce; there is no secure entropy source or getrandom
implementation.

Parents inherit referenced CWD/open descriptions; CLOEXEC slots are excluded when
loading the child image. Root init gets independent terminal descriptors 0–2.
There is no executable filesystem lookup or execve syscall yet: the boot coordinator
loads the embedded static ELF through the same loader API.

## Filesystem ownership and concurrency

The shared VFS mounts tmpfs at `/` and devfs at `/dev`; `/tmp` is created during
userspace startup. Filesystem code depends on the heap and the canonical VFS interfaces.
The ARM tty uses length-delimited PL011 writes; normal writes serialize output,
while emergency panic output remains lock-free. TTY input currently reports EOF.

A descriptor slot owns one reference to an open description; each description
owns one inode reference. Duplication/inheritance share positions and status flags.
Descriptor close removes the slot under the process lock and drops the reference
outside that lock. I/O borrows a file reference before releasing the process lock.
Mutable status flags use atomic snapshots; F_SETFL also serializes updates with
the file lock. Close-on-exec belongs to the individual descriptor slot.

Regular I/O and directory enumeration lock file position before inode content.
Creation snapshots umask under the process lock. Positioned callbacks leave
the shared position unchanged. Growth checks arithmetic and clears holes after a
prior truncate. Nonblocking pipes return EAGAIN instead of polling; blocking paths
yield without holding pipe locks. EOF observes an empty buffer and the final write-description
reference under the same pipe lock, and absent readers produce EPIPE. Pipe writes/writev are not yet a full
Linux PIPE_BUF atomicity implementation.

Tmpfs rename prepares allocation first and orders both directory locks by address;
same-entry rename is a no-op and destination replacement retains cleanup ownership.
Cross-backend rename gives EXDEV. Directory rename is rejected pending cycle checks;
cross-mount identity for multiple tmpfs instances is not represented yet.

## Linux AArch64 ABI

SVC uses x8 for number, x0–x5 for arguments and signed x0 results. The syscall layer
copies paths, metadata and byte buffers through checked process mappings; no raw
user pointer reaches a filesystem callback. Read buffers are preflighted before
consuming input. Copies revalidate mappings and may return a successful prefix.
The ARM stat structure is 128 bytes, with compile-time size/offset checks.

| Group | Implemented numbers / operations |
|---|---|
| Descriptors / terminal | 23 dup, 24 dup3, 25 fcntl, 29 supported tty ioctls, 57 close |
| Paths | 17 getcwd, 34 mkdirat, 35 unlinkat/rmdir, 38 renameat, 45 truncate, 46 ftruncate, 48 faccessat, 49 chdir, 56 openat |
| Data / directories | 59 pipe2, 61 getdents64, 62 lseek, 63/64 read/write, 65/66 readv/writev, 67/68 pread64/pwrite64, 79 newfstatat, 80 fstat |
| Memory | 214 brk, 215 munmap, 222 anonymous private mmap, 226 mprotect |
| Completion | 93 exit, 94 exit_group, 260 wait4 (no rusage) |
| Identity | 160 uname, 166 umask, 172/173 getpid/getppid, 174–177 root credential IDs, 178 gettid |
| Time / scheduling | 101 nanosleep, 113 clock_gettime, 115 clock_nanosleep, 124 sched_yield |
| Bootstrap support | 96 set_tid_address identity only, 261 read-only stack/descriptor prlimit64, 81 sync, 82/83 fsync/fdatasync |

Unsupported operations return negative errno, usually ENOSYS. Signals, futexes,
robust lists, clone/fork/execve, rusage, a credential/permission model and dynamic
linking are not implemented. Absolute paths and AT_FDCWD are supported; other
relative directory descriptors are not. Memory mappings are anonymous/private,
not file-backed/shared. User mapping placement is a monotonic watermark and can
leave gaps on failure. Realtime currently reports monotonic uptime; there is no RTC
wall-clock source. Sleep yields rather than using a timer wait queue. The bootstrap
set_tid_address handler does not implement clear-child-TID wakeups.

## Verification and build policy

`make` and direct `make -C kernel` build normal ARM outputs in `build/kernel/`.
`make run` initializes the machine and starts one normal musl init; it runs no
allocator, interrupt, scheduler, ELF or userspace regression workloads. `make test`
and `check-*` targets recursively select `KERNEL_SELF_TESTS=1` and use the separate
`build/tests/` directory. Their kernel includes validation workers, and
their init ELF comes from `userspace/tests/init.c`. Test builds cannot overwrite
the normal image. `make test` runs only the required ARM/native suite. Zig cross-compiles a static musl ELF and
keeps its caches inside `build/`. The checked boot matrix covers 1/2/4/8 CPUs and
128/256/512 MiB. Success requires the existing kernel/MMU/SMP/EL0 markers and the
new ELF/shared-filesystem/musl/wait/adoption/reclamation markers, with exact final
thread counts. Two musl waves per CPU check post-warmup frame equality; malformed
ELFs and bad user pointers must fail without a kernel panic.

The x86 backend, compatibility structs/IRQ branches, obsolete kernel tests,
Limine/ACPI/APIC/HPET code, and legacy userspace/build utilities have been removed.
The scheduler header models the actual ARM state lock and page reservations;
it no longer carries unused per-thread/queue locks, thread lists or VMAs for a
retired backend. Native DTB/frame/memory-operation fixtures remain host-compiled tests rather than
an alternate kernel target.

## Source formatting and constants

`make format` applies `.clang-format` to all first-party kernel/userspace C and
headers, plus the first-party freestanding string/memory declarations.
`make check-format` verifies the same set without modifying files.
Install clang-format (LLVM), or set `CLANG_FORMAT` to its executable. Upstream
printf and imported compiler/integer/ELF compatibility headers retain their existing formatting.
Assembly, Python and build files are maintained in their native syntax.

Use a single blank line between logical steps within a function: validation,
resource acquisition, initialization, work, and cleanup. Separate control-flow
blocks from surrounding work and keep adjacent functions and type declarations
visually distinct. Keep related assignments and short operation sequences
together; do not add a blank line after every statement. Clang-format preserves
these section breaks, but choosing them requires a readability review.

Use descriptive identifiers, named ABI/register fields and short function
contracts. `linux_abi.h` owns implemented Linux syscall numbers and shared terminal
wire constants; `arm64_mmu.h` owns shared translation geometry and PTE fields.
Tests may retain literal input/expected ABI values to check the implementation
independently. Architectural register names and standard ABI field names retain
their conventional spelling.

`memory/user.c` owns reservation metadata, demand frames and private tables. It
has no filesystem, scheduler-call or exception-dispatch dependency. Process
creation/destruction belong to `sys/process.c`; TTBR/TLS switching, EL0 entry and
exception handling belong to `sys/user.c`. Address-space roots are borrowed;
only unpublished or fully off-CPU spaces may be destroyed.
