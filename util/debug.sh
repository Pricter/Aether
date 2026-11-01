#!/bin/sh
# Attach an AArch64-capable debugger to make run QEMU_DEBUG="-S -s".
exec "${GDB:-gdb-multiarch}" -x util/gdb_commands.txt
