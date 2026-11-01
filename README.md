# Aether

Aether is a monolithic, internally modular **AArch64 SMP operating system**.
AArch64 is the sole architecture. The former x86_64 implementation, bootloader
assets and build/test backends have been removed; their history remains in Git.

The current target is QEMU `virt` with Cortex-A72 CPUs and GICv2. It boots one static
musl-based init through the Linux AArch64 ABI, with private address spaces, shared
VFS/tmpfs/devfs/pipes, TLS/SIMD preservation, and parent/child status collection.
This is initial userspace, not a shell or a complete Linux implementation.

The design rules below apply to all new work. The authoritative implementation
contracts are in [Kernel architecture](docs/kernel_architecture.md); boot/MMU/SMP
hardware details are in [Boot and platform](docs/boot.md).

## 1. Design Philosophy

Aether is guided by a small set of principles that take precedence over implementation convenience, performance, or familiarity. These are not aspirational - they are enforced design constraints.

**Explicitness over implicit behavior.**
Functions must not silently allocate memory, acquire locks, block execution, or trigger scheduling unless explicitly documented. Every API must communicate its ownership semantics, locking expectations, execution context requirements, failure modes, and lifetime guarantees at the call site or in the header contract. Hidden side effects are design violations.

**Correctness before optimization.**
The kernel prefers correct, readable, and debuggable code over clever or premature optimizations. Performance work is acceptable only within a subsystem once its correctness is established and its behavior is observable. Global optimizations that obscure ownership or control flow are rejected.

**Determinism over emergence.**
Concurrency is designed explicitly, not incidental. Synchronization paths, interrupt behavior, scheduling decisions, and locking sequences must be predictable and analyzable. Behavior that emerges from uncontrolled interaction between subsystems is a defect.

**Illegal states must be hard to represent.**
Subsystem APIs should be designed so that valid use is the natural path and misuse requires active effort. When illegal states occur, they must be easy to detect. Invariants must be enforceable and testable.

**Debuggability is load-bearing.**
Diagnostics, testing, tracing, and validation are not auxiliary systems. They are core architectural components built alongside the subsystems they observe. A subsystem that cannot be diagnosed or tested is considered incomplete.

**Clarity of design over cleverness of implementation.**
Small, composable abstractions are preferred over large generic frameworks. A function that does one thing correctly is preferred over a function that handles many cases through layered indirection.

---

## 2. Architecture Overview

| Layer | Current implementation |
|---|---|
| Platform | ARM64 Image/DTB boot, PL011 console, PSCI, GICv2, physical timer |
| Memory | Fallible frame allocator, shared slab heap, 4 KiB tables, demand paging |
| Execution | Fixed-affinity preemptive SMP threads on up to eight CPUs |
| Isolation | Private EL0 mappings; privileged shared kernel identity map |
| ABI | Linux AArch64 syscall numbers and wire layouts, checked user copies |
| Files | Shared VFS, tmpfs root, devfs terminal/null/zero, anonymous pipes |
| Programs | Static AArch64 ELF, argc/argv/envp/auxv, musl TLS and allocation |
| Lifetime | Kernel-created children, exit status, wait/WNOHANG, orphan adoption |

User mappings occupy `[0x10000, 0x08000000)`. Kernel RAM remains identity-mapped
and privileged. This transitional layout is limited to 128 MiB of user virtual
addresses; a higher-half layout and ASID allocation remain future work.

## 3. Subsystem Rules

Every subsystem in Aether must satisfy these rules. They apply equally to new subsystems and modifications to existing ones.

**3.1 Clear responsibility.**
A subsystem owns one concern. If a change requires modifying two subsystems' internals simultaneously for a single logical operation, one of them has too broad or too narrow a scope.

**3.2 Stable interface.**
A subsystem's public interface is defined by its header(s) under `kernel/include/kernel/`. Internal implementation files must not be included directly by other subsystems. Internal helpers must not appear in public headers.

**3.3 Explicit ownership at the interface boundary.**
Every pointer, handle, or resource crossing a subsystem boundary must have documented ownership semantics: who allocated it, who frees it, and when it becomes invalid.

**3.4 No upward dependencies.**
Lower subsystems must not call into higher ones. A memory allocator must not call the scheduler. A timer driver must not call into the VFS. Upward coupling must be resolved through callbacks, event queues, or inversion of control - never by adding a direct include.

**3.5 Testability.**
Every subsystem must expose behavior that can be validated by the kernel test framework without running the full system. Subsystems that cannot be tested in isolation contain hidden coupling.

**3.6 Documented invariants.**
Each subsystem must define what it guarantees, what preconditions it requires, and what states it considers impossible. These invariants must be enforced with assertions or stated explicitly in the header.

---

## 4. Concurrency and Synchronization

Concurrency in Aether is designed explicitly. Every piece of shared state must have a clearly defined synchronization owner before it is introduced.

### Hard Rules

These are invariants. Violation is a kernel bug, not a tradeoff.

1. **No sleeping while holding a spinlock.** Any code path that may block, yield, or call into the scheduler must not hold a spinlock. If a subsystem needs to sleep while holding a resource lock, it requires a mutex or semaphore, not a spinlock.

