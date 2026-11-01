#!/usr/bin/env python3
"""Build pinned, unpatched BusyBox sources as a static AArch64 Executable."""
import hashlib
import json
import re
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
VERSION = "1.36.1"
DIGEST = "b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314"


def main():
    vendor = ROOT / "build/vendor"
    vendor.mkdir(parents=True, exist_ok=True)
    archive = vendor / f"busybox-{VERSION}.tar.bz2"
    if not archive.exists():
        urllib.request.urlretrieve(f"https://busybox.net/downloads/{archive.name}", archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != DIGEST:
        raise SystemExit("BusyBox archive checksum mismatch")
    source = vendor / f"busybox-{VERSION}"
    if not source.exists():
        with tarfile.open(archive) as bundle:
            # The checksum pins all archive contents; also reject paths outside vendor.
            for member in bundle.getmembers():
                if not (vendor / member.name).resolve().is_relative_to(vendor.resolve()):
                    raise SystemExit("Unsafe BusyBox archive path")
            bundle.extractall(vendor)
    subprocess.run(["make", "allnoconfig"], cwd=source, check=True,
                   stdout=subprocess.DEVNULL)
    config = source / ".config"
    text = config.read_text()
    for option in ("STATIC", "ASH", "SH_IS_ASH", "ASH_ECHO", "ASH_PRINTF", "ASH_TEST",
                   "ASH_OPTIMIZE_FOR_SIZE", "ECHO", "CAT", "LS", "PWD", "TRUE", "FALSE",
                   "MKDIR", "RM", "WC", "ENV", "UNAME", "TEST", "FEATURE_TEST_64", "FEATURE_LS_FILETYPES"):
        text = text.replace(f"# CONFIG_{option} is not set", f"CONFIG_{option}=y")
    text = text.replace("CONFIG_SH_IS_NONE=y", "# CONFIG_SH_IS_NONE is not set")
    identity = (ROOT / "kernel/include/kernel/version.h").read_text()
    os_name = re.search(r'^#define OS_NAME "([^"\n]+)"$', identity, re.MULTILINE).group(1)
    text = re.sub(r'^CONFIG_UNAME_OSNAME=.*$',
                  "CONFIG_UNAME_OSNAME=" + json.dumps(os_name), text, flags=re.MULTILINE)
    config.write_text(text)
    archiver = shutil.which("llvm-ar")
    if not archiver:
        archiver = next((str(p) for p in Path("/opt/homebrew/opt").glob("llvm*/bin/llvm-ar")), None)
    if not archiver:
        raise SystemExit("llvm-ar is required for BusyBox")
    compiler = f"{shutil.which('python3')} {ROOT / 'util/busybox-cc.py'}"
    subprocess.run(["make", "-j4", "ARCH=arm64", f"CC={compiler}", f"AR={archiver}", f"STRIP={Path(archiver).with_name('llvm-strip')}",
                    "HOSTCC=clang", "HOSTCFLAGS=-O2 -Wno-deprecated-non-prototype", "busybox"],
                   cwd=source, check=True)


if __name__ == "__main__":
    main()
