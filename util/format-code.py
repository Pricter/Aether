#!/usr/bin/env python3
"""Format first-party C sources and headers using the repository style."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    """Use one formatter invocation so check and write cover identical files."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    formatter = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format")
    if not formatter:
        for prefix in (Path("/opt/homebrew/opt"), Path("/usr/local/opt")):
            candidates = [prefix / "llvm" / "bin/clang-format"]
            candidates += sorted(prefix.glob("llvm@*/bin/clang-format"), reverse=True)
            formatter = next((str(candidate) for candidate in candidates if candidate.is_file()), None)
            if formatter:
                break
    if not formatter:
        print("clang-format is required; install LLVM or set CLANG_FORMAT", file=sys.stderr)
        return 1
    sources = []
    for directory in (root / "kernel", root / "userspace"):
        for path in sorted(directory.rglob("*")):
            if path.suffix not in (".c", ".h"):
                continue
            if "external" in path.parts or "deps" in path.parts:
                continue
            sources.append(str(path))
    sources += [str(root / "base/usr/include" / name) for name in ("memory.h", "string.h")]
    options = ["--dry-run", "--Werror"] if arguments.check else ["-i"]
    return subprocess.run([formatter, *options, *sources], cwd=root, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
