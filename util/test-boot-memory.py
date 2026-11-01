"""Exercise the actual allocation-free C parser with bounded synthetic DTBs."""

import ctypes
import pathlib
import struct
import subprocess
import tempfile
import unittest

PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]


class MemoryRange(ctypes.Structure):
    """Mirror the public physical interval ABI for native parser checks."""

    _fields_ = [("base", ctypes.c_ulong), ("end", ctypes.c_ulong),
                ("description", ctypes.c_char_p)]


class MemoryMap(ctypes.Structure):
    """Mirror the fixed-capacity boot map; test processes own these buffers."""

    _fields_ = [("ram", MemoryRange * 16), ("reserved", MemoryRange * 32),
                ("available", MemoryRange * 64), ("ram_count", ctypes.c_uint),
                ("reserved_count", ctypes.c_uint), ("available_count", ctypes.c_uint),
                ("device_tree_size", ctypes.c_ulong),
                ("cpu_hardware_ids", ctypes.c_uint64 * 8),
                ("cpu_count", ctypes.c_uint), ("psci_conduit", ctypes.c_uint)]


def encode_cells(value, cell_count):
    """Encode a single physical address or size in big-endian DT cells."""
    return value.to_bytes(cell_count * 4, "big")


def build_tree(memory_ranges=((0x40000000, 0x10000000),), cell_count=2,
               reservations=(), reserved_regions=(), dynamic_reservation=False,
               translated_reservations=False, disabled_region=False,
               cpu_ids=None, cpu_cells=1, psci_method="hvc"):
    """Build standard v17 blobs with selectable RAM and reservation semantics."""
    strings = bytearray()
    string_offsets = {}

    def property_token(name, value):
        """Intern property names and emit aligned structure property tokens."""
        if name not in string_offsets:
            string_offsets[name] = len(strings)
            strings.extend(name.encode() + b"\0")
        return (struct.pack(">III", 3, len(value), string_offsets[name]) + value +
                bytes((-len(value)) % 4))

    def node_token(name, properties, children=()):
        """Emit properties before children as required by the DT structure format."""
        encoded_name = name.encode() + b"\0"
        return (struct.pack(">I", 1) + encoded_name + bytes((-len(encoded_name)) % 4) +
                b"".join(property_token(*entry) for entry in properties) +
                b"".join(children) + struct.pack(">I", 2))

    register_data = b"".join(encode_cells(base, cell_count) + encode_cells(length, cell_count)
                             for base, length in memory_ranges)
    memory_node = node_token("memory@40000000", [("device_type", b"memory\0"),
                                                 ("reg", register_data)])
    children = [memory_node]
    if cpu_ids is not None:
        cpu_children = [node_token(f"cpu@{hardware_id:x}", [
            ("device_type", b"cpu\0"), ("reg", encode_cells(hardware_id, cpu_cells)),
        ]) for hardware_id in cpu_ids]
        children.append(node_token("cpus", [
            ("#address-cells", struct.pack(">I", cpu_cells)),
            ("#size-cells", struct.pack(">I", 0)),
        ], cpu_children))
        children.append(node_token("psci", [
            ("compatible", b"arm,psci-1.0\0arm,psci-0.2\0"),
            ("method", psci_method.encode() + b"\0"),
        ]))
    if reserved_regions or dynamic_reservation:
        reserved_children = []
        for region_index, (base, length) in enumerate(reserved_regions):
            properties = [("reg", encode_cells(base, cell_count) + encode_cells(length, cell_count))]
            if disabled_region:
                properties.append(("status", b"disabled\0"))
            reserved_children.append(node_token(f"region@{region_index:x}", properties))
        if dynamic_reservation:
            reserved_children.append(node_token("dynamic", [("size", encode_cells(4096, cell_count))]))
        children.append(node_token("reserved-memory", [
            ("#address-cells", struct.pack(">I", cell_count)),
            ("#size-cells", struct.pack(">I", cell_count)),
            ("ranges", b"translated" if translated_reservations else b""),
        ], reserved_children))
    structure = node_token("", [
        ("#address-cells", struct.pack(">I", cell_count)),
        ("#size-cells", struct.pack(">I", cell_count)),
    ], children) + struct.pack(">I", 9)
    reservation_block = b"".join(struct.pack(">QQ", *entry) for entry in reservations) + bytes(16)
    structure_offset = 40 + len(reservation_block)
    strings_offset = structure_offset + len(structure)
    total = strings_offset + len(strings)
    header = struct.pack(">10I", 0xd00dfeed, total, structure_offset, strings_offset,
                         40, 17, 16, 0, len(strings), len(structure))
    return header + reservation_block + structure + strings


