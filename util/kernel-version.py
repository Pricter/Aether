#!/usr/bin/env python3
"""Generate shared kernel build metadata, using UTC and an abbreviated HEAD hash."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parent.parent
    try:
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
                                  capture_output=True, text=True, check=True).stdout.strip()[:12]
    except (OSError, subprocess.CalledProcessError):
        revision = "unknown"
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    instant = (datetime.fromtimestamp(int(epoch), timezone.utc) if epoch is not None
               else datetime.now(timezone.utc))
    timestamp = instant.strftime("%Y-%m-%dT%H:%M:%SZ")
    content = ("/* Generated build metadata; edit kernel/version.h for release policy. */\n"
               "#pragma once\n"
               f"#define KERNEL_GIT_REVISION {json.dumps(revision)}\n"
               f"#define KERNEL_BUILD_TIMESTAMP {json.dumps(timestamp)}\n")
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != content:
        output.write_text(content)


if __name__ == "__main__":
    main()
