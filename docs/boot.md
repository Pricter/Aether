# Boot and QEMU Platform

Aether's supported/default ARM target boots QEMU `virt` with Cortex-A72 CPUs and GICv2. It now
supports CPU discovery, PSCI startup on up to eight CPUs, shared kernel memory,
per-CPU timers, preemptive kernel/user threads, private process address spaces,
and EL0 exception handling. Static ELF loading and the Linux AArch64 syscall layer run real musl init
programs against the shared VFS/tmpfs/devfs/pipes. Automatic load balancing and
physical-board support remain future work. ARM is the sole kernel architecture.

## Build and run

Requirements: Clang with AArch64 support, LLVM LLD, QEMU, Python 3, and Zig
with bundled musl for the static ARM init. Compiler caches stay under `build/`.
The build finds `ld.lld` on PATH or through Homebrew `lld@22`; override with
`LD=/absolute/path/to/ld.lld`. These tools are installed on this Mac.

```sh
make                            # normal ARM boot; no self-tests
make test                       # required ARM/native checks only
make arm64
make run-arm64                   # four CPUs by default
make run-arm64 CPUS=8
make check-arm64                 # single-CPU regression
make check-arm64-user            # EL0 + mapping/revocation/reaping, 1/2/4/8 CPUs
make check-arm64-smp             # 2/4/8 CPUs, 128/256/512 MiB
make check-arm64-handoff         # forced before-save wake race, 2/4 CPUs
make check-arm64-smp-panic       # secondary panic owner and peer-stop check
make check-arm64-scheduler       # normal scheduling + IRQ/held-lock misuse
make check-arm64-runtime         # heap + timer + misuse checks
make check-arm64-memory          # parser tests and RAM-size regressions
make check-arm64-exceptions
make check-arm64-mmu
```

Boot `build/kernel/Image`; `kernel.elf` retains debugging symbols. The Image
header selects QEMU's arm64 boot protocol so the DTB arrives in x0. Direct ELF
boot does not satisfy this target's DTB contract. Ctrl+C stops an interactive
run. Checks bound emulator runtime and terminate/reap every test VM.

Normal startup prints `Aether init: ARM64 userspace ready.` once and returns to
idle. Explicit checks use separate outputs in `build/tests/`. Their
success output includes `ARM64 scheduler passed`; multicore checks also require
`ARM64 SMP passed`, exact online CPU count, and timer/IPI statistics per CPU.
The coordinator then exits and is reaped, leaving per-CPU idle threads and one
blocked global reaper. The normal scheduler callback remains installed, and
all online CPUs keep receiving 200 Hz timer interrupts. Host checks continue
observing after success markers and reject late panics.

## Boot and memory contracts

The target uses TCG, `secure=off`, `virtualization=off`, and `gic-version=2`.
CPU 0 enters EL1 with the MMU off, establishes a 16 KiB aligned stack, clears
BSS, installs all sixteen EL1 vectors, and establishes TPIDR_EL1 CPU-local state.
Self-test images seed a BSS probe to verify the clearing loop. The early image is
identity-linked at physical `0x40080000`. UART and GIC register locations are
QEMU platform constants, not Raspberry Pi addresses.

The bootloader must supply an accessible, 8-byte-aligned DTB no larger than
2 MiB. The allocation-free parser checks block bounds, strings/tokens, cell
widths, arithmetic overflow, and fixed capacities. It supports v17-compatible
conventionally ordered blobs, root memory nodes, memreserve entries, and static
identity-addressed `/reserved-memory`. Unsupported translated/dynamic reserved
regions fail closed. No initrd is loaded. The map stores copied values, never
borrowed DTB pointers.

Kernel, boot stack, DTB, firmware reservations, and frame bitmap storage are
excluded from usable RAM. Available intervals are sorted whole 4 KiB pages;
even a one-byte reservation protects its whole page. Live boot objects must lie
inside RAM, and the DTB must not overlap the kernel. `AVAILABLE_BYTES` is the
initial capacity after fixed reservations. `FRAME_FREE_COUNT` additionally
excludes the permanently owned MMU tables at that point, before heap/threads.

`boot/frames.c` uses one ownership bit per usable frame, without bitmap entries
for physical holes. Allocation supports contiguous runs within one usable span;
release validates the whole request before clearing anything. Frame zero,
reserved pages, double releases, and unaligned/out-of-range requests are rejected.
After binding, all runtime frame operations use the IRQ-safe shared page backend;
raw allocator access is confined to initialization and quiescent test snapshots.

