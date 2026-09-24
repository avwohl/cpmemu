#!/usr/bin/env python3
"""Unit tests for cpm_disk.py"""

import contextlib
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

from cpm_disk import (
    Hd1kDisk,
    ComboDisk,
    SssdDisk,
    create_hd1k_disk,
    create_sssd_disk,
    detect_disk_format,
    get_disk_object,
    SliceError,
    verify_disk,
    BLOCK_SIZE,
)

CPM_DISK = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cpm_disk.py')


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


def dir_entries(disk):
    """(index, raw 32 bytes) of every directory entry, for either geometry."""
    if isinstance(disk, SssdDisk):
        return [(i, bytearray(disk.read_dir_entry(i))) for i in range(disk.DIR_ENTRIES)]
    return [(i, bytearray(disk.data[disk.DIR_START + i * 32:disk.DIR_START + i * 32 + 32]))
            for i in range(disk.DIR_ENTRIES)]


class TestSssdCreate(unittest.TestCase):
    """`create --sssd` failed its own verify and so never wrote an image.

    format_sssd_disk filled 2 KB at the directory's physical offset with E5,
    but the directory is read through the sector skew: its sixteen logical
    sectors are spread over the whole of track 2, and the ones that fell
    outside that 2 KB read back as zeros - user 0, a name of NULs.
    """

    def test_a_new_sssd_image_passes_verify(self):
        data = create_sssd_disk()
        for skew in (True, False):
            with self.subTest(skew=skew):
                disk = SssdDisk(data, use_skew=skew)
                self.assertEqual(verify_disk(disk, data, 'sssd')[0], [])
                self.assertEqual(disk.list_files(), {})
                self.assertTrue(all(e[0] == 0xE5 for i, e in dir_entries(disk)))

    def test_create_sssd_writes_the_image(self):
        d = tempfile.mkdtemp()
        try:
            img = os.path.join(d, 's.img')
            p = subprocess.run([sys.executable, CPM_DISK, 'create', '--sssd', img],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               universal_newlines=True)
            self.assertEqual(p.returncode, 0, p.stdout)
            self.assertEqual(os.path.getsize(img), 256256)
        finally:
            shutil.rmtree(d)



def put_entry(disk, index, entry):
    if isinstance(disk, SssdDisk):
        disk.write_dir_entry(index, bytes(entry))
    else:
        at = disk.DIR_START + index * 32
        disk.data[at:at + 32] = entry


def set_attributes(disk, name83, positions):
    """Set the high bit of name bytes `positions` (1-11) in every entry of a
    file, the way MP/M's SET does: SET Y.COM [F1=ON] sets position 1, R/O is
    9, SYS 10 and archive 11."""
    for i, entry in dir_entries(disk):
        if entry[0] != 0xE5 and bytes(b & 0x7F for b in entry[1:12]) == name83:
            for p in positions:
                entry[p] |= 0x80
            put_entry(disk, i, entry)


