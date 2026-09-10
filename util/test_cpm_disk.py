#!/usr/bin/env python3
"""Unit tests for cpm_disk.py"""

import struct
import unittest

from cpm_disk import (
    Hd1kDisk,
    ComboDisk,
    create_hd1k_disk,
    create_sssd_disk,
    detect_disk_format,
    get_disk_object,
    SliceError,
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


class TestComboSlices(unittest.TestCase):
    """--slice N: every slice of a combo, not just the first.

    A combo is six 8 MB hd1k images behind a 1 MB MBR prefix.  ComboDisk could
    only ever see the first, so slices 1-5 needed a dd cut to reach - the same
    thing cpmtools needs, though for a different reason (its libdsk backend
    cannot address past 8 MB from the start of a file at all).  Nothing here
    has that limit: the image is a bytearray and a slice is an index into it.
    """

    PAYLOAD = b"SLICE-CANARY-0123456789A" * 16   # 384 bytes = 3 records

    def test_each_slice_starts_where_the_layout_says(self):
        for n in range(ComboDisk.SLICES):
            disk = ComboDisk(bytearray(create_hd1k_disk(combo=True)), slice_num=n)
            self.assertEqual(disk.slice_start,
                             ComboDisk.PREFIX_SIZE + n * ComboDisk.SLICE_SIZE)
            self.assertEqual(disk.DIR_START,
                             disk.slice_start
                             + ComboDisk.BOOT_TRACKS * ComboDisk.TRACK_SIZE)

    def test_a_write_to_one_slice_is_invisible_in_the_others(self):
        """The property that matters: slices must not bleed into each other."""
        data = bytearray(create_hd1k_disk(combo=True))
        ComboDisk(data, slice_num=3).add_file("CANARY.DAT", self.PAYLOAD)

        self.assertIn((0, "CANARY.DAT"),
                      ComboDisk(data, slice_num=3).list_files())
        for n in range(ComboDisk.SLICES):
            if n == 3:
                continue
            self.assertNotIn((0, "CANARY.DAT"),
                             ComboDisk(data, slice_num=n).list_files(),
                             "slice %d saw slice 3's file" % n)

    def test_a_slice_reads_the_same_as_that_slice_cut_out_with_dd(self):
        data = bytearray(create_hd1k_disk(combo=True))
        ComboDisk(data, slice_num=5).add_file("CANARY.DAT", self.PAYLOAD)

        start = ComboDisk.PREFIX_SIZE + 5 * ComboDisk.SLICE_SIZE
        cut = bytearray(data[start:start + ComboDisk.SLICE_SIZE])
        plain = Hd1kDisk(cut)
        self.assertEqual(bytes(plain.extract_file("CANARY.DAT")), self.PAYLOAD)

    def test_boot_area_belongs_to_the_slice(self):
        data = bytearray(create_hd1k_disk(combo=True))
        ComboDisk(data, slice_num=2).write_boot_area(b"\xC3" + b"\x5A" * 99)
        self.assertEqual(ComboDisk(data, slice_num=2).read_boot_area()[:2],
                         b"\xC3\x5A")
        # ...and not to slice 0, which was where it always used to go.
        self.assertNotEqual(ComboDisk(data, slice_num=0).read_boot_area()[:2],
                            b"\xC3\x5A")

    def test_a_slice_outside_the_image_is_refused(self):
        data = bytearray(create_hd1k_disk(combo=True))
        for bad in (-1, ComboDisk.SLICES, 99):
            with self.assertRaises(ValueError):
                ComboDisk(data, slice_num=bad)

    def test_slice_on_a_non_combo_image_is_refused(self):
        plain = create_hd1k_disk(combo=False)
        with self.assertRaises(ValueError):
            get_disk_object(plain, slice_num=2)
        # slice 0 is the default and means "the only slice there is"
        self.assertIsInstance(get_disk_object(plain, slice_num=0), Hd1kDisk)


class TestComboGuards(unittest.TestCase):
    """The bounds and refusals, each of which a mutation test showed uncovered."""

    def test_verify_accepts_the_last_block_of_a_slice(self):
        """verify_disk uses max_block as a COUNT (block >= max_block).

        MAX_BLOCK is the last valid INDEX, so handing it over unadjusted
        condemned block 2043 - the very block find_free_block gives out on a
        full slice.  The tool's allocator and its verifier have to agree.
        """
        data = bytearray(create_hd1k_disk(combo=True))
        disk = ComboDisk(data, slice_num=1)
        disk.add_file("EDGE.DAT", b"E" * 128)
        # Point it at the last legal block of the slice.
        struct.pack_into('<H', data, disk.DIR_START + 16, ComboDisk.MAX_BLOCK)
        errors, _ = verify_disk(disk, data, 'combo')
        self.assertEqual(errors, [], "the last block of a slice must be legal")

        # ...and one past it must still be caught.
        struct.pack_into('<H', data, disk.DIR_START + 16, ComboDisk.MAX_BLOCK + 1)
        errors, _ = verify_disk(disk, data, 'combo')
        self.assertTrue(errors, "a block past the slice must be an error")

    def test_a_multi_extent_file_round_trips_on_a_combo(self):
        """The limit that is gone.

        ComboDisk used to write a single logical extent with RC capped at 128
        records, so a 24576-byte file was recorded as 16384 and read back
        truncated - with "Added BIG.DAT: 24576 bytes" printed and exit 0.  It
        was refused rather than truncated for one commit, and now it simply
        works: ComboDisk is Hd1kDisk with a base offset, and Hd1kDisk always
        handled multi-extent files.
        """
        data = bytearray(create_hd1k_disk(combo=True))
        for n, size in ((0, 128 * 128 + 128),      # just over one extent
                        (2, 24576),                # six blocks
                        (5, 102400)):              # twenty-five, several extents
            with self.subTest(slice_num=n, size=size):
                payload = (bytes(range(256)) * (size // 256 + 1))[:size]
                self.assertTrue(
                    ComboDisk(data, slice_num=n).add_file("BIG.DAT", payload))
                back = ComboDisk(data, slice_num=n).extract_file("BIG.DAT")
                self.assertEqual(bytes(back), payload)

    def test_a_file_larger_than_the_slice_is_refused(self):
        """...but the disk still ends where it ends."""
        data = bytearray(create_hd1k_disk(combo=True))
        disk = ComboDisk(data, slice_num=1)
        before = len(data)
        self.assertFalse(disk.add_file("HUGE.DAT", b"H" * (9 * 1024 * 1024)))
        self.assertNotIn((0, "HUGE.DAT"), disk.list_files())
        self.assertEqual(len(data), before, "the image must not have grown")

    def test_a_plain_image_is_bounded_too(self):
        """Hd1kDisk had no bound at all.

        `add hd.img <9MB file>` on an 8 MB image printed "Successfully updated"
        and left the file 9,486,336 bytes: a grown image whose directory points
        past where the geometry says the disk ends.
        """
        data = bytearray(create_hd1k_disk(combo=False))
        before = len(data)
        self.assertFalse(Hd1kDisk(data).add_file("HUGE.DAT", b"H" * (9 * 1024 * 1024)))
        self.assertEqual(len(data), before)

    def test_a_slice_is_an_hd1k_image_at_an_offset(self):
        """The claim the refactor rests on, asserted rather than assumed."""
        self.assertTrue(issubclass(ComboDisk, Hd1kDisk))
        for n in range(ComboDisk.SLICES):
            disk = ComboDisk(bytearray(create_hd1k_disk(combo=True)), slice_num=n)
            self.assertEqual(disk.base, disk.slice_start)
            self.assertEqual(disk.DIR_START, disk.base + Hd1kDisk.BOOT_SIZE)
            self.assertEqual(disk.MAX_BLOCK, Hd1kDisk.MAX_BLOCK)

    def test_slice_is_refused_on_every_single_slice_format(self):
        """The guard used to sit in the hd1k branch, so SSSD fell past it."""
        for make in (lambda: create_hd1k_disk(combo=False), create_sssd_disk):
            data = make()
            with self.subTest(fmt=detect_disk_format(data)):
                with self.assertRaises(SliceError):
                    get_disk_object(data, slice_num=2)
                self.assertIsNotNone(get_disk_object(data, slice_num=0))

    def test_slice_errors_do_not_hide_decode_errors(self):
        """main() catches the slice error only.

        UnicodeDecodeError is a subclass of ValueError, so a blanket catch
        would have reported a corrupt directory as a bad flag.
        """
        self.assertTrue(issubclass(SliceError, ValueError))
        self.assertFalse(issubclass(UnicodeDecodeError, SliceError))


if __name__ == '__main__':
    unittest.main()
