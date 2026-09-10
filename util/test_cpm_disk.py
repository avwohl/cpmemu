#!/usr/bin/env python3
"""Unit tests for cpm_disk.py"""

import struct
import unittest

from cpm_disk import (
    Hd1kDisk,
    ComboDisk,
    create_hd1k_disk,
    verify_disk,
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


class TestComboDataOffsets(unittest.TestCase):
    """Where a combo disk's file DATA lands, as opposed to its block numbers.

    Every test above this point checks the block NUMBER recorded in the
    directory, and all of them passed while ComboDisk read and wrote file data
    16384 bytes before where those block numbers point: the directory was
    located at PREFIX_SIZE + boot area, but the data was addressed from
    PREFIX_SIZE alone.  A listing looked perfect and `extract` returned a
    neighbouring file's bytes.  These tests are about the offset itself.
    """

    # 24 bytes x 16 = 384 = exactly 3 CP/M records, so extract_file() trims to
    # the payload with no 0x1A record padding to account for.
    PAYLOAD = b"COMBO-DATA-OFFSET-CANARY" * 16

    def test_block_data_lands_where_the_block_number_says(self):
        """Block N's bytes must be at DIR_START + N*BLOCK_SIZE, and nowhere else."""
        disk_data = create_hd1k_disk(combo=True)
        disk = ComboDisk(disk_data)
        disk.add_file("CANARY.DAT", self.PAYLOAD)

        block = disk.list_files()[(0, "CANARY.DAT")]['blocks'][0]
        at = disk.DIR_START + (block * BLOCK_SIZE)
        self.assertEqual(bytes(disk_data[at:at + len(self.PAYLOAD)]), self.PAYLOAD)

        # And specifically NOT at the pre-fix offset, one boot area earlier.
        was = disk.PREFIX_SIZE + (block * BLOCK_SIZE)
        self.assertNotEqual(was, at)
        self.assertNotEqual(bytes(disk_data[was:was + len(self.PAYLOAD)]), self.PAYLOAD)

    def test_read_back_matches_what_was_written(self):
        """extract must return the bytes add put in - the whole point."""
        disk_data = create_hd1k_disk(combo=True)
        disk = ComboDisk(disk_data)
        disk.add_file("CANARY.DAT", self.PAYLOAD)
        self.assertEqual(bytes(disk.extract_file("CANARY.DAT")), self.PAYLOAD)

    def test_combo_slice_0_reads_the_same_as_the_plain_slice_cut_out_of_it(self):
        """The invariant that broke.

        Slice 0 of a combo IS a plain hd1k image with a 1MB prefix in front of
        it.  Cutting it out and reading it as an Hd1kDisk must give the same
        answer as reading the combo whole; that equality is what failed, in
        silence, on every published combo image.
        """
        disk_data = create_hd1k_disk(combo=True)
        disk = ComboDisk(disk_data)
        disk.add_file("CANARY.DAT", self.PAYLOAD)

        cut = bytearray(disk_data[ComboDisk.PREFIX_SIZE:
                                  ComboDisk.PREFIX_SIZE + ComboDisk.SLICE_SIZE])
        plain = Hd1kDisk(cut)
        self.assertIn((0, "CANARY.DAT"), plain.list_files())
        self.assertEqual(bytes(plain.extract_file("CANARY.DAT")), self.PAYLOAD)

    def test_dir_start_is_the_prefix_plus_the_boot_area(self):
        disk = ComboDisk(bytearray(create_hd1k_disk(combo=True)))
        self.assertEqual(disk.DIR_START,
                         ComboDisk.PREFIX_SIZE
                         + ComboDisk.BOOT_TRACKS * ComboDisk.TRACK_SIZE)
        self.assertEqual(disk.dir_offset, disk.DIR_START)

    def test_allocation_cannot_reach_slice_1(self):
        """The last addressable block must end exactly on the slice boundary."""
        end_of_last_block = (ComboDisk.DIR_START
                             + (ComboDisk.MAX_BLOCK + 1) * BLOCK_SIZE)
        self.assertEqual(end_of_last_block,
                         ComboDisk.PREFIX_SIZE + ComboDisk.SLICE_SIZE)

    def test_verify_disk_can_read_a_combo(self):
        """verify_disk() reads disk.DIR_START by name, for any disk object.

        ComboDisk exposed only `dir_offset`, so `add` wrote the file, then died
        in the verify step with AttributeError and never saved. The crash is
        what kept the bad offset from reaching anyone's disk.
        """
        disk_data = create_hd1k_disk(combo=True)
        disk = ComboDisk(disk_data)
        disk.add_file("CANARY.DAT", self.PAYLOAD)
        errors, warnings = verify_disk(disk, disk_data, 'combo')
        self.assertEqual(errors, [])


if __name__ == '__main__':
    unittest.main()
