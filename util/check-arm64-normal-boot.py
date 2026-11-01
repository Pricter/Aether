#!/usr/bin/env python3
"""Check normal ARM startup independently of the self-test kernel images."""
from pathlib import Path
import subprocess
import sys


def check_boot(emulator, image, cpu_count):
    """Require one normal init, no validation workers, and a clean idle handoff."""
    command = [emulator, "-machine", "virt,secure=off,virtualization=off,gic-version=2",
               "-accel", "tcg", "-cpu", "cortex-a72", "-smp", str(cpu_count), "-m", "256M",
               "-display", "none", "-monitor", "none", "-serial", "stdio", "-no-reboot",
               "-kernel", image]
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
        try:
            output, unused_stderr = process.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                output, unused_stderr = process.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                output, unused_stderr = process.communicate()
    diagnostics = output.decode(errors="replace")
    Path(image).with_name(f"normal-boot-{cpu_count}.log").write_text(diagnostics)
    if (diagnostics.count("Aether init: ARM64 userspace ready.") != 1 or
            "Init exited. System idle." not in diagnostics or
            any(marker in diagnostics.lower() for marker in ("passed", "panic", "fatal", "fail"))):
        raise SystemExit(f"Normal boot failed ({cpu_count} CPUs):\n{diagnostics}")
    print(f"ARM64 normal boot passed ({cpu_count} CPUs): one init, no self-tests")


def main():
    """Use the normal Image; test builds have a separate output directory."""
    emulator, image = sys.argv[1:3]
    for cpu_count in (1, 4):
        check_boot(emulator, image, cpu_count)


if __name__ == "__main__":
    main()