2. **No blocking in interrupt context.** Interrupt handlers and code running with interrupts disabled must not block, allocate memory from a sleeping allocator, or call any function that may sleep.

3. **Lock ordering must be globally consistent.** When multiple locks must be held simultaneously, they must always be acquired in the same order throughout the entire kernel. The lock ordering must be documented. Violations cause deadlocks that are difficult to reproduce.

4. **Critical sections must be minimal.** Spinlocks must be held for the shortest possible time. Work that does not require mutual exclusion must be moved outside the locked region.

5. **All shared state must have a synchronization owner.** Every global or shared data structure must have a single designated lock, atomic operation, or access rule that protects it. Implicit or ad-hoc protection is not acceptable.

### Interrupt Safety

Functions must be annotated with their interrupt-safety expectations. The following contexts are distinct and must not be confused:

- **Normal context** - interrupts enabled, can sleep, can acquire spinlocks.
- **Spinlock context** - interrupts disabled (by spinlock acquire), cannot sleep.
- **Interrupt context** - running inside an interrupt handler, cannot sleep, cannot acquire sleeping locks.

The `spinlock_acquire()` primitive saves and disables interrupts on entry and restores them on `spinlock_release()`. This is the correct mechanism for short critical sections that must be safe across both normal and interrupt context.

### SMP

Per-core state is accessed through `this_cpu()`: TPIDR_EL1 on ARM. Global shared state must be synchronized. Kernel code must not assume it runs on a specific core unless it has explicitly pinned itself.

---

## 5. Memory Management

Memory management in Aether follows strict ownership rules. Every allocation has a defined owner, a defined lifetime, and a defined cleanup path.

### Layers

| Layer | Mechanism | Granularity | Ownership |
|-------|-----------|-------------|-----------|
| Physical | PMM bitmap | 4 KiB pages | Explicit: caller frees |
| Virtual | VMM page tables | 4 KiB pages | Tied to address space |
| Kernel heap | Slab allocator | 8 B - 2 KiB + large | Explicit: caller frees |

`malloc()` / `free()` are the general kernel heap interface. They are backed by the slab allocator for small objects and use contiguous frames through the shared page backend for large allocations.

### Ownership Rules

1. **Allocations have exactly one owner.** When ownership is transferred, the transfer must be explicit and documented at the call site.

2. **Shared objects use reference counting.** Any object that may be accessed from multiple contexts simultaneously and has a non-trivial lifetime must use reference counting. The object must not be freed until its reference count reaches zero.

3. **Hidden allocations are design violations.** A function that allocates memory internally and returns a pointer to it - without documenting this in its interface - creates implicit ownership. Document it, or refactor to let the caller provide the buffer.

4. **Every allocation has a cleanup path.** Allocations made during subsystem initialization must have a corresponding teardown path, even if teardown is not currently exercised. Designing without a cleanup path creates permanent resource leaks.

5. **Live boot objects remain reserved.** The kernel image, stacks, DTB and frame metadata must stay outside allocatable RAM until their owners explicitly release them.

---

## 6. Error Handling

Aether distinguishes between three categories of failure. Each has a distinct response and must not be conflated with the others.

### Programmer / Invariant Failures -> Panic

These are states that should never occur if the kernel is correct. When they occur, continued execution would produce undefined or corrupted behavior. The correct response is an immediate kernel panic with full diagnostics.

Use `KERNEL_ASSERT(cond)`, `KERNEL_BUG(msg)`, or `SUBSYS_PANIC(subsys, msg)`.

Examples: use-after-free, null pointer dereference in a path that guarantees non-null, violation of a lock ordering invariant, memory corruption detected by a slab sentinel.

### Recoverable Operational Failures -> Return Values

These are expected failure conditions that occur during normal operation. The caller must be able to detect and handle them without crashing the system.

Use return values (`NULL`, negative error codes, `bool` success flags). Subsystem APIs must document all possible failure modes.

Examples: out of memory for a user allocation, file not found, invalid syscall arguments, device not present.

### Hardware / Transient Failures -> Local Recovery

These are failures caused by hardware conditions or transient state. They should be handled as close to the hardware as possible, without propagating upward unless the condition is unrecoverable.

Examples: ECC-correctable memory errors, transient I/O timeouts, spurious interrupts.

### Panic Policy

A kernel panic must only occur when:

- A kernel invariant is demonstrably violated.
- The kernel's internal state is provably corrupted and continued execution would cause harm.
- A hardware condition makes continued operation impossible.

Normal operational failures (OOM for user memory, invalid syscall arguments, bad user pointers) must never directly produce a kernel panic. The kernel must return an error and recover.

---

## 7. Execution Model

The boot CPU discovers memory/CPUs through the DTB and starts secondaries with
PSCI. Every CPU owns a fixed run queue, idle thread, GIC private interface, and
physical timer. TPIDR_EL1 identifies CPU storage; TPIDR_EL0 belongs to userspace
TLS. Integer and FP/SIMD state survive preemption.

