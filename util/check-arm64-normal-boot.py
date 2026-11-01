#!/usr/bin/env python3
"""Exercise upstream static BusyBox on Aether through its serial shell."""
from pathlib import Path
import os
import re
import selectors
import subprocess
import sys
import time


def check_boot(emulator, image, cpu_count):
    """Check delayed wakeups, FIFO reuse, editing, CRLF, and clean init exit."""
    command = [emulator, "-machine", "virt,secure=off,virtualization=off,gic-version=2",
               "-accel", "tcg", "-cpu", "cortex-a72", "-smp", str(cpu_count), "-m", "256M",
               "-display", "none", "-monitor", "none", "-serial", "stdio", "-no-reboot",
               "-kernel", image]
    output = bytearray()
    with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT) as process:
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)

            def wait_for(marker, start=0):
                deadline = time.monotonic() + 10
                while marker not in output[start:]:
                    if time.monotonic() >= deadline:
                        raise RuntimeError(f"Timed out waiting for {marker!r}")
                    for key, _ in selector.select(0.1):
                        data = os.read(key.fileobj.fileno(), 65536)
                        if not data:
                            raise RuntimeError("QEMU exited unexpectedly")
                        output.extend(data)

            def send(data):
                process.stdin.write(data)
                process.stdin.flush()

            try:
                wait_for(b"aether> ")
                # Let init enter its empty-input wait before issuing the IRQ.
                time.sleep(0.1)
                def command(text, result):
                    start = len(output)
                    send(text.encode() + b"\n")
                    wait_for(b"\r\n" + result.encode() + b"\r\naether> ", start)

                command("echo helx\x7flo", "hello")
                command("/bin/echo native-exec", "native-exec")
                match = re.search(rb"Aether kernel (0\.1-[0-9a-f]{12}-[0-9T:Z-]+)\r?\n", output)
                if not match:
                    raise RuntimeError("Missing kernel release banner")
                command("/bin/uname -sr", "Aether " + match.group(1).decode())
                command("/bin/uname -o", "Aether")
                command("/bin/uname -n", "Aether")

                command("/bin/echo file-data > /tmp/message; /bin/cat /tmp/message", "file-data")
                command("/bin/echo pipe-data | /bin/cat", "pipe-data")
                command("/bin/mkdir /tmp/work; cd /tmp/work; /bin/pwd", "/tmp/work")
                command("/bin/false; echo $?", "1")
                command("/missing-command; echo $?", "127")
                # Repeated fork/exec/wait and enough received bytes to wrap the UART FIFO.
                for index in range(20):
                    command(f"/bin/echo launch-{index:02d}", f"launch-{index:02d}")
                command("echo wrong\x08\x08\x08\x08\x08edited", "edited")
                start = len(output)
                send(b"exit\n")
                wait_for(b"Init exited. System idle.", start)
                # Keep observing after success so late panics are detected.
                deadline = time.monotonic() + 0.3
                while time.monotonic() < deadline:
                    for key, _ in selector.select(0.05):
                        data = os.read(key.fileobj.fileno(), 65536)
                        if data:
                            output.extend(data)
            except RuntimeError as error:
                raise SystemExit(f"Normal boot failed ({cpu_count} CPUs): {error}\n"
                                 + output.decode(errors="replace")) from error
            finally:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                Path(image).with_name(f"normal-boot-{cpu_count}.log").write_bytes(output)
    diagnostics = output.decode(errors="replace")
    if (diagnostics.count("Aether init: ARM64 userspace ready.") != 1 or
            "BusyBox v1.36.1" not in diagnostics or
            any(marker in diagnostics.lower() for marker in ("passed", "panic", "fatal", "fail"))):
        raise SystemExit(f"Normal boot failed ({cpu_count} CPUs):\n{diagnostics}")
    print(f"ARM64 normal boot passed ({cpu_count} CPUs): BusyBox shell, fork/exec, files, pipes, editing, idle exit")


def main():
    emulator, image = sys.argv[1:3]
    for cpu_count in (1, 4):
        check_boot(emulator, image, cpu_count)


if __name__ == "__main__":
    main()