The kernel MMU uses immutable four-level, 4 KiB identity page tables shared by all CPUs.
Text is EL1 RX, rodata R/NX, and other RAM RW/NX. UART/GIC pages use device memory
and are NX. Kernel identity mappings never grant EL0 access. Both caches and write-implies-NX are
enabled; TTBR1 walks are disabled. CPU physical-address width is checked/capped
at 48 bits. Failed table construction releases its unpublished frames. Reserved
RAM stays mapped; reservations control allocation, not kernel accessibility.
The null address and RAM holes are unmapped. This is not yet the final
higher-half layout. User mappings, synchronous broadcast TLB invalidation, and an
unmapped user-stack guard are now implemented separately below. ASID allocation
and guarded kernel stacks remain future work.

## Shared heap and locking

`kernel/page_memory.h` provides physical page ownership and explicit direct-map
pointer conversion. The kernel currently uses permanent identity mappings for RAM.
The same `memory/slab.c` provides malloc/free/realloc through the shared page backend.
Payloads are zero-filled and at least 8-byte aligned. Slab pages retain permanent
cache ownership; large allocations return their frames on free. Slot ownership
bits catch double frees, large frees require exact payload pointers, and overflow
is rejected. ARM OOM returns NULL; failed realloc preserves the original object.
Frame exhaustion is a recoverable allocation failure throughout the page backend.

Heap work is forbidden in IRQ context. Cache locks may acquire the innermost
page-backend lock; the reverse is forbidden. The scheduler state lock is never
held while allocating/freeing frames, accessing heap caches, or acquiring process
locks. Queue wakeups and IPI sends allocate nothing. Spinlocks save/restore local
IRQ state and use acquire/release atomics. Per-CPU held-lock depth prevents an
ARM thread from scheduling or blocking while any spinlock is held.

## CPU discovery and secondary entry

The DTB CPU list is copied as MPIDR affinities, separate from logical CPU indices.
One- or two-cell CPU addresses are supported; duplicate IDs, unsupported affinity
bits, malformed PSCI methods, and more than eight CPUs are rejected. The running
boot affinity becomes logical CPU 0. PSCI 0.2+ HVC/SMC conduit is selected from
the DTB rather than guessed. This target assumes QEMU's uniform PSCI CPU setup.

CPU 0 allocates permanent secondary bootstrap stacks, captures its root table,
TCR, MAIR, SCTLR, and CPU-local pointer in aligned boot blocks, cleans those blocks
to PoC, and calls PSCI CPU_ON with a physical entry/context address. Secondary
assembly does not clear shared BSS or use a C stack before establishing the
existing identity map. It installs VBAR, SP, translation/caches, and TPIDR_EL1,
then validates MPIDR and initializes its private interrupt interface and timer.
The shared page tables were built before caching was enabled and remain immutable.

Online publication uses release/acquire ordering and occurs only after local
interrupt/timer readiness. Startup is sequential and bounded. A failed/timed-out
firmware handoff aborts boot; its stack is retained because a late secondary must
never enter freed memory. There is no CPU hotplug, CPU_OFF, or startup retry API.
Bootstrap stacks, per-CPU idle stacks, and CPU-local storage are kernel-lifetime
objects. Physical-board firmware/coherency/boot requirements remain unverified.

## Interrupts and time

GIC distributor initialization happens once on CPU 0. Each CPU separately configures
its banked SGI/PPI state and CPU interface. Private callbacks belong to the CPU
that registered them; global SPIs remain routed to/owned by CPU 0. This is QEMU's
single-security-state Group 0 setup, not a GICv3 or secure-firmware driver.
Remote SGIs use discovered GIC target masks and a store barrier after publishing
RAM state. SGI 2 requests rescheduling; SGI 3 halts peers during fatal shutdown.

IRQ dispatch acknowledges the full IAR token, invokes the source callback, completes
EOI, and decrements CPU-local IRQ depth before considering a context switch.
Spurious IDs require no EOI. Unknown valid IRQs are masked and fatal. Callbacks
must not allocate, block, acquire sleeping locks, or unregister themselves.
Borrowed callback context must remain alive until normal-context unregister.

The system counter supplies monotonic nanoseconds independently of IRQ delivery.
Only CPU 0 initializes the common frequency/origin. Each CPU has its own comparator,
interval, delivered-tick count, running state, callback, and context. EL1 physical
timer PPI 30 is rearmed before EOI; missed periods are skipped without an IRQ storm.
Start/stop/hook replacement operate on the calling CPU. Time conversion avoids
multiplying the full counter by a billion; 64-bit nanoseconds have an uptime limit
of roughly 584 years. No production sleeping API is implied by boot-test polling.