One scheduler-state lock protects lifecycle and queue membership. No lock crosses
a physical stack switch. The incoming stack publishes the previous context as
off-CPU before it can run elsewhere or be reclaimed. A CPU-0 reaper frees thread
stacks/descriptors and completes processes only after their last thread is gone.

Loaded processes currently have one user thread. The kernel can create a child
with inherited open descriptions; wait consumes a completed child's status and
address space. User `fork`, `clone`, `execve`, signals and pthreads are not yet
implemented. Automatic load balancing and CPU hotplug are also future work.

## 8. Observability and Testing

Normal boot initializes the system and launches one init, without running tests.
`make test` builds separate images in `build/tests/` to validate scheduling,
memory permissions, remote TLB revocation and real musl programs. Two test-only
parent/child waves run on every CPU, checking stable frame counts after warmup. Separate negative images cover fatal kernel paths.
Native parser, allocator and memory-operation tests complement the VM checks. Fatal diagnostics retain
allocation-free, lock-free serial output with atomic SMP ownership.

## 9. Userspace ABI

The compatibility target is **Linux AArch64**, using `SVC #0`, x8 for the syscall
number, x0–x5 for arguments and x0 for the result. Errors are negative Linux errno
values. The kernel exposes the Linux AArch64 layouts, including 128-byte `stat`.

Implemented groups include file/descriptor I/O, positioned/vector I/O, directory
entries, path operations, pipes and nonblocking flags, anonymous memory,
identity/time, TLS support, exit and child waiting. Unsupported syscalls return
`-ENOSYS`; unsupported signal operations do not pretend to install handlers.
See [ABI and limitations](docs/kernel_architecture.md#linux-aarch64-abi).

## 10. Next Milestones

- User-driven process creation and image replacement: fork/clone/execve.
- Signals, cancellation, futexes and pthread lifecycle.
- Higher-half kernel mapping, larger user address spaces and ASIDs.
- Storage/persistence, terminal input, and an interactive shell.
- A selected physical ARM board's boot, interrupt and device drivers.

## 11. Building and Running

Requirements: Clang with AArch64 support, LLVM LLD, QEMU AArch64, Python 3, and Zig
with bundled musl for cross-building userspace. Zig caches stay under `build/`.
Set `CC`, `LD`, `QEMU`, or `ZIG` to override tools. `CC` defaults to Clang targeting
`aarch64-none-elf`; the linker must support AArch64 ELF.

```sh
make                         # normal ARM Image, symbols and static musl init
make run                     # QEMU, four CPUs
make run CPUS=8
make test                    # full required ARM + native regression suite
make check-arm64-user        # userspace/kernel matrix: 1/2/4/8 CPUs
make check-fs-syscalls        # alias for the ARM userspace matrix
make userspace               # build only the ARM init ELF
make clean                   # remove normal/test ARM outputs, retain compiler caches
```

Normal outputs are `build/kernel/Image`, `kernel.elf`, and `init.elf`. A normal
boot prints `Aether init: ARM64 userspace ready.` once, then `Init exited. System
idle.` Tests never overwrite these outputs: they have their own kernel and musl
regression image under `build/tests/`. The interactive console uses a
132-column × 40-line QEMU window with zoom-to-fit.

## 12. Source Layout

ARM is integrated directly into the kernel rather than maintained as a side port:

| Path | Responsibility |
|---|---|
| `kernel/kernel.c`, `kernel/boot/` | Entry, device-tree discovery, boot reservations |
| `kernel/sys/` | CPUs, interrupts, SMP, scheduling, syscalls, ELF and startup |
| `kernel/memory/` | Frames, page tables, user mappings, stacks and heap |
| `kernel/debug/` | Fatal exception and panic reporting |
| `kernel/time/` | Architectural timer and monotonic clock |
| `kernel/fs/` | VFS, tmpfs, devfs, pipes and mount initialization |
| `kernel/platform/qemu-virt/` | QEMU-specific PL011 serial console |
| `kernel/include/kernel/` | Canonical kernel interfaces |
| `kernel/test/`, `userspace/tests/` | Explicit self-test workloads |
| `userspace/init/` | The single normal static musl init |

`kernel/build.mk` owns the source lists and normal/test build isolation. Direct
`make -C kernel` and `make -C userspace/init` forward to the root build. Hardware
functions retain ARM-specific names where that identifies their ISA contract;
there are no alternate-architecture branches or legacy build targets.

For source debugging, run `make run QEMU_DEBUG="-S -s"`, then attach an
AArch64-capable GDB using `util/debug.sh` (override its `GDB` environment variable
when needed). Symbols are in `build/kernel/kernel.elf`.

## Code style

Run `make format` before submitting C/header changes and `make check-format` to
verify formatting. These commands use `.clang-format`; set `CLANG_FORMAT` if LLVM
is not on your PATH. Use descriptive variable names, named ABI/hardware constants,
and comments explaining function contracts and non-obvious ownership or ordering.
See [source formatting and constants](docs/kernel_architecture.md#source-formatting-and-constants)
for the precise scope and subsystem boundaries.