class BootMemoryTests(unittest.TestCase):
    """Check format validation and independently specified free-page results."""

    @classmethod
    def setUpClass(cls):
        """Compile the same parser source into a disposable native test library."""
        cls.temporary_directory = tempfile.TemporaryDirectory(prefix="aether-memory-")
        library_path = pathlib.Path(cls.temporary_directory.name) / "parser.so"
        subprocess.run([
            "clang", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
            f"-I{PROJECT_ROOT / 'kernel/include'}", str(PROJECT_ROOT / "kernel/boot/memory.c"),
            "-o", str(library_path),
        ], check=True)
        cls.parser = ctypes.CDLL(str(library_path))
        cls.parser.boot_memory_discover.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                                    ctypes.c_ulong, ctypes.POINTER(MemoryMap)]
        cls.parser.boot_memory_discover.restype = ctypes.c_bool
        cls.parser.boot_memory_finalize.argtypes = [ctypes.POINTER(MemoryMap)]
        cls.parser.boot_memory_finalize.restype = ctypes.c_bool

    @classmethod
    def tearDownClass(cls):
        """Remove generated test artifacts after all parser checks finish."""
        cls.temporary_directory.cleanup()

    def discover(self, blob, readable_bytes=None):
        """Keep blob storage alive for the full synchronous parser invocation."""
        source = ctypes.create_string_buffer(blob)
        memory_map = MemoryMap()
        accepted = self.parser.boot_memory_discover(
            source, len(blob) if readable_bytes is None else readable_bytes,
            0x48000000, ctypes.byref(memory_map))
        return accepted, memory_map

    def test_one_and_two_cell_memory(self):
        """Both common root cell widths describe identical physical memory."""
        for cell_count in (1, 2):
            accepted, memory_map = self.discover(build_tree(cell_count=cell_count))
            self.assertTrue(accepted)
            self.assertEqual((memory_map.ram[0].base, memory_map.ram[0].end),
                             (0x40000000, 0x50000000))

    def test_cpu_affinity_and_psci(self):
        """Copy MPIDR affinities with either CPU cell width and PSCI conduit."""
        for cpu_cells, hardware_ids, method, conduit in (
            (1, (0, 1, 2, 3), "hvc", 1),
            (2, (0, 0x100000001), "smc", 2),
        ):
            accepted, memory_map = self.discover(build_tree(
                cpu_ids=hardware_ids, cpu_cells=cpu_cells, psci_method=method))
            self.assertTrue(accepted)
            self.assertEqual(list(memory_map.cpu_hardware_ids[:memory_map.cpu_count]), list(hardware_ids))
            self.assertEqual(memory_map.psci_conduit, conduit)

    def test_cpu_capacity_duplicates_and_conduit(self):
        """Reject ambiguous CPU IDs, unsupported affinity bits, and bad PSCI methods."""
        for blob in (build_tree(cpu_ids=tuple(range(9))),
                     build_tree(cpu_ids=(0, 0)),
                     build_tree(cpu_ids=(0, 0x1000000)),
                     build_tree(cpu_ids=(0, 1), psci_method="invalid")):
            self.assertFalse(self.discover(blob)[0])

    def test_reservations_and_page_rounding(self):
        """Subtract overlapping firmware reservations and protect partial pages."""
        accepted, memory_map = self.discover(build_tree(
            reservations=((0x40001001, 4096),),
            reserved_regions=((0x40002000, 8192),)))
        self.assertTrue(accepted)
        self.assertTrue(self.parser.boot_memory_finalize(ctypes.byref(memory_map)))
        self.assertEqual([(entry.base, entry.end) for entry in
                          memory_map.available[:memory_map.available_count]], [
                              (0x40000000, 0x40001000),
                              (0x40004000, 0x48000000),
                              (0x48001000, 0x50000000),
                          ])

    def test_discontiguous_and_duplicate_ram(self):
        """RAM holes remain absent and overlapping memory nodes are not double counted."""
        accepted, memory_map = self.discover(build_tree(memory_ranges=(
            (0x50000000, 0x10000), (0x40000000, 0x10000), (0x40008000, 0x10000))))
        self.assertTrue(accepted)
        self.assertTrue(self.parser.boot_memory_finalize(ctypes.byref(memory_map)))
        self.assertEqual([(entry.base, entry.end) for entry in memory_map.ram[:memory_map.ram_count]],
                         [(0x40000000, 0x40018000), (0x50000000, 0x50010000)])

    def test_disabled_reserved_region(self):
        """A disabled reserved-memory node does not remove usable RAM."""
        accepted, memory_map = self.discover(build_tree(
            reserved_regions=((0x40001000, 4096),), disabled_region=True))
        self.assertTrue(accepted)
        self.assertEqual(memory_map.reserved_count, 1)  # DTB itself only

    def test_unsupported_reservation_semantics(self):
        """Reject reservations that require allocation or bus address translation."""
        for blob in (build_tree(dynamic_reservation=True),
                     build_tree(reserved_regions=((0x40001000, 4096),), translated_reservations=True)):
            self.assertFalse(self.discover(blob)[0])

    def test_truncations(self):
        """No truncated prefix is accepted as a complete DTB."""
        blob = build_tree()
        for prefix_size in range(len(blob)):
            self.assertFalse(self.discover(blob[:prefix_size])[0])

    def test_bad_header_blocks_and_tokens(self):
        """Header corruption and out-of-range structure data fail closed."""
        blob = build_tree()
        for byte_offset, value in ((0, 0), (4, 0xffffffff), (8, 0xfffffff0),
                                   (12, 0xffffffff), (16, 41), (20, 16),
                                   (24, 18), (32, 0xffffffff), (36, 0xffffffff)):
            damaged = bytearray(blob)
            struct.pack_into(">I", damaged, byte_offset, value)
            self.assertFalse(self.discover(bytes(damaged))[0])
        damaged = bytearray(blob)
        structure_offset = struct.unpack_from(">I", blob, 8)[0]
        struct.pack_into(">I", damaged, structure_offset, 0xffffffff)
        self.assertFalse(self.discover(bytes(damaged))[0])

    def test_wrapping_ranges_and_capacity(self):
        """Wrapped addresses and excess reservations never become usable pages."""
        for blob in (build_tree(memory_ranges=((0xfffffffffffff000, 0x2000),)),
                     build_tree(reservations=tuple((0x41000000 + region_index * 4096, 4096)
                                                   for region_index in range(33)))):
            self.assertFalse(self.discover(blob)[0])

    def test_fully_reserved_ram(self):
        """A map with no usable whole pages cannot initialize an allocator."""
        accepted, memory_map = self.discover(build_tree(reservations=((0x40000000, 0x10000000),)))
        self.assertTrue(accepted)
        self.assertFalse(self.parser.boot_memory_finalize(ctypes.byref(memory_map)))


if __name__ == "__main__":
    unittest.main(verbosity=2)
