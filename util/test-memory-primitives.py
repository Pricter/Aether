"""Validate the real freestanding byte operations against independent host models."""

import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest


class MemoryPrimitiveTests(unittest.TestCase):
    """Check overlap, bounds, return pointers and unsigned byte ordering."""

    @classmethod
    def setUpClass(cls):
        """Rename symbols so host libc calls cannot accidentally satisfy a test."""
        root = Path(__file__).resolve().parent.parent
        cls.storage = tempfile.TemporaryDirectory(prefix="aether-memory-")
        library_path = Path(cls.storage.name) / "memory.so"
        subprocess.run([
            "clang", "-shared", "-fPIC", "-fno-builtin", "-O2", "-Wall", "-Wextra", "-Werror",
            f"-I{root / 'kernel/include'}", "-Dmemcpy=aether_memcpy", "-Dmemmove=aether_memmove",
            "-Dmemset=aether_memset", "-Dmemcmp=aether_memcmp",
            str(root / "kernel/libc/memory.c"), "-o", str(library_path),
        ], check=True)
        cls.library = ctypes.CDLL(str(library_path))
        for name in ("memcpy", "memmove", "memset", "memcmp"):
            function = getattr(cls.library, "aether_" + name)
            middle_type = ctypes.c_int if name == "memset" else ctypes.c_void_p
            function.argtypes = [ctypes.c_void_p, middle_type, ctypes.c_size_t]
            function.restype = ctypes.c_int if name == "memcmp" else ctypes.c_void_p

    @classmethod
    def tearDownClass(cls):
        """Release the temporary native library and compiler artifacts."""
        cls.storage.cleanup()

    def test_overlapping_moves(self):
        """Both overlap directions must preserve a snapshot of source bytes."""
        original = bytes(range(64))
        for source_offset in range(16):
            for destination_offset in range(16):
                for length in (0, 1, 7, 16, 32):
                    buffer = ctypes.create_string_buffer(original, len(original))
                    expected = bytearray(original)
                    expected[destination_offset:destination_offset + length] = (
                        original[source_offset:source_offset + length])
                    base = ctypes.addressof(buffer)
                    result = self.library.aether_memmove(
                        base + destination_offset, base + source_offset, length)
                    self.assertEqual(result, base + destination_offset)
                    self.assertEqual(bytes(buffer), bytes(expected))

    def test_disjoint_copy_preserves_guards(self):
        """Copy exactly the requested region and return the destination address."""
        source = ctypes.create_string_buffer(bytes(range(64)), 64)
        for length in range(65):
            destination = ctypes.create_string_buffer(bytes([0xa5]) * 80, 80)
            target = ctypes.addressof(destination) + 8
            self.assertEqual(self.library.aether_memcpy(target, source, length), target)
            self.assertEqual(bytes(destination),
                             bytes([0xa5]) * 8 + bytes(range(length)) +
                             bytes([0xa5]) * (72 - length))

    def test_fill_uses_low_byte(self):
        """Fill truncates int values to unsigned bytes without changing guards."""
        for value in (-1, 0, 0x5a, 0x1ff):
            buffer = ctypes.create_string_buffer(bytes(32), 32)
            target = ctypes.addressof(buffer) + 4
            self.assertEqual(self.library.aether_memset(target, value, 24), target)
            self.assertEqual(bytes(buffer), bytes(4) + bytes([value & 0xff]) * 24 + bytes(4))

    def test_unsigned_comparison_and_prefix_bounds(self):
        """Comparison ignores suffixes and compares high-bit bytes as unsigned."""
        left = ctypes.create_string_buffer(b"same\x80")
        right = ctypes.create_string_buffer(b"same\x7f")
        self.assertEqual(self.library.aether_memcmp(left, right, 0), 0)
        self.assertEqual(self.library.aether_memcmp(left, right, 4), 0)
        self.assertGreater(self.library.aether_memcmp(left, right, 5), 0)
        self.assertLess(self.library.aether_memcmp(right, left, 5), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