class TestAttributeBits(unittest.TestCase):
    """A name's high bits are attributes, not part of the name.

    CP/M keeps f1'-f4' (user attributes, MP/M's and CP/M 3's SET [F1=ON]),
    f5'-f8' (reserved) and t1'-t3' (read-only, SYS, archive) in bit 7 of the
    eleven name bytes, and the BDOS compares names with those bits stripped.
    `delete` and `add` crashed with UnicodeDecodeError on any disk where one
    entry had a bit set in its first eight bytes - they decoded every entry
    of the user as ASCII to compare it - `extract` crashed the same way on
    hd1k, and `list` skipped the file as though it were not there.  Found by
    MP/M's SET [F1=ON] followed by cpm_disk.py delete.
    """

    X = b"hello" * 30
    Y = b"world" * 30

    def disks(self):
        for make, cls in ((lambda: create_hd1k_disk(combo=False), Hd1kDisk),
                          (create_sssd_disk, SssdDisk),
                          (lambda: create_hd1k_disk(combo=True), ComboDisk)):
            data = bytearray(make())
            disk = cls(data)
            disk.add_file("X.COM", self.X)
            disk.add_file("Y.COM", self.Y)
            set_attributes(disk, b"Y       COM", (1, 10))   # F1 and SYS
            yield cls.__name__, data, disk

    def test_list_shows_a_file_with_an_attribute_set(self):
        for fmt, data, disk in self.disks():
            with self.subTest(fmt=fmt):
                files = disk.list_files()
                self.assertIn((0, "Y.COM"), files)
                self.assertEqual(files[(0, "Y.COM")]['attrs'], ['F1', 'SYS'])
                self.assertEqual(files[(0, "X.COM")]['attrs'], [])

    def test_delete_is_not_stopped_by_another_files_attributes(self):
        for fmt, data, disk in self.disks():
            with self.subTest(fmt=fmt):
                self.assertEqual(disk.delete_file("X.COM"), 1)
                self.assertNotIn((0, "X.COM"), disk.list_files())

    def test_delete_matches_through_the_attribute_bits(self):
        for fmt, data, disk in self.disks():
            with self.subTest(fmt=fmt):
                self.assertEqual(disk.delete_file("Y.COM"), 1)
                self.assertNotIn((0, "Y.COM"), disk.list_files())

    def test_extract_matches_through_the_attribute_bits(self):
        for fmt, data, disk in self.disks():
            with self.subTest(fmt=fmt):
                self.assertEqual(bytes(disk.extract_file("Y.COM"))[:len(self.Y)], self.Y)

    def test_add_is_not_stopped_by_another_files_attributes(self):
        for fmt, data, disk in self.disks():
            with self.subTest(fmt=fmt):
                self.assertTrue(disk.add_file("Z.COM", b"Z" * 128))
                self.assertEqual(bytes(disk.extract_file("Z.COM")), b"Z" * 128)

    def test_add_replaces_a_file_whose_name_has_attributes(self):
        for fmt, data, disk in self.disks():
            with self.subTest(fmt=fmt):
                self.assertTrue(disk.add_file("Y.COM", b"N" * 128))
                self.assertEqual(bytes(disk.extract_file("Y.COM")), b"N" * 128)
                self.assertEqual(len([e for i, e in dir_entries(disk) if e[0] == 0
                                      and bytes(b & 0x7F for b in e[1:12]) == b"Y       COM"]), 1)
                self.assertEqual(verify_disk(disk, data, detect_disk_format(data))[0], [])

    def test_the_bits_on_disk_survive_other_changes(self):
        """Nothing that rewrites the directory may strip them."""
        for fmt, data, disk in self.disks():
            with self.subTest(fmt=fmt):
                def marked():
                    return [e for i, e in dir_entries(disk) if e[0] != 0xE5 and e[1] & 0x80]
                before = marked()
                self.assertEqual(len(before), 1)
                disk.delete_file("X.COM")
                disk.add_file("Z.COM", b"Z" * 128)
                self.assertEqual(marked(), before)

    def test_every_attribute_is_named(self):
        data = bytearray(create_hd1k_disk(combo=False))
        disk = Hd1kDisk(data)
        disk.add_file("A.COM", b"A")
        set_attributes(disk, b"A       COM", range(1, 12))
        self.assertEqual(disk.list_files()[(0, "A.COM")]['attrs'],
                         ['F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'R/O', 'SYS', 'ARC'])
        self.assertEqual(verify_disk(disk, data, 'hd1k')[0], [])


def each_geometry():
    """(name, image bytes, disk) for a fresh hd1k, SSSD and combo slice."""
    for make, cls in ((lambda: create_hd1k_disk(combo=False), Hd1kDisk),
                      (create_sssd_disk, SssdDisk),
                      (lambda: create_hd1k_disk(combo=True), ComboDisk)):
        data = bytearray(make())
        yield cls.__name__, data, cls(data)


class TestFreedBlocks(unittest.TestCase):
    """The blocks a delete or a replace frees are used again.

    add_file put every file in one run after the highest block in use, so a
    file deleted or replaced below another one left blocks nothing would ever
    hand out.  On a 241-block SSSD image, a 100 KB file replaced twice beside
    a 10 KB one ran out, "needs 100 blocks and only 21 are left", with 110
    blocks in use.
    """

    def test_sssd_survives_replacing_a_file_below_another(self):
        data = bytearray(create_sssd_disk())
        disk = SssdDisk(data)
        a, b = b"a" * 102400, b"b" * 10240
        for name, body in (("A.BIN", a), ("B.BIN", b), ("A.BIN", a), ("B.BIN", b),
                           ("A.BIN", a), ("B.BIN", b), ("A.BIN", a)):
            self.assertTrue(disk.add_file(name, body), name)
        self.assertEqual(bytes(disk.extract_file("A.BIN")), a)
        self.assertEqual(bytes(disk.extract_file("B.BIN")), b)
        self.assertEqual(verify_disk(disk, data, 'sssd')[0], [])

    def test_hd1k_reuses_what_a_delete_freed(self):
        for fmt, data, disk in each_geometry():
            if fmt == 'SssdDisk':
                continue
            with self.subTest(fmt=fmt):
                big = bytes(range(256)) * 19532          # 5,000,192 bytes
                self.assertTrue(disk.add_file("BIG.DAT", big))
                self.assertTrue(disk.add_file("MID.DAT", b"m" * 1000064))
                self.assertTrue(disk.add_file("BIG.DAT", big))
                self.assertEqual(bytes(disk.extract_file("BIG.DAT")), big)
                self.assertEqual(bytes(disk.extract_file("MID.DAT")), b"m" * 1000064)
                self.assertEqual(verify_disk(disk, data, detect_disk_format(data))[0], [])

    def test_a_file_that_fits_after_the_last_goes_there(self):
        """The run after the highest block is still first choice: images
        romwbw_disks rebuilds byte for byte depend on where files land."""
        for fmt, data, disk in each_geometry():
            with self.subTest(fmt=fmt):
                disk.add_file("A.COM", b"a" * 5000)
                disk.add_file("B.COM", b"b" * 5000)
                top = max(disk.list_files()[(0, "B.COM")]['blocks'])
                disk.delete_file("A.COM")
                disk.add_file("C.COM", b"c" * 5000)
                self.assertEqual(min(disk.list_files()[(0, "C.COM")]['blocks']), top + 1)

    def test_a_full_disk_says_how_much_is_free(self):
        data = bytearray(create_sssd_disk())
        disk = SssdDisk(data)
        self.assertTrue(disk.add_file("A.BIN", b"a" * 200 * 1024))
        self.assertFalse(disk.add_file("B.BIN", b"b" * 100 * 1024))


class TestFailedReplace(unittest.TestCase):
    """A replace that cannot be written leaves the file it would have
    replaced, and does not say it replaced it.

    add_file deleted the old file, printed "(replaced existing A.BIN)", and
    only then found the disk too full - "needs 150 blocks and only 141 are
    free".  The command wrote nothing back, so A.BIN survived on disk; in the
    disk object it was gone.  A directory with no room for the new file's
    extents was found later still, after the data blocks and its first
    extent were written over the old file's entry.
    """

    def add_quietly(self, disk, name, body):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            ok = disk.add_file(name, body)
        return ok, out.getvalue()

    def test_too_few_blocks(self):
        for fmt, data, disk in each_geometry():
            with self.subTest(fmt=fmt):
                self.assertTrue(disk.add_file("A.BIN", b"a" * 1024))
                ok, out = self.add_quietly(disk, "A.BIN", b"b" * len(data))
                self.assertFalse(ok)
                self.assertNotIn("replaced", out)
                self.assertIn("A.BIN it would replace, which is left as it was", out)
                self.assertEqual(bytes(disk.extract_file("A.BIN")), b"a" * 1024)
                self.assertEqual(verify_disk(disk, data, detect_disk_format(data))[0], [])

    def test_too_few_directory_entries(self):
        data = bytearray(create_sssd_disk())
        disk = SssdDisk(data)
        for i in range(disk.DIR_ENTRIES - 1):
            self.assertTrue(self.add_quietly(disk, "F%02d.DAT" % i, b"f")[0])
        self.assertTrue(disk.add_file("A.BIN", b"a" * 1024))
        # 20 blocks is two extents, and only A.BIN's one entry would be free
        ok, out = self.add_quietly(disk, "A.BIN", b"b" * 20 * 1024)
        self.assertFalse(ok)
        self.assertNotIn("replaced", out)
        self.assertIn("needs 2 directory entries and only 1 are free, counting the "
                      "directory entries of the A.BIN it would replace", out)
        self.assertEqual(bytes(disk.extract_file("A.BIN")), b"a" * 1024)
        self.assertEqual(verify_disk(disk, data, 'sssd')[0], [])

    def test_the_command_says_so(self):
        d = tempfile.mkdtemp()
        try:
            img = os.path.join(d, 's.img')
            with open(img, 'wb') as f:
                f.write(create_sssd_disk())
            for name, size in (('a.bin', 100 * 1024), ('c.bin', 40 * 1024), ('a.bin', 210 * 1024)):
                with open(os.path.join(d, name), 'wb') as f:
                    f.write(name[0].encode() * size)
                p = subprocess.run([sys.executable, CPM_DISK, 'add', img, name], cwd=d,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   universal_newlines=True)
            self.assertEqual(p.returncode, 1, p.stdout)
            self.assertNotIn("replaced", p.stdout)
            with open(img, 'rb') as f:
                disk = SssdDisk(bytearray(f.read()))
            self.assertEqual(bytes(disk.extract_file("A.BIN")), b"a" * 100 * 1024)
        finally:
            shutil.rmtree(d)


class TestEmptyFile(unittest.TestCase):
    """An empty file has a directory entry: extent 0, RC 0, no blocks.

    It had none, so an empty file added did not exist, and adding one over an
    existing file deleted that file and reported success.
    """

    def test_an_empty_file_is_listed_and_extracts_empty(self):
        for fmt, data, disk in each_geometry():
            with self.subTest(fmt=fmt):
                self.assertTrue(disk.add_file("EMPTY.TXT", b""))
                files = disk.list_files()
                self.assertIn((0, "EMPTY.TXT"), files)
                self.assertEqual(files[(0, "EMPTY.TXT")]['records'], 0)
                self.assertEqual(files[(0, "EMPTY.TXT")]['blocks'], [])
                self.assertEqual(bytes(disk.extract_file("EMPTY.TXT")), b"")
                self.assertEqual(verify_disk(disk, data, detect_disk_format(data))[0], [])

    def test_an_empty_file_replaces_rather_than_deletes(self):
        for fmt, data, disk in each_geometry():
            with self.subTest(fmt=fmt):
                disk.add_file("NOTE.TXT", b"12345678")
                self.assertTrue(disk.add_file("NOTE.TXT", b""))
                self.assertEqual(bytes(disk.extract_file("NOTE.TXT")), b"")
                entries = [e for i, e in dir_entries(disk)
                           if e[0] == 0 and bytes(e[1:12]) == b"NOTE    TXT"]
                self.assertEqual(len(entries), 1)
                self.assertEqual(entries[0][12:], bytes(20))


class TestLowerCaseNames(unittest.TestCase):
    """A name a program made with a lower-case FCB can be extracted and deleted.

    The BDOS does not fold case, and list showed such a file, but extract and
    delete upper-cased what they were given and so could never match it:
    `delete *.*` matched it by its listed name and then deleted nothing.
    """

    def disks(self):
        for fmt, data, disk in each_geometry():
            disk.add_file("LOW.TXT", b"lower" * 30)
            for i, e in dir_entries(disk):
                if e[0] == 0 and bytes(e[1:12]) == b"LOW     TXT":
                    e[1:4] = b"low"
                    put_entry(disk, i, e)
            yield fmt, data, disk

    def test_extract_and_delete_reach_it(self):
        for fmt, data, disk in self.disks():
            with self.subTest(fmt=fmt):
                self.assertIn((0, "low.TXT"), disk.list_files())
                self.assertEqual(bytes(disk.extract_file("low.txt"))[:150], b"lower" * 30)
                self.assertEqual(disk.delete_file("LOW.TXT"), 1)
                self.assertEqual(disk.list_files(), {})

    def test_an_exact_name_wins_over_a_folded_one(self):
        for fmt, data, disk in self.disks():
            with self.subTest(fmt=fmt):
                disk.add_file("LOW.TXT", b"UPPER" * 30)
                self.assertEqual(bytes(disk.extract_file("low.txt"))[:150], b"UPPER" * 30)
                self.assertEqual(disk.delete_file("low.txt"), 1)
                self.assertEqual(list(disk.list_files()), [(0, "low.TXT")])

    def test_delete_star_dot_star_deletes_it(self):
        d = tempfile.mkdtemp()
        try:
            for fmt, data, disk in self.disks():
                with self.subTest(fmt=fmt):
                    img = os.path.join(d, 'x.img')
                    with open(img, 'wb') as f:
                        f.write(data)
                    p = subprocess.run([sys.executable, CPM_DISK, 'delete', img, '*.*'],
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       universal_newlines=True)
                    self.assertEqual(p.returncode, 0, p.stdout)
                    self.assertIn('Deleted low.TXT', p.stdout)
                    with open(img, 'rb') as f:
                        after = bytearray(f.read())
                    self.assertEqual(get_disk_object(after, None).list_files(), {})
        finally:
            shutil.rmtree(d)


class TestCommandLine(unittest.TestCase):
    """The same through the commands, which is where the crash was reported."""

    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.img = os.path.join(self.dir, 'hd.img')
        data = bytearray(create_hd1k_disk(combo=False))
        disk = Hd1kDisk(data)
        disk.add_file("X.COM", b"x" * 200)
        disk.add_file("Y.COM", b"y" * 200)
        set_attributes(disk, b"Y       COM", (1, 9))   # F1 and R/O
        with open(self.img, 'wb') as f:
            f.write(data)

    def tearDown(self):
        shutil.rmtree(self.dir)

    def run_tool(self, *args):
        p = subprocess.run([sys.executable, CPM_DISK] + list(args), cwd=self.dir,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           universal_newlines=True)
        return p.returncode, p.stdout

    def test_list_reports_the_attributes_after_the_columns_it_had(self):
        rc, out = self.run_tool('list', self.img)
        self.assertEqual(rc, 0, out)
        rows = {line.split()[1]: line.split() for line in out.splitlines()[2:]}
        # romwbw_emu and romwbw_disks read the name as the second field of
        # every line after the first two, so it has to stay there.
        self.assertEqual(sorted(rows), ['X.COM', 'Y.COM'])
        self.assertEqual(rows['Y.COM'][4:], ['F1', 'R/O'])
        self.assertEqual(rows['X.COM'][4:], [])

    def test_delete_and_add_run(self):
        rc, out = self.run_tool('delete', self.img, 'X.COM')
        self.assertEqual(rc, 0, out)
        self.assertIn('Deleted X.COM', out)
        with open(os.path.join(self.dir, 'z.com'), 'wb') as f:
            f.write(b'z' * 300)
        rc, out = self.run_tool('add', self.img, 'z.com')
        self.assertEqual(rc, 0, out)
        rc, out = self.run_tool('delete', self.img, 'Y.*')
        self.assertEqual(rc, 0, out)
        self.assertIn('Deleted Y.COM', out)
        rc, out = self.run_tool('list', self.img)
        self.assertEqual([line.split()[1] for line in out.splitlines()[2:]], ['Z.COM'])

    def test_extract_runs(self):
        rc, out = self.run_tool('extract', self.img, 'Y.COM', '-o', self.dir)
        self.assertEqual(rc, 0, out)
        with open(os.path.join(self.dir, 'y.com'), 'rb') as f:
            self.assertEqual(f.read()[:200], b"y" * 200)


if __name__ == '__main__':
    unittest.main()