Fatal reporting is allocation-free and lock-free: one atomic owner prints, losers
halt, and the winner sends stop SGIs to online peers. Nesting depth is CPU-local;
second faults use emergency output, third faults halt immediately. Normal panic
reporting never waits for peers to cooperate. The separate panic test bounds its
peer-acknowledgement wait and confirms a secondary can become the sole fatal owner.
A valid stack, CPU-local pointer, and polling UART remain prerequisites.

## SMP scheduler and off-CPU handoff

The thread/process/wait interfaces, intrusive lists, heap and stack backend are
canonical kernel components. `sys/scheduler.c` implements kernel and EL0 threads
with fixed CPU affinity. There is no alternate scheduler backend.

ARM currently uses one short scheduler-state lock to serialize lifecycle and all
per-CPU ready/wait/dead lists. This deliberately prioritizes correctness over
parallel scheduler throughput; per-CPU lock sharding and load balancing are future
work. Ready/wait/dead list ownership belongs solely to the scheduler-state lock;
there are no redundant per-thread or per-queue locks. Use scheduler APIs for access.
Predicates run under the state lock and may not allocate, acquire another lock,
block, or enable IRQs. Producers atomically publish conditions before waking.
The check and wait-list insertion therefore cannot lose a remote notification.

A thread has fixed CPU affinity assigned before ready publication. Wakeups retain
that CPU and send a reschedule IPI when remote. There is no work stealing, automatic
migration, or real-time scheduling policy. AAPCS64 context calls preserve x19–x30
and SP and clear the local exclusive reservation. IRQ frames preserve all integer
registers plus return state; Kernel C excludes SIMD/FP; assembly preserves all q0–q31, FPCR and FPSR.

The key handoff is `on_cpu`: a blocked thread may already be visible on a wait queue
while its outgoing stack/context is still in use. A remote wake then marks it READY
but cannot enqueue it yet. Assembly saves its context, changes SP, and calls
`scheduler_switch_complete` on the incoming stack. Only this hook clears on_cpu
and publishes deferred runnable or dead work. No scheduler lock survives the switch.
A deferred local wake entering idle sends a self-IPI, so it cannot depend on a timer
that the blocked thread may have stopped.

DEAD threads reach the reaper only after that same physical handoff. The reaper is
pinned to CPU 0 and checks/sleeps under the state lock, preventing lost exit wakes.
It frees stacks/descriptors outside the lock and publishes reaping completion after
parent-count updates. CREATED descriptors remain caller-owned and cancellable;
ready publication transfers ownership to scheduler/reaper. Do not retain descriptor
pointers after exit. The bootstrap stack is reserved, not a reapable thread stack.

## Process mappings and EL0

`memory/user.c` owns private address spaces, anonymous mmap, munmap, mprotect,
stack reservations and demand pages. `sys/process.c` owns process creation,
completion and destruction; `sys/user.c` owns TTBR/TLS switching, EL0 entry and
synchronous exception dispatch. ARM-specific
fallible mapping/copy helpers are declared in `kernel/user.h`. Metadata is
one reservation per page under the process lock. No separate legacy VMA list is
retained. Partial unmaps and protection changes therefore do not allocate split
nodes. Map preparation and protection preflight leave existing mappings intact
on allocation/validation failure. Copy helpers may copy a prefix on failure;
callers must check the boolean result. Explicit mmap hints are exact, and automatic
placement uses a monotonic watermark that can leave gaps after failed allocations.

The transitional user window is `[0x10000, 0x08000000)`, keeping null/low pages
unmapped and avoiding the identity-mapped GIC/UART and RAM. Automatic mmap starts
at 16 MiB; stacks end at `0x07fff000`, with 128 KiB reserved below and an unmapped
page underneath. This 128 MiB layout must change for a higher-half kernel and
larger applications. Each process privately clones the three table ancestors of
this window and allocates leaf tables lazily. Kernel branches remain shared.
Anonymous frames are zeroed on demand, retained across PROT_NONE, and released on
unmap/destruction. Empty leaf tables are retained until process destruction.

PTEs grant EL0 access only to user pages, always set PXN, and set UXN unless the
page is executable. W+X is rejected; write/execute imply read. Permission changes
use break-before-make and `TLBI VAAE1IS`, with inner-shareable completion barriers
before physical frames can be reused. No software IPI acknowledgments or IRQ
handler allocation are needed. Executable publication/loading cleans data to PoU
and invalidates instruction caches across CPUs. All processes currently use ASID
zero; a changed TTBR0 triggers a local full invalidation. TPIDR_EL0 is saved per
thread, while TPIDR_EL1 remains kernel CPU storage.

