#!/usr/bin/env python3
"""Unit tests for cpm_disk.py"""

import struct
import unittest

from cpm_disk import (
    Hd1kDisk,
    ComboDisk,
    create_hd1k_disk,
    BLOCK_SIZE,
)


class TestDiskAllocation(unittest.TestCase):
    """Tests for disk block allocation."""

    def test_hd1k_empty_disk_starts_at_block_8(self):
        """New files on empty hd1k disk should start at block 8 (after directory)."""
        disk_data = create_hd1k_disk(combo=False)
        disk = Hd1kDisk(disk_data)

        # Empty disk should report max block as 7 (directory uses 0-7)
        self.assertEqual(disk.find_max_block(), 7)

        # Add a small file
        disk.add_file("TEST.COM", b"Hello")

        # File should be allocated at block 8
        files = disk.list_files()
        self.assertEqual(len(files), 1)
        key = (0, "TEST.COM")
        self.assertIn(key, files)
        self.assertEqual(files[key]['blocks'], [8])

    def test_hd1k_directory_entry_block_pointer(self):
        """Verify the raw directory entry has correct block pointer."""
        disk_data = create_hd1k_disk(combo=False)
        disk = Hd1kDisk(disk_data)

        disk.add_file("TEST.COM", b"Hello")

        # Read block pointer from directory entry (offset 16-17, little-endian)
        dir_entry = disk_data[disk.DIR_START:disk.DIR_START + 32]
        block_ptr = struct.unpack('<H', dir_entry[16:18])[0]
        self.assertEqual(block_ptr, 8)

    def test_hd1k_sequential_allocation(self):
        """Multiple files should be allocated sequentially starting at block 8."""
        disk_data = create_hd1k_disk(combo=False)
        disk = Hd1kDisk(disk_data)

        # Add three small files (each fits in 1 block)
        disk.add_file("FILE1.COM", b"A" * 100)
        disk.add_file("FILE2.COM", b"B" * 100)
        disk.add_file("FILE3.COM", b"C" * 100)

        files = disk.list_files()
        self.assertEqual(files[(0, "FILE1.COM")]['blocks'], [8])
        self.assertEqual(files[(0, "FILE2.COM")]['blocks'], [9])
        self.assertEqual(files[(0, "FILE3.COM")]['blocks'], [10])

    def test_hd1k_multiblock_file(self):
        """Large file should span multiple blocks starting at block 8."""
        disk_data = create_hd1k_disk(combo=False)
        disk = Hd1kDisk(disk_data)

        # File that needs 3 blocks (each block is 4KB)
        large_data = b"X" * (BLOCK_SIZE * 2 + 100)
        disk.add_file("BIG.DAT", large_data)

        files = disk.list_files()
        self.assertEqual(files[(0, "BIG.DAT")]['blocks'], [8, 9, 10])

    def test_combo_empty_disk_starts_at_block_8(self):
        """New files on empty combo disk should start at block 8."""
        disk_data = create_hd1k_disk(combo=True)
        disk = ComboDisk(disk_data)

        # get_used_blocks should return blocks 0-7 on empty disk
        used = disk.get_used_blocks()
        self.assertEqual(used, set(range(8)))

        # Add a small file
        disk.add_file("TEST.COM", b"Hello")

        # File should be allocated at block 8
        files = disk.list_files()
        key = (0, "TEST.COM")
        self.assertIn(key, files)
        self.assertEqual(files[key]['blocks'], [8])

    def test_combo_directory_entry_block_pointer(self):
        """Verify combo disk raw directory entry has correct block pointer."""
        disk_data = create_hd1k_disk(combo=True)
        disk = ComboDisk(disk_data)

        disk.add_file("TEST.COM", b"Hello")

        # Read block pointer from directory entry
        dir_entry = disk_data[disk.dir_offset:disk.dir_offset + 32]
        block_ptr = struct.unpack('<H', dir_entry[16:18])[0]
        self.assertEqual(block_ptr, 8)

    def test_combo_sequential_allocation(self):
        """Multiple files on combo disk should be allocated sequentially."""
        disk_data = create_hd1k_disk(combo=True)
        disk = ComboDisk(disk_data)

        disk.add_file("FILE1.COM", b"A" * 100)
        disk.add_file("FILE2.COM", b"B" * 100)
        disk.add_file("FILE3.COM", b"C" * 100)

        files = disk.list_files()
        self.assertEqual(files[(0, "FILE1.COM")]['blocks'], [8])
        self.assertEqual(files[(0, "FILE2.COM")]['blocks'], [9])
        self.assertEqual(files[(0, "FILE3.COM")]['blocks'], [10])


if __name__ == '__main__':
    unittest.main()
