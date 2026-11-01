"""Boot the minimal ARM kernel and validate its serial handoff diagnostics."""

import subprocess
import sys
import re
import struct
from pathlib import Path


def check_memory_map(diagnostics, memory_mebibytes, kernel_image):
    """Independently subtract page-rounded reservations from expected QEMU RAM."""
    reported_ranges = {"RAM": [], "RESERVED": [], "AVAILABLE": []}
    reservation_labels = set()
    labelled_reservations = {}
    for range_kind, base, end, label in re.findall(
        r"^(RAM|RESERVED|AVAILABLE) (0x[0-9a-f]+) (0x[0-9a-f]+) ([^\r\n]+)",
        diagnostics, re.MULTILINE,
    ):
        reported_ranges[range_kind].append((int(base, 16), int(end, 16)))
        if range_kind == "RESERVED":
            reservation_labels.add(label)
            labelled_reservations[label] = (int(base, 16), int(end, 16))
    expected_ram = [(0x40000000, 0x40000000 + memory_mebibytes * 1024 * 1024)]
    if reported_ranges["RAM"] != expected_ram:
        raise SystemExit("Discovered RAM does not match QEMU configuration")
    if not {"device tree", "kernel", "boot stack", "frame bitmap"} <= reservation_labels:
        raise SystemExit("Missing live boot-object reservation")
    image_header = Path(kernel_image).read_bytes()[:64]
    image_size = struct.unpack_from("<Q", image_header, 16)[0]
    kernel_base, kernel_end = labelled_reservations["kernel"]
    stack_base, stack_end = labelled_reservations["boot stack"]
    if (image_header[56:60] != b"ARM\x64" or kernel_base != 0x40080000 or
            kernel_end != stack_base or stack_end - stack_base != 16384 or
            stack_end != kernel_base + image_size):
        raise SystemExit("Kernel/stack reservations do not cover the linked image")
    expected_available = expected_ram
    for reserved_base, reserved_end in reported_ranges["RESERVED"]:
        reserved_base &= ~4095
        reserved_end = (reserved_end + 4095) & ~4095
        remaining_ranges = []
        for available_base, available_end in expected_available:
            if reserved_end <= available_base or reserved_base >= available_end:
                remaining_ranges.append((available_base, available_end))
            else:
                if available_base < reserved_base:
                    remaining_ranges.append((available_base, reserved_base))
                if reserved_end < available_end:
                    remaining_ranges.append((reserved_end, available_end))
        expected_available = remaining_ranges
    if reported_ranges["AVAILABLE"] != expected_available:
        raise SystemExit("Available pages differ from independently calculated map")
    total_match = re.search(r"^AVAILABLE_BYTES (0x[0-9a-f]+)", diagnostics, re.MULTILINE)
    expected_bytes = sum(end - base for base, end in expected_available)
    if not total_match or int(total_match.group(1), 16) != expected_bytes:
        raise SystemExit("Available byte count is inconsistent")
    table_match = re.search(r"^MMU_TABLE_FRAMES (0x[0-9a-f]+)", diagnostics, re.MULTILINE)
    free_match = re.search(r"^FRAME_FREE_COUNT (0x[0-9a-f]+)", diagnostics, re.MULTILINE)
    if not table_match or not free_match:
        raise SystemExit("Missing physical allocator/MMU accounting")
    table_frames = int(table_match.group(1), 16)
    # Four-level, page-only identity maps require one table per distinct prefix.
    # Include the UART separately, without treating its MMIO page as RAM.
    table_prefixes = {shift: set() for shift in (39, 30, 21)}
    for ram_base, ram_end in expected_ram + [(0x09000000, 0x09001000),
                                            (0x08000000, 0x08001000),
                                            (0x08010000, 0x08011000)]:
        for shift, prefixes in table_prefixes.items():
            prefixes.update(range(ram_base >> shift, ((ram_end - 1) >> shift) + 1))
    expected_table_frames = 1 + sum(len(prefixes) for prefixes in table_prefixes.values())
    if (table_frames != expected_table_frames or
            int(free_match.group(1), 16) != expected_bytes // 4096 - table_frames):
        raise SystemExit("Page-table frame ownership does not match free-frame accounting")


def main():
    """Bound QEMU runtime and always reap the emulator after the smoke check."""
    emulator, kernel_image = sys.argv[1:3]
    check_mode = sys.argv[3] if len(sys.argv) > 3 else "boot"
    memory_mebibytes = int(sys.argv[4]) if len(sys.argv) > 4 else 256
    cpu_count = int(sys.argv[5]) if len(sys.argv) > 5 else 1
    command = [
        emulator, "-machine", "virt,secure=off,virtualization=off,gic-version=2",
        "-accel", "tcg", "-cpu", "cortex-a72", "-smp", str(cpu_count), "-m", f"{memory_mebibytes}M",
        "-display", "none", "-monitor", "none", "-serial", "stdio",
        "-no-reboot", "-kernel", kernel_image,
    ]
    with subprocess.Popen(command, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT) as emulator_process:
        reached_timeout = False
        try:
            output = emulator_process.communicate(timeout=20 if cpu_count > 1 else 12)[0]
        except subprocess.TimeoutExpired:
            reached_timeout = True
            emulator_process.terminate()
            try:
                output = emulator_process.communicate(timeout=2)[0]
            except subprocess.TimeoutExpired:
                emulator_process.kill()
                output = emulator_process.communicate()[0]
    diagnostics = output.decode(errors="replace")
    print(diagnostics, end="")
    check_memory_map(diagnostics, memory_mebibytes, kernel_image)
    expected_messages = (
        "Aether ARM64 booted",
        "EL1; stack aligned; BSS cleared; MMU off",
        "ARM64 memory discovery passed",
        "ARM64 frame allocator passed",
        "ARM64 MMU enabled",
        "ARM64 memory access passed",
        "ARM64 shared heap passed",
        "ARM64 interrupts passed",
        "ARM64 timer passed",
        "ARM64 thread cancellation passed",
        "ARM64 thread contexts passed",
        "ARM64 preemption passed",
        "ARM64 wait queues passed",
        "ARM64 idle wakeup passed",
        "ARM64 thread reaping passed",
        "ARM64 user mappings passed",
        "ARM64 EL0 isolation passed",
        "ARM64 user faults passed",
        "ARM64 TLB shootdown passed",
        "ARM64 user reclamation passed",
        "ARM64 EL0 passed",
        "ARM64 shared filesystems passed",
        "ARM64 ELF validation passed",
        "ARM64 process wait passed",
        "ARM64 orphan adoption and fault exit passed",
        "ARM64 init reclamation passed",
        "ARM64 musl init passed",
        "ARM64 scheduler passed",
        "ARM64 idle",
    )
    if check_mode in ("exception", "nested"):
        expected_messages = (
            "ARM64 fault test: BRK", "ARM64 fatal exception", "synchronous",
            "VECTOR=0x0000000000000004", "ESR_EL1=0x00000000f2001234",
            "AArch64 breakpoint", "x00=0x0000000000001234",
            "x01=0x0000000000005678", "x30=", "ELR_EL1=0x000000004",
            "SPSR_EL1=", "FAR_EL1(raw)=", "SP_EL1=",
            "ARM64 exception halted" if check_mode == "exception" else
            "ARM64 nested exception: emergency halt",
        )
        if "ARM64 idle" in diagnostics or "unexpectedly returned" in diagnostics:
            raise SystemExit("Fault handler incorrectly returned")
        if diagnostics.count("ARM64 fatal exception") != 1:
            raise SystemExit("Fault handler did not gate nested diagnostics")
    elif check_mode in ("mmu-unmapped", "mmu-readonly", "mmu-execute"):
        expected_syndromes = {"mmu-unmapped": 0x96000006, "mmu-readonly": 0x9600004f,
                              "mmu-execute": 0x8600000f}
        expected_messages = ("ARM64 MMU enabled", "ARM64 memory access passed",
                             "ARM64 fatal exception", "ARM64 exception halted",
                             f"ESR_EL1=0x{expected_syndromes[check_mode]:016x}")
        target_match = re.search(r"MMU fault target=(0x[0-9a-f]+)", diagnostics)
        fault_match = re.search(r"FAR_EL1\(raw\)=(0x[0-9a-f]+)", diagnostics)
        if (not target_match or not fault_match or target_match.group(1) != fault_match.group(1) or
                "ARM64 idle" in diagnostics or "unexpectedly returned" in diagnostics or
                diagnostics.count("ARM64 fatal exception") != 1):
            raise SystemExit("MMU permission test did not trap at the intended address")
    elif check_mode in ("heap-irq", "heap-double-free"):
        expected_panic = ("allocation in interrupt context" if check_mode == "heap-irq" else
                          "double free of slab object")
        expected_messages = ("ARM64 shared heap passed", f"ARM64 panic: slab: {expected_panic}")
        if "ARM64 idle" in diagnostics or "unexpectedly returned" in diagnostics:
            raise SystemExit("Heap misuse guard incorrectly returned")
    elif check_mode in ("scheduler-lock", "scheduler-irq"):
        expected_messages = (
            "ARM64 scheduler initialized (CPU 0)",
            "ARM64 panic: kernel: assertion failed: !kernel_interrupt_depth() && !kernel_spinlock_depth()",
        )
        if "ARM64 idle" in diagnostics or "ARM64 scheduler passed" in diagnostics:
            raise SystemExit("Scheduler context guard incorrectly returned")
    elif check_mode == "smp-panic":
        expected_messages = ("ARM64 SMP startup passed", "ARM64 panic peers stopped",
                             "ARM64 panic: smp: intentional SMP panic")
        if diagnostics.count("ARM64 panic:") != 1 or "ARM64 scheduler passed" in diagnostics:
            raise SystemExit("SMP panic did not have a sole fatal owner")
    elif "ARM64 fatal exception" in diagnostics:
        raise SystemExit("Unexpected exception during normal boot")
    if check_mode == "mmu-rollback":
        expected_messages += ("ARM64 MMU rollback passed",)
    if check_mode in ("boot", "mmu-rollback", "handoff"):
        if "ARM64 panic:" in diagnostics:
            raise SystemExit("Unexpected panic during scheduled execution")
        for label, minimum in (("COUNTER_FREQUENCY", 1), ("TIMER_TICKS", 10),
                               ("MONOTONIC_NS", 100000000), ("SCHEDULER_SWITCHES", 64),
                               ("SCHEDULER_PREEMPTIONS", 4), ("SCHEDULER_REAPED", 41)):
            scalar = re.search(rf"^{label} (0x[0-9a-f]+)", diagnostics, re.MULTILINE)
            if not scalar or int(scalar.group(1), 16) < minimum:
                raise SystemExit(f"Invalid timer diagnostic: {label}")
        online = re.search(r"^SMP_ONLINE (0x[0-9a-f]+)", diagnostics, re.MULTILINE)
        if not online or int(online.group(1), 16) != cpu_count:
            raise SystemExit("Online CPU count does not match QEMU configuration")
        if cpu_count > 1:
            expected_messages += ("ARM64 SMP startup passed", "ARM64 per-CPU timers passed",
                                  "ARM64 remote wakeup passed", "ARM64 cross-core waits passed",
                                  "ARM64 SMP reaping passed", "ARM64 SMP passed")
            cpu_statistics = re.findall(
                r"SMP_CPU (0x[0-9a-f]+)\r?\nSMP_TICKS (0x[0-9a-f]+)\r?\nSMP_IPIS (0x[0-9a-f]+)",
                diagnostics,
            )
            if len(cpu_statistics) != cpu_count or {int(entry[0], 16) for entry in cpu_statistics} != set(range(cpu_count)):
                raise SystemExit("Missing per-CPU runtime statistics")
            for cpu_id, ticks, ipis in cpu_statistics:
                if int(ticks, 16) < 3 or (int(cpu_id, 16) > 0 and int(ipis, 16) < 1):
                    raise SystemExit("A secondary CPU did not receive its timer/IPI interrupts")
            reaped = re.search(r"^SMP_REAPED (0x[0-9a-f]+)", diagnostics, re.MULTILINE)
            if not reaped or int(reaped.group(1), 16) != 45 + 10 * cpu_count:
                raise SystemExit("Not every SMP worker completed off-CPU reclamation")
        # Two isolated contexts/CPU, seven EL0 fault/revocation probes, two
        # musl parent/child waves per CPU, and three adoption/fault workers.
        completed_users = re.search(r"^SCHEDULER_REAPED (0x[0-9a-f]+)", diagnostics, re.MULTILINE)
        kernel_workers = 41 if cpu_count == 1 else 45 + 10 * cpu_count
        if not completed_users or int(completed_users.group(1), 16) != kernel_workers + 6 * cpu_count + 10:
            raise SystemExit("Not every user thread completed off-CPU reclamation")
        if check_mode == "handoff":
            expected_messages += ("ARM64 forced handoff passed",)
    if not reached_timeout or not all(message in diagnostics for message in expected_messages):
        raise SystemExit("ARM64 boot smoke check failed")
    print(f"ARM64 {check_mode} smoke check passed")


if __name__ == "__main__":
    main()
