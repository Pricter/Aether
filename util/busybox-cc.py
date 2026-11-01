#!/usr/bin/env python3
"""Adapt GNU ld diagnostic flags to Zig's static cross compiler."""
import os
from pathlib import Path
import subprocess
import sys

# BusyBox's final link adds GNU ld diagnostics that Zig's driver rejects.
arguments = [arg for arg in sys.argv[1:] if arg not in ("-Wl,--warn-common", "-Wl,--verbose") and not arg.startswith("-Wl,-Map,")]
raise SystemExit(subprocess.call([os.environ.get("ZIG", "zig"), "cc", "-target",
    (Path(__file__).resolve().parent.parent / "docs/userspace-target.txt").read_text().strip(), "-fno-pie", "-Wl,-no-pie", "-Wl,-z,max-page-size=4096", *arguments]))