The scheduler changes roots with IRQs masked after releasing its state lock and
before the assembly stack handoff. Shared privileged mappings keep both stacks
valid. It release-publishes the active process per CPU; destruction checks no CPU
still uses the process. Process ownership remains with the caller until every
thread is reaped, including CREATED threads cancelled by the caller. Never destroy
or concurrently publish threads into a process being torn down.

A fresh user thread validates its RX entry and writable aligned stack, resets
SP_EL1 to its owned kernel-stack top, clears integer registers, and ERETs to EL0t.
Vector frames separately save SP_EL0 and SP_EL1; an invalid user stack cannot become
the handler stack. EL0 IRQs use the same EOI/deferred-preemption path as kernel
IRQs. Synchronous translation faults resolve permitted anonymous pages; invalid
addresses, permission violations, and other user traps exit the thread. Fatal
kernel exceptions retain the existing panic path. FP/SIMD is enabled on every CPU and saved across context switches; fresh
EL0 entry clears its bank. Kernel C still uses general registers only.

Static musl init now runs through the validated ARM ELF loader and Linux AArch64
syscall boundary. Syscalls use checked kernel buffers rather than raw user pointers.
The self-test boot coordinator starts parent/child pairs; children are collected after the
last kernel stack is reaped. Orphans are adopted by the kernel. The process tree,
ABI table, ELF stack/TLS setup and precise limitations are documented in
[Kernel architecture](kernel_architecture.md).

## Verification coverage

Native tests exercise the actual DTB parser, frame allocator and byte operations:
20 tests covering
bounds/truncation, CPU affinity/PSCI parsing, RAM holes, reservations, capacities,
fragmentation, exhaustion, invalid releases, a randomized ownership model,
overlapping copies, guard bytes, and unsigned byte comparisons.

QEMU checks cover:

- Normal scheduling and timer preemption on 1, 2, 4, and 8 CPUs.
- Integer-register preservation, wait-one/all/IRQ wakes, idle wakeup, cancellation,
  automatic exit, and reaper accounting.
- Independent per-CPU timer programming, including stopping one CPU's timer.
- Remote wakeup with the target's timer stopped, requiring an IPI.
- 400 reciprocal cross-core exchanges with shared heap/frame operations.
- Concurrent exit/reap waves with identical post-warmup free-frame counts.
- A forced before-context-save wake, proving deferred enqueue on 2/4 CPUs.
- Two EL0 processes per CPU using identical virtual addresses with distinct data
  and TLS, preserving registers/stacks across SVC yield and timer preemption.
- Anonymous demand zeroing, W^X/range/overlap validation, PROT_NONE retention,
  partial protection/unmap, remapping, and stable frame counts after teardown.
- Actual hardware faults for writing a resident RO page, executing NX data,
  reading kernel RAM, unmapped memory, and an invalid SP_EL0.
- Static musl startup with argc/argv/envp/auxv, real TLS, initialized/BSS data,
  SIMD preservation, allocation, filesystem/vector/positioned I/O, stat and time.
- Parent/child wait status, WNOHANG, one-shot reaping, orphan adoption and fatal
  managed-process faults; two complete waves require equal free-frame counts.
- Malformed ELF metadata/TLS and invalid/cross-page syscall pointers.
- Remote read/write translation revocation while the target EL0 CPU masks IRQs
  and never yields; switch counters prove no incidental local flush occurred.
- Heap misuse, scheduling under a held lock/from IRQ, MMU permission faults,
  table-construction rollback, nested faults, and SMP fatal ownership/peer halt.

Checks compare RAM against emulator configuration, reservations against the linked
Image, and table-frame counts against mapped prefixes. SMP checks require each CPU's
ticks/IPIs and exact completed worker count. Negative images are separate builds.
The test workflow builds the ARM kernel and host-native parser/allocator/byte-operation fixtures.

References: [QEMU virt](https://www.qemu.org/docs/master/system/arm/virt),
[Devicetree](https://devicetree-org.github.io/devicetree-specification/),
[Arm PSCI](https://documentation-service.arm.com/static/640f584656ea36189d4e94a4),
[AAPCS64](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst),
[GICv2](https://documentation-service.arm.com/static/5f8ff21df86e16515cdbfafe).

Next: user-driven fork/clone/execve, signals/futexes, and storage/input support. Scheduler lock sharding, load balancing, and CPU hotplug remain
independent future improvements.
