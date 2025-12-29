#!/usr/bin/env python3
"""CP/M disk image utility supporting multiple disk formats.

Supported formats:
  - sssd:  8" SSSD floppy (ibm-3740 compatible, 250KB)
  - hd1k:  Standard RomWBW hd1k format (8MB single slice)
  - combo: Combo disk with 1MB MBR prefix + 6x8MB slices (51MB total)

Usage:
  cpm_disk.py create <disk.img>                    # Create 8MB hd1k disk
  cpm_disk.py create --sssd <disk.img>             # Create 250KB SSSD floppy
  cpm_disk.py create --combo <disk.img>            # Create 51MB combo disk
  cpm_disk.py add <disk.img> <file1.com> [...]     # Add files to disk
  cpm_disk.py list <disk.img>                      # List files in disk
  cpm_disk.py delete <disk.img> <file1.com> [...]  # Delete files from disk
  cpm_disk.py extract <disk.img> <file1.com> [...] # Extract files from disk

Format is auto-detected for existing disks based on file size.
"""

import sys
import os
import struct
import argparse

# Common CP/M constants
SECTOR_SIZE_HD = 512    # hd1k sector size
SECTOR_SIZE_SSSD = 128  # SSSD sector size

# ibm-3740 (8" SSSD) format constants
SSSD_SECTOR_SIZE = 128
SSSD_SECTORS_PER_TRACK = 26
SSSD_TRACKS = 77
SSSD_BLOCK_SIZE = 1024   # 1KB blocks
SSSD_DIR_ENTRIES = 64
SSSD_BOOT_TRACKS = 2
SSSD_SIZE = SSSD_TRACKS * SSSD_SECTORS_PER_TRACK * SSSD_SECTOR_SIZE  # 256,256 bytes
SSSD_DIR_START = SSSD_BOOT_TRACKS * SSSD_SECTORS_PER_TRACK * SSSD_SECTOR_SIZE  # 6656 bytes

# hd1k format constants
BLOCK_SIZE = 4096  # 4KB blocks for hd1k


def cpm_pattern_to_83(pattern):
    """Convert a CP/M wildcard pattern to 8.3 format with ? expansion.

    In CP/M, * fills the rest of the field with ?, and ? matches any char.
    E.g., "*.COM" -> "????????COM", "A*.*" -> "A???????????"
    """
    pattern = pattern.upper()
    if '.' in pattern:
        name, ext = pattern.rsplit('.', 1)
    else:
        name, ext = pattern, ''

    # Expand * to fill rest of field with ?
    if '*' in name:
        idx = name.index('*')
        name = name[:idx] + '?' * (8 - idx)
    name = name[:8].ljust(8, ' ')

    if '*' in ext:
        idx = ext.index('*')
        ext = ext[:idx] + '?' * (3 - idx)
    ext = ext[:3].ljust(3, ' ')

    return name + ext


def cpm_match(pattern_83, filename_83):
    """Match a filename against a CP/M pattern (both in 8.3 format).

    ? matches any character, other characters must match exactly.
    """
    if len(pattern_83) != 11 or len(filename_83) != 11:
        return False
    for p, f in zip(pattern_83, filename_83):
        if p != '?' and p != f:
            return False
    return True

# Disk format sizes
HD1K_SINGLE_SIZE = 8388608      # 8 MB
HD1K_SLICE_SIZE = 8388608       # 8 MB per slice
HD1K_MBR_PREFIX = 1048576       # 1 MB MBR prefix for combo
HD1K_COMBO_SLICES = 6           # 6 slices in combo disk
HD1K_COMBO_SIZE = HD1K_MBR_PREFIX + (HD1K_COMBO_SLICES * HD1K_SLICE_SIZE)  # ~51 MB


def format_sssd_disk(data):
    """Format an SSSD (ibm-3740) disk with empty CP/M directory.

    ibm-3740 format:
    - 128 bytes/sector, 26 sectors/track, 77 tracks
    - Block size: 1024 bytes (8 sectors)
    - Boot tracks: 2 (reserved)
    - Directory: 64 entries x 32 bytes = 2KB = 2 blocks

    Directory starts at track 2, sector 0
    """
    dir_size = SSSD_DIR_ENTRIES * 32

    # Initialize directory with 0xE5 (CP/M empty directory marker)
    data[SSSD_DIR_START:SSSD_DIR_START + dir_size] = bytes([0xE5] * dir_size)


def create_sssd_disk():
    """Create a new formatted SSSD (ibm-3740) disk image in memory.

    Returns:
        bytearray containing the formatted disk image
    """
    data = bytearray(SSSD_SIZE)
    format_sssd_disk(data)
    return data


def format_hd1k_slice(data, offset):
    """Format a single hd1k slice with empty CP/M directory.

    hd1k format:
    - 512 bytes/sector, 16 sectors/track, 1024 tracks
    - Block size: 4096 bytes (8 sectors)
    - Boot tracks: 2 (reserved)
    - Directory: 1024 entries x 32 bytes = 32KB = 8 blocks

    Directory starts at track 2, sector 0
    """
    BOOT_TRACKS = 2
    SECTORS_PER_TRACK = 16
    DIR_ENTRIES = 1024
    DIR_ENTRY_SIZE = 32

    dir_offset = offset + (BOOT_TRACKS * SECTORS_PER_TRACK * SECTOR_SIZE_HD)
    dir_size = DIR_ENTRIES * DIR_ENTRY_SIZE

    # Initialize directory with 0xE5 (CP/M empty directory marker)
    if dir_offset + dir_size <= len(data):
        data[dir_offset:dir_offset + dir_size] = bytes([0xE5] * dir_size)


def create_combo_mbr(data):
    """Create MBR for combo disk with RomWBW partition type.

    MBR structure:
    - Offset 0x1BE: First partition entry (16 bytes)
    - Offset 0x1FE: Signature 0x55AA
    """
    PARTITION_START_LBA = 2048  # 1MB / 512 = 2048 sectors
    PARTITION_SIZE_LBA = (HD1K_COMBO_SIZE - HD1K_MBR_PREFIX) // 512

    # Partition entry at offset 0x1BE
    part_offset = 0x1BE
    data[part_offset + 0] = 0x00   # Not bootable
    data[part_offset + 1] = 0x01   # CHS start head
    data[part_offset + 2] = 0x01   # CHS start sector
    data[part_offset + 3] = 0x00   # CHS start cylinder
    data[part_offset + 4] = 0x2E   # Partition type: RomWBW hd1k
    data[part_offset + 5] = 0xFE   # CHS end head
    data[part_offset + 6] = 0xFF   # CHS end sector
    data[part_offset + 7] = 0xFF   # CHS end cylinder

    # LBA start (little-endian)
    struct.pack_into('<I', data, part_offset + 8, PARTITION_START_LBA)
    # LBA size (little-endian)
    struct.pack_into('<I', data, part_offset + 12, PARTITION_SIZE_LBA)

    # MBR signature
    data[0x1FE] = 0x55
    data[0x1FF] = 0xAA


def create_hd1k_disk(combo=False):
    """Create a new formatted hd1k disk image in memory.

    Args:
        combo: If True, create combo disk (51MB), otherwise single slice (8MB)

    Returns:
        bytearray containing the formatted disk image
    """
    if combo:
        size = HD1K_COMBO_SIZE
    else:
        size = HD1K_SINGLE_SIZE

    # Allocate and zero-fill
    data = bytearray(size)

    if not combo:
        # Single slice: format the entire disk
        format_hd1k_slice(data, 0)
    else:
        # Combo disk: create MBR and format each slice
        create_combo_mbr(data)

        # Format each slice (starts after 1MB MBR prefix)
        for slice_num in range(HD1K_COMBO_SLICES):
            slice_offset = HD1K_MBR_PREFIX + (slice_num * HD1K_SLICE_SIZE)
            format_hd1k_slice(data, slice_offset)

    return data


class SssdDisk:
    """SSSD (ibm-3740) 8" floppy disk format."""

    SECTOR_SIZE = SSSD_SECTOR_SIZE  # 128 bytes
    SECTORS_PER_TRACK = SSSD_SECTORS_PER_TRACK  # 26
    BLOCK_SIZE = SSSD_BLOCK_SIZE  # 1024 bytes
    DIR_ENTRIES = SSSD_DIR_ENTRIES  # 64
    BOOT_TRACKS = SSSD_BOOT_TRACKS  # 2
    DIR_START = SSSD_DIR_START  # 6656 bytes (2 tracks * 26 sectors * 128 bytes)
    # With 1KB blocks and DSM < 256, block pointers are 8-bit
    # EXM = 0 for 1KB blocks, so each extent = 128 records = 16KB = 16 blocks
    BLOCKS_PER_EXTENT = 16
    RECORDS_PER_BLOCK = BLOCK_SIZE // 128  # 8 records per 1KB block

    def __init__(self, disk_data):
        self.data = disk_data

    def find_free_dir_entry(self):
        """Find first free directory entry (starts with 0xE5)."""
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            if self.data[offset] == 0xE5:
                return offset
        return None

    def find_max_block(self):
        """Find highest used block number in directory.

        Returns the highest block number in use, or 1 if no files exist
        (blocks 0-1 are reserved for directory with 1KB blocks, 64 entries).
        """
        # Directory = 64 entries * 32 bytes = 2048 bytes = 2 blocks
        max_block = 1
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            if self.data[offset] != 0xE5:
                # 8-bit block pointers for SSSD (16 pointers per entry)
                for j in range(16):
                    block = self.data[offset + 16 + j]
                    if block > max_block and block != 0:
                        max_block = block
        return max_block

    def add_file(self, filename, file_data, sys_attr=False, user=0):
        """Add a file to the disk image.

        Args:
            filename: Name of the file to add
            file_data: File contents as bytes
            sys_attr: If True, set the SYS attribute
            user: User number (0-15)
        """
        # Parse filename (8.3 format)
        name, ext = os.path.splitext(filename.upper())
        name = name[:8].ljust(8)
        ext = ext[1:4].ljust(3) if ext else '   '

        num_records = (len(file_data) + 127) // 128
        blocks_needed = (num_records + self.RECORDS_PER_BLOCK - 1) // self.RECORDS_PER_BLOCK

        next_block = self.find_max_block() + 1

        sys_flag = " [SYS]" if sys_attr else ""
        user_flag = f" [U{user}]" if user != 0 else ""
        print(f"Adding {filename}{sys_flag}{user_flag}: {len(file_data)} bytes, {num_records} records, {blocks_needed} blocks starting at {next_block}")

        # Prepare extension bytes with optional SYS attribute
        ext_bytes = ext.encode('ascii')
        if sys_attr:
            ext_bytes = bytes([ext_bytes[0], ext_bytes[1] | 0x80, ext_bytes[2]])

        # Write file data to blocks first
        for i in range(blocks_needed):
            block_num = next_block + i
            block_offset = self.DIR_START + (block_num * self.BLOCK_SIZE)
            data_offset = i * self.BLOCK_SIZE
            chunk = file_data[data_offset:data_offset + self.BLOCK_SIZE]
            if len(chunk) < self.BLOCK_SIZE:
                chunk = chunk + bytes([0x1A] * (self.BLOCK_SIZE - len(chunk)))
            self.data[block_offset:block_offset + self.BLOCK_SIZE] = chunk

        # Create directory entries
        # For SSSD: EXM=0, each extent = 128 records, 16 blocks max per entry
        extent_num = 0
        block_idx = 0

        while block_idx < blocks_needed:
            dir_offset = self.find_free_dir_entry()
            if dir_offset is None:
                print(f"No free directory entry for {filename} extent {extent_num}")
                return False

            entry = bytearray(32)
            entry[0] = user  # User number
            entry[1:9] = name.encode('ascii')
            entry[9:12] = ext_bytes

            # Get blocks for this extent (up to 16 for 8-bit pointers)
            extent_blocks = []
            for i in range(self.BLOCKS_PER_EXTENT):
                if block_idx + i < blocks_needed:
                    extent_blocks.append(next_block + block_idx + i)

            # Calculate record count for this extent
            if block_idx + len(extent_blocks) >= blocks_needed:
                # Last extent - calculate remaining records
                remaining_bytes = len(file_data) - (block_idx * self.BLOCK_SIZE)
                extent_records = (remaining_bytes + 127) // 128
            else:
                # Full extent
                extent_records = 128

            entry[12] = extent_num & 0x1F  # Extent low
            entry[13] = 0  # S1
            entry[14] = (extent_num >> 5) & 0x3F  # S2/EH
            entry[15] = min(extent_records, 128)

            # Store 8-bit block pointers
            for i, block in enumerate(extent_blocks):
                entry[16 + i] = block & 0xFF

            self.data[dir_offset:dir_offset + 32] = entry

            block_idx += len(extent_blocks)
            extent_num += 1

        return True

    def list_files(self):
        """List all files in the directory."""
        files = {}
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            user = self.data[offset]
            if user != 0xE5 and user < 32:
                # Mask off attribute bits from extension bytes
                name_bytes = self.data[offset + 1:offset + 9]
                ext_bytes = bytes([b & 0x7F for b in self.data[offset + 9:offset + 12]])
                if not all(0x20 <= b <= 0x7E for b in name_bytes):
                    continue
                if not all(0x20 <= b <= 0x7E for b in ext_bytes):
                    continue

                name = name_bytes.decode('ascii').rstrip()
                ext = ext_bytes.decode('ascii').rstrip()
                extent_lo = self.data[offset + 12]
                extent_hi = self.data[offset + 14]
                extent = extent_lo + (extent_hi << 5)
                records = self.data[offset + 15]

                fullname = f"{name}.{ext}" if ext else name
                key = (user, fullname)

                if key not in files:
                    files[key] = {'extents': 0, 'records': 0, 'blocks': []}

                files[key]['extents'] = max(files[key]['extents'], extent + 1)
                if extent == files[key]['extents'] - 1:
                    files[key]['records'] = extent * 128 + records

                # 8-bit block pointers
                for j in range(16):
                    block = self.data[offset + 16 + j]
                    if block > 0:
                        files[key]['blocks'].append(block)

        return files

    def delete_file(self, filename, user=0):
        """Delete a file from the disk image."""
        name, ext = os.path.splitext(filename.upper())
        name = name[:8].ljust(8)
        ext = ext[1:4].ljust(3) if ext else '   '

        deleted_count = 0
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            entry_user = self.data[offset]
            if entry_user == user:
                entry_name = bytes(self.data[offset + 1:offset + 9]).decode('ascii')
                entry_ext_bytes = bytes([b & 0x7F for b in self.data[offset + 9:offset + 12]])
                entry_ext = entry_ext_bytes.decode('ascii')
                if entry_name == name and entry_ext == ext:
                    self.data[offset] = 0xE5
                    deleted_count += 1

        return deleted_count

    def extract_file(self, filename, user=0):
        """Extract a file from the disk image.

        Returns the file data as bytes, or None if not found.
        """
        name, ext = os.path.splitext(filename.upper())
        name = name[:8].ljust(8)
        ext = ext[1:4].ljust(3) if ext else '   '

        # Collect all extents for this file
        extents = {}
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            entry_user = self.data[offset]
            if entry_user == user:
                # Mask off attribute bits from name bytes too (high bit can be set)
                entry_name_bytes = bytes([b & 0x7F for b in self.data[offset + 1:offset + 9]])
                entry_ext_bytes = bytes([b & 0x7F for b in self.data[offset + 9:offset + 12]])
                try:
                    entry_name = entry_name_bytes.decode('ascii')
                    entry_ext = entry_ext_bytes.decode('ascii')
                except UnicodeDecodeError:
                    continue
                if entry_name == name and entry_ext == ext:
                    extent_lo = self.data[offset + 12]
                    extent_hi = self.data[offset + 14]
                    extent_num = extent_lo + (extent_hi << 5)
                    records = self.data[offset + 15]
                    blocks = []
                    # 8-bit block pointers
                    for j in range(16):
                        block = self.data[offset + 16 + j]
                        if block > 0:
                            blocks.append(block)
                    extents[extent_num] = (records, blocks)

        if not extents:
            return None

        # Read data from blocks in extent order
        file_data = bytearray()
        for ext_num in sorted(extents.keys()):
            records, blocks = extents[ext_num]
            for block in blocks:
                block_offset = self.DIR_START + (block * self.BLOCK_SIZE)
                file_data.extend(self.data[block_offset:block_offset + self.BLOCK_SIZE])

        # Trim to actual size
        last_ext = max(extents.keys())
        total_records = last_ext * 128 + extents[last_ext][0]
        actual_size = total_records * 128
        return bytes(file_data[:actual_size])


class Hd1kDisk:
    """Standard hd1k disk format (RomWBW compatible)."""

    SECTORS_PER_TRACK = 16
    DIR_ENTRIES = 1024
    BOOT_TRACKS = 2
    DIR_START = BOOT_TRACKS * SECTORS_PER_TRACK * SECTOR_SIZE_HD  # 0x4000 (16KB)

    def __init__(self, disk_data):
        self.data = disk_data

    def find_free_dir_entry(self):
        """Find first free directory entry (starts with 0xE5)."""
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            if self.data[offset] == 0xE5:
                return offset
        return None

    def find_max_block(self):
        """Find highest used block number in directory.

        Returns the highest block number in use, or 7 if no files exist
        (blocks 0-7 are reserved for directory).
        """
        # Blocks 0-7 are reserved for directory (32KB = 8 blocks at 4KB each)
        max_block = 7
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            if self.data[offset] != 0xE5:
                for j in range(8):
                    block = struct.unpack('<H', self.data[offset+16+j*2:offset+18+j*2])[0]
                    if block > max_block and block < 0xFFFF:
                        max_block = block
        return max_block

    def add_file(self, filename, file_data, sys_attr=False, user=0):
        """Add a file to the disk image.

        Args:
            filename: Name of the file to add
            file_data: File contents as bytes
            sys_attr: If True, set the SYS attribute (makes file visible from any user area)
            user: User number (0-15)
        """
        # Parse filename (8.3 format)
        name, ext = os.path.splitext(filename.upper())
        name = name[:8].ljust(8)
        ext = ext[1:4].ljust(3) if ext else '   '

        num_records = (len(file_data) + 127) // 128
        records_per_block = BLOCK_SIZE // 128  # 32 records per 4KB block
        blocks_needed = (num_records + records_per_block - 1) // records_per_block

        next_block = self.find_max_block() + 1

        sys_flag = " [SYS]" if sys_attr else ""
        user_flag = f" [U{user}]" if user != 0 else ""
        print(f"Adding {filename}{sys_flag}{user_flag}: {len(file_data)} bytes, {num_records} records, {blocks_needed} blocks starting at {next_block}")

        # Prepare extension bytes with optional SYS attribute
        ext_bytes = ext.encode('ascii')
        if sys_attr:
            ext_bytes = bytes([ext_bytes[0], ext_bytes[1] | 0x80, ext_bytes[2]])

        # Write file data to blocks first
        for i in range(blocks_needed):
            block_num = next_block + i
            block_offset = self.DIR_START + (block_num * BLOCK_SIZE)
            data_offset = i * BLOCK_SIZE
            chunk = file_data[data_offset:data_offset + BLOCK_SIZE]
            if len(chunk) < BLOCK_SIZE:
                chunk = chunk + bytes([0x1A] * (BLOCK_SIZE - len(chunk)))
            self.data[block_offset:block_offset + BLOCK_SIZE] = chunk

        # Create directory entries
        # For hd1k with 4KB blocks and DSM > 255: EXM=1
        # Each physical directory entry covers 2 logical extents = 256 records = 8 blocks
        # EL field contains the last logical extent number within this physical extent
        # RC field contains the record count in that last logical extent
        exm = 1  # EXM=1 for hd1k format (4KB blocks, DSM > 255)
        records_per_physical_extent = 128 * (exm + 1)  # 256 records
        blocks_per_physical_extent = 8  # 8 block pointers per directory entry
        physical_extent_num = 0
        block_idx = 0

        while block_idx < blocks_needed:
            dir_offset = self.find_free_dir_entry()
            if dir_offset is None:
                print(f"No free directory entry for {filename} extent {physical_extent_num}")
                return False

            entry = bytearray(32)
            entry[0] = user  # User number
            entry[1:9] = name.encode('ascii')
            entry[9:12] = ext_bytes

            # Get blocks for this physical extent
            extent_blocks = []
            for i in range(blocks_per_physical_extent):
                if block_idx + i < blocks_needed:
                    extent_blocks.append(next_block + block_idx + i)

            # Calculate which logical extent this ends on and the record count
            records_before = block_idx * records_per_block
            records_in_extent = len(extent_blocks) * records_per_block
            records_covered = min(records_before + records_in_extent, num_records)
            records_in_last_logical = ((records_covered - 1) % 128) + 1 if records_covered > 0 else 0

            # Logical extent number within this physical extent (0 or 1 for EXM=1)
            if records_covered > records_before + 128:
                last_logical_extent = 1  # Extends into second logical extent
            else:
                last_logical_extent = 0

            # Full extent number = physical_extent * (exm+1) + last_logical_extent
            full_extent_num = physical_extent_num * (exm + 1) + last_logical_extent

            entry[12] = full_extent_num & 0x1F  # Extent low (bits 0-4)
            entry[13] = 0  # S1
            entry[14] = (full_extent_num >> 5) & 0x3F  # S2/EH (bits 5-10)
            entry[15] = records_in_last_logical  # RC = records in last logical extent

            # Store block pointers
            for i, block in enumerate(extent_blocks):
                struct.pack_into('<H', entry, 16 + i*2, block)

            self.data[dir_offset:dir_offset+32] = entry

            block_idx += len(extent_blocks)
            physical_extent_num += 1

        return True

    def list_files(self):
        """List all files in the directory."""
        files = {}
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            user = self.data[offset]
            if user != 0xE5 and user < 32:
                # Validate filename - must be printable ASCII (0x20-0x7E)
                # Mask off attribute bits (high bit) from extension bytes
                name_bytes = self.data[offset+1:offset+9]
                ext_bytes = bytes([b & 0x7F for b in self.data[offset+9:offset+12]])
                if not all(0x20 <= b <= 0x7E for b in name_bytes):
                    continue
                if not all(0x20 <= b <= 0x7E for b in ext_bytes):
                    continue

                name = name_bytes.decode('ascii').rstrip()
                ext = ext_bytes.decode('ascii').rstrip()
                extent_lo = self.data[offset+12]
                extent_hi = self.data[offset+14]
                extent = extent_lo + (extent_hi << 5)
                records = self.data[offset+15]

                fullname = f"{name}.{ext}" if ext else name
                key = (user, fullname)

                if key not in files:
                    files[key] = {'extents': 0, 'records': 0, 'blocks': []}

                files[key]['extents'] = max(files[key]['extents'], extent + 1)
                if extent == files[key]['extents'] - 1:
                    files[key]['records'] = extent * 128 + records

                for j in range(8):
                    block = struct.unpack('<H', self.data[offset+16+j*2:offset+18+j*2])[0]
                    if block > 0:
                        files[key]['blocks'].append(block)

        return files

    def delete_file(self, filename, user=0):
        """Delete a file from the disk image by marking its directory entries as empty."""
        # Parse filename (8.3 format)
        name, ext = os.path.splitext(filename.upper())
        name = name[:8].ljust(8)
        ext = ext[1:4].ljust(3) if ext else '   '

        deleted_count = 0
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            entry_user = self.data[offset]
            if entry_user == user:
                entry_name = bytes(self.data[offset+1:offset+9]).decode('ascii')
                # Mask off attribute bits (high bit) from extension bytes
                entry_ext_bytes = bytes([b & 0x7F for b in self.data[offset+9:offset+12]])
                entry_ext = entry_ext_bytes.decode('ascii')
                if entry_name == name and entry_ext == ext:
                    # Mark entry as deleted
                    self.data[offset] = 0xE5
                    deleted_count += 1

        return deleted_count

    def extract_file(self, filename, user=0):
        """Extract a file from the disk image.

        Returns the file data as bytes, or None if not found.
        """
        # Parse filename (8.3 format)
        name, ext = os.path.splitext(filename.upper())
        name = name[:8].ljust(8)
        ext = ext[1:4].ljust(3) if ext else '   '

        # Collect all extents for this file
        extents = {}  # extent_num -> (records, blocks)
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            entry_user = self.data[offset]
            if entry_user == user:
                entry_name = bytes(self.data[offset+1:offset+9]).decode('ascii')
                # Mask off attribute bits (high bit) from extension bytes
                entry_ext_bytes = bytes([b & 0x7F for b in self.data[offset+9:offset+12]])
                entry_ext = entry_ext_bytes.decode('ascii')
                if entry_name == name and entry_ext == ext:
                    extent_lo = self.data[offset+12]
                    extent_hi = self.data[offset+14]
                    extent_num = extent_lo + (extent_hi << 5)
                    records = self.data[offset+15]
                    blocks = []
                    for j in range(8):
                        block = struct.unpack('<H', self.data[offset+16+j*2:offset+18+j*2])[0]
                        if block > 0:
                            blocks.append(block)
                    extents[extent_num] = (records, blocks)

        if not extents:
            return None

        # Read data from blocks in extent order
        file_data = bytearray()
        for ext_num in sorted(extents.keys()):
            records, blocks = extents[ext_num]
            for block in blocks:
                block_offset = self.DIR_START + (block * BLOCK_SIZE)
                file_data.extend(self.data[block_offset:block_offset + BLOCK_SIZE])

        # Trim to actual size based on last extent's record count
        last_ext = max(extents.keys())
        total_records = last_ext * 128 + extents[last_ext][0]
        actual_size = total_records * 128
        return bytes(file_data[:actual_size])


class ComboDisk:
    """Combo disk with 1MB prefix."""

    SECTORS_PER_TRACK = 16
    TRACK_SIZE = SECTOR_SIZE_HD * SECTORS_PER_TRACK
    DIR_ENTRIES = 1024
    BOOT_TRACKS = 2
    PREFIX_SIZE = 1048576  # 1MB
    SLICE_SIZE = 8388608   # 8MB

    def __init__(self, disk_data):
        self.data = disk_data
        self.dir_offset = self.PREFIX_SIZE + (self.BOOT_TRACKS * self.TRACK_SIZE)

    def find_free_dir_entry(self):
        """Find first free directory entry (starts with 0xE5)."""
        for i in range(self.DIR_ENTRIES):
            entry_offset = self.dir_offset + (i * 32)
            if self.data[entry_offset] == 0xE5:
                return i
        return -1

    def get_used_blocks(self):
        """Scan directory to find all used blocks."""
        used = set(range(8))  # Directory blocks are always used
        for i in range(self.DIR_ENTRIES):
            entry_offset = self.dir_offset + (i * 32)
            user = self.data[entry_offset]
            if user != 0xE5 and user < 32:
                for j in range(8):
                    ptr_offset = entry_offset + 16 + (j * 2)
                    block = struct.unpack('<H', self.data[ptr_offset:ptr_offset+2])[0]
                    if block != 0:
                        used.add(block)
        return used

    def find_free_block(self, used_blocks):
        """Find first free block (skip blocks 0-7 used by directory)."""
        for block in range(8, 2048):
            if block not in used_blocks:
                return block
        return -1

    def add_file(self, filename, file_data, user=0, sys_attr=False):
        """Add a file to the disk image.

        Args:
            filename: Name of the file to add
            file_data: File contents as bytes
            user: User number (0-15)
            sys_attr: If True, set the SYS attribute (makes file visible from any user area)
        """
        name, ext = os.path.splitext(filename.upper())
        name = name[:8].ljust(8)
        ext = ext[1:4].ljust(3) if ext else '   '

        num_records = (len(file_data) + 127) // 128
        num_blocks = (len(file_data) + BLOCK_SIZE - 1) // BLOCK_SIZE

        used_blocks = self.get_used_blocks()

        allocated_blocks = []
        for _ in range(num_blocks):
            block = self.find_free_block(used_blocks)
            if block < 0:
                print(f"Error: No free blocks for {filename}")
                return False
            allocated_blocks.append(block)
            used_blocks.add(block)

        # Write file data to blocks
        for i, block in enumerate(allocated_blocks):
            block_offset = self.PREFIX_SIZE + (block * BLOCK_SIZE)
            start = i * BLOCK_SIZE
            end = min(start + BLOCK_SIZE, len(file_data))
            chunk = file_data[start:end]
            if len(chunk) < BLOCK_SIZE:
                chunk = chunk + bytes([0x1A] * (BLOCK_SIZE - len(chunk)))
            self.data[block_offset:block_offset+BLOCK_SIZE] = chunk

        # Create directory entries
        blocks_per_extent = 8
        extent_num = 0
        block_idx = 0

        # Prepare extension bytes with optional SYS attribute
        ext_bytes = ext.encode('ascii')
        if sys_attr:
            ext_bytes = bytes([ext_bytes[0] | 0x80]) + ext_bytes[1:]

        while block_idx < len(allocated_blocks):
            dir_idx = self.find_free_dir_entry()
            if dir_idx < 0:
                print(f"Error: No free directory entry for {filename}")
                return False

            entry_offset = self.dir_offset + (dir_idx * 32)

            entry = bytearray(32)
            entry[0] = user
            entry[1:9] = name.encode('ascii')
            entry[9:12] = ext_bytes
            entry[12] = extent_num & 0x1F
            entry[13] = 0
            entry[14] = (extent_num >> 5) & 0x3F

            extent_blocks = allocated_blocks[block_idx:block_idx+blocks_per_extent]
            if block_idx + blocks_per_extent >= len(allocated_blocks):
                remaining = len(file_data) - (block_idx * BLOCK_SIZE)
                extent_records = (remaining + 127) // 128
            else:
                extent_records = 128
            entry[15] = min(extent_records, 128)

            for i, block in enumerate(extent_blocks):
                struct.pack_into('<H', entry, 16 + i*2, block)

            self.data[entry_offset:entry_offset+32] = entry

            block_idx += blocks_per_extent
            extent_num += 1

        sys_flag = " [SYS]" if sys_attr else ""
        print(f"Added {filename}{sys_flag}: {len(file_data)} bytes, {num_blocks} blocks")
        return True

    def list_files(self):
        """List all files in the directory."""
        files = {}
        for i in range(self.DIR_ENTRIES):
            offset = self.dir_offset + (i * 32)
            user = self.data[offset]
            if user != 0xE5 and user < 32:
                # Validate filename - must be printable ASCII (0x20-0x7E)
                # Mask off attribute bits (high bit) from extension bytes
                name_bytes = self.data[offset+1:offset+9]
                ext_bytes = bytes([b & 0x7F for b in self.data[offset+9:offset+12]])
                if not all(0x20 <= b <= 0x7E for b in name_bytes):
                    continue
                if not all(0x20 <= b <= 0x7E for b in ext_bytes):
                    continue

                name = name_bytes.decode('ascii').rstrip()
                ext = ext_bytes.decode('ascii').rstrip()
                extent_lo = self.data[offset+12]
                extent_hi = self.data[offset+14]
                extent = extent_lo + (extent_hi << 5)
                records = self.data[offset+15]

                fullname = f"{name}.{ext}" if ext else name
                key = (user, fullname)

                if key not in files:
                    files[key] = {'extents': 0, 'records': 0, 'blocks': []}

                files[key]['extents'] = max(files[key]['extents'], extent + 1)
                if extent == files[key]['extents'] - 1:
                    files[key]['records'] = extent * 128 + records

                for j in range(8):
                    block = struct.unpack('<H', self.data[offset+16+j*2:offset+18+j*2])[0]
                    if block > 0:
                        files[key]['blocks'].append(block)

        return files

    def delete_file(self, filename, user=0):
        """Delete a file from the disk image by marking its directory entries as empty."""
        # Parse filename (8.3 format)
        name, ext = os.path.splitext(filename.upper())
        name = name[:8].ljust(8)
        ext = ext[1:4].ljust(3) if ext else '   '

        deleted_count = 0
        for i in range(self.DIR_ENTRIES):
            offset = self.dir_offset + (i * 32)
            entry_user = self.data[offset]
            if entry_user == user:
                entry_name = bytes(self.data[offset+1:offset+9]).decode('ascii')
                # Mask off attribute bits (high bit) from extension bytes
                entry_ext_bytes = bytes([b & 0x7F for b in self.data[offset+9:offset+12]])
                entry_ext = entry_ext_bytes.decode('ascii')
                if entry_name == name and entry_ext == ext:
                    # Mark entry as deleted
                    self.data[offset] = 0xE5
                    deleted_count += 1

        return deleted_count

    def extract_file(self, filename, user=0):
        """Extract a file from the disk image.

        Returns the file data as bytes, or None if not found.
        """
        # Parse filename (8.3 format)
        name, ext = os.path.splitext(filename.upper())
        name = name[:8].ljust(8)
        ext = ext[1:4].ljust(3) if ext else '   '

        # Collect all extents for this file
        extents = {}  # extent_num -> (records, blocks)
        for i in range(self.DIR_ENTRIES):
            offset = self.dir_offset + (i * 32)
            entry_user = self.data[offset]
            if entry_user == user:
                entry_name = bytes(self.data[offset+1:offset+9]).decode('ascii')
                # Mask off attribute bits (high bit) from extension bytes
                entry_ext_bytes = bytes([b & 0x7F for b in self.data[offset+9:offset+12]])
                entry_ext = entry_ext_bytes.decode('ascii')
                if entry_name == name and entry_ext == ext:
                    extent_lo = self.data[offset+12]
                    extent_hi = self.data[offset+14]
                    extent_num = extent_lo + (extent_hi << 5)
                    records = self.data[offset+15]
                    blocks = []
                    for j in range(8):
                        block = struct.unpack('<H', self.data[offset+16+j*2:offset+18+j*2])[0]
                        if block > 0:
                            blocks.append(block)
                    extents[extent_num] = (records, blocks)

        if not extents:
            return None

        # Read data from blocks in extent order
        file_data = bytearray()
        for ext_num in sorted(extents.keys()):
            records, blocks = extents[ext_num]
            for block in blocks:
                block_offset = self.PREFIX_SIZE + (block * BLOCK_SIZE)
                file_data.extend(self.data[block_offset:block_offset + BLOCK_SIZE])

        # Trim to actual size based on last extent's record count
        last_ext = max(extents.keys())
        total_records = last_ext * 128 + extents[last_ext][0]
        actual_size = total_records * 128
        return bytes(file_data[:actual_size])


def cmd_create(args):
    """Create a new empty formatted disk image."""
    if os.path.exists(args.disk) and not args.force:
        print(f"Error: {args.disk} already exists (use --force to overwrite)")
        return 1

    # Determine format to create
    if getattr(args, 'sssd', False):
        disk_data = create_sssd_disk()
        size_desc = f"{len(disk_data) // 1024}KB SSSD (ibm-3740)"
    elif getattr(args, 'combo', False):
        disk_data = create_hd1k_disk(combo=True)
        size_desc = f"{len(disk_data) // 1048576}MB combo (6 slices)"
    else:
        disk_data = create_hd1k_disk(combo=False)
        size_desc = f"{len(disk_data) // 1048576}MB hd1k"

    with open(args.disk, 'wb') as f:
        f.write(disk_data)

    print(f"Created {size_desc} disk: {args.disk}")
    return 0


def detect_disk_format(disk_data):
    """Auto-detect disk format based on size and signatures.

    Returns:
        'sssd' for ibm-3740 (8" SSSD floppy)
        'combo' for combo disk with MBR
        'hd1k' for standard hd1k
    """
    size = len(disk_data)

    # Check for SSSD (ibm-3740) format - ~256KB
    if size == SSSD_SIZE or (243000 < size < 260000):
        return 'sssd'

    # Check for combo disk - MBR signature and partition type
    if size >= HD1K_COMBO_SIZE:
        if disk_data[0x1FE] == 0x55 and disk_data[0x1FF] == 0xAA:
            if disk_data[0x1BE + 4] == 0x2E:  # RomWBW hd1k partition type
                return 'combo'

    # Default to hd1k for 8MB disks
    if size == HD1K_SINGLE_SIZE:
        return 'hd1k'

    # Fallback - assume hd1k for larger disks, sssd for smaller
    if size > 1000000:
        return 'hd1k'
    else:
        return 'sssd'


def get_disk_object(disk_data, format_hint=None):
    """Get appropriate disk object for the format.

    Args:
        disk_data: The disk image data
        format_hint: Optional format override ('sssd', 'hd1k', 'combo')

    Returns:
        Disk object (SssdDisk, Hd1kDisk, or ComboDisk)
    """
    if format_hint:
        fmt = format_hint
    else:
        fmt = detect_disk_format(disk_data)

    if fmt == 'sssd':
        return SssdDisk(disk_data)
    elif fmt == 'combo':
        return ComboDisk(disk_data)
    else:
        return Hd1kDisk(disk_data)


def is_combo_disk(disk_data):
    """Auto-detect if disk is combo format by checking MBR signature and size."""
    return detect_disk_format(disk_data) == 'combo'


def cmd_add(args):
    """Add files to a disk image."""
    with open(args.disk, 'rb') as f:
        disk_data = bytearray(f.read())

    # Determine format (explicit flags override auto-detect)
    format_hint = None
    if getattr(args, 'sssd', False):
        format_hint = 'sssd'
    elif getattr(args, 'combo', False):
        format_hint = 'combo'

    disk = get_disk_object(disk_data, format_hint)

    sys_attr = getattr(args, 'sys', False)
    user = getattr(args, 'user', 0)

    for filepath in args.files:
        filename = os.path.basename(filepath)
        with open(filepath, 'rb') as f:
            file_data = f.read()
        if not disk.add_file(filename, file_data, sys_attr=sys_attr, user=user):
            return 1

    with open(args.disk, 'wb') as f:
        f.write(disk_data)

    print(f"Successfully updated {args.disk}")
    return 0


def cmd_list(args):
    """List files in a disk image."""
    with open(args.disk, 'rb') as f:
        disk_data = bytearray(f.read())

    # Determine format (explicit flags override auto-detect)
    format_hint = None
    if getattr(args, 'sssd', False):
        format_hint = 'sssd'
    elif getattr(args, 'combo', False):
        format_hint = 'combo'

    disk = get_disk_object(disk_data, format_hint)
    files = disk.list_files()

    if not files:
        print("No files found")
        return 0

    print(f"{'User':<5} {'Filename':<12} {'Size':>8} {'Blocks':>6}")
    print("-" * 35)

    for (user, name), info in sorted(files.items()):
        size = info['records'] * 128
        blocks = len(info['blocks'])
        print(f"{user:<5} {name:<12} {size:>8} {blocks:>6}")

    return 0


def cmd_delete(args):
    """Delete files from a disk image."""
    with open(args.disk, 'rb') as f:
        disk_data = bytearray(f.read())

    # Determine format (explicit flags override auto-detect)
    format_hint = None
    if getattr(args, 'sssd', False):
        format_hint = 'sssd'
    elif getattr(args, 'combo', False):
        format_hint = 'combo'

    disk = get_disk_object(disk_data, format_hint)

    # Get list of all files
    files = disk.list_files()

    any_deleted = False
    for pattern in args.files:
        pattern_83 = cpm_pattern_to_83(pattern)
        matched = False

        for (user, fullname), info in list(files.items()):
            # Convert filename to 8.3 format for matching
            if '.' in fullname:
                name, ext = fullname.rsplit('.', 1)
            else:
                name, ext = fullname, ''
            filename_83 = name.ljust(8) + ext.ljust(3)

            if cpm_match(pattern_83, filename_83):
                deleted = disk.delete_file(fullname, user)
                if deleted > 0:
                    print(f"Deleted {fullname} ({deleted} extent(s))")
                    any_deleted = True
                    matched = True
                    del files[(user, fullname)]

        if not matched:
            print(f"No files matching: {pattern}")

    if any_deleted:
        with open(args.disk, 'wb') as f:
            f.write(disk_data)
        print(f"Successfully updated {args.disk}")

    return 0


def cmd_extract(args):
    """Extract files from a disk image."""
    with open(args.disk, 'rb') as f:
        disk_data = bytearray(f.read())

    # Determine format (explicit flags override auto-detect)
    format_hint = None
    if getattr(args, 'sssd', False):
        format_hint = 'sssd'
    elif getattr(args, 'combo', False):
        format_hint = 'combo'

    disk = get_disk_object(disk_data, format_hint)

    user = getattr(args, 'user', 0)
    output_dir = getattr(args, 'output', '.')

    for filename in args.files:
        file_data = disk.extract_file(filename, user=user)
        if file_data is None:
            print(f"File not found: {filename}")
            return 1

        # Determine output path
        out_name = os.path.basename(filename).lower()
        out_path = os.path.join(output_dir, out_name)

        with open(out_path, 'wb') as f:
            f.write(file_data)
        print(f"Extracted {filename} -> {out_path} ({len(file_data)} bytes)")

    return 0


def main():
    parser = argparse.ArgumentParser(
        description='CP/M disk image utility',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )

    subparsers = parser.add_subparsers(dest='command', required=True)

    # Create command
    create_parser = subparsers.add_parser('create', help='Create new empty disk image')
    create_group = create_parser.add_mutually_exclusive_group()
    create_group.add_argument('--sssd', action='store_true',
                              help='Create SSSD (ibm-3740) format (250KB)')
    create_group.add_argument('--combo', action='store_true',
                              help='Create combo format (51MB) instead of single hd1k (8MB)')
    create_parser.add_argument('--force', '-f', action='store_true',
                               help='Overwrite existing file')
    create_parser.add_argument('disk', help='Disk image file to create')
    create_parser.set_defaults(func=cmd_create)

    # Add command
    add_parser = subparsers.add_parser('add', help='Add files to disk image')
    add_format = add_parser.add_mutually_exclusive_group()
    add_format.add_argument('--sssd', action='store_true',
                            help='Disk is SSSD (ibm-3740) format')
    add_format.add_argument('--combo', action='store_true',
                            help='Disk is combo format (1MB prefix)')
    add_parser.add_argument('--sys', '-s', action='store_true',
                           help='Set SYS attribute on files (makes visible from any user area)')
    add_parser.add_argument('--user', '-u', type=int, default=0,
                           help='User number for files (0-15, default 0)')
    add_parser.add_argument('disk', help='Disk image file')
    add_parser.add_argument('files', nargs='+', help='Files to add')
    add_parser.set_defaults(func=cmd_add)

    # List command
    list_parser = subparsers.add_parser('list', help='List files in disk image')
    list_format = list_parser.add_mutually_exclusive_group()
    list_format.add_argument('--sssd', action='store_true',
                             help='Disk is SSSD (ibm-3740) format')
    list_format.add_argument('--combo', action='store_true',
                             help='Disk is combo format (1MB prefix)')
    list_parser.add_argument('disk', help='Disk image file')
    list_parser.set_defaults(func=cmd_list)

    # Delete command
    delete_parser = subparsers.add_parser('delete', help='Delete files from disk image')
    delete_format = delete_parser.add_mutually_exclusive_group()
    delete_format.add_argument('--sssd', action='store_true',
                               help='Disk is SSSD (ibm-3740) format')
    delete_format.add_argument('--combo', action='store_true',
                               help='Disk is combo format (1MB prefix)')
    delete_parser.add_argument('disk', help='Disk image file')
    delete_parser.add_argument('files', nargs='+', help='Files to delete')
    delete_parser.set_defaults(func=cmd_delete)

    # Extract command
    extract_parser = subparsers.add_parser('extract', help='Extract files from disk image')
    extract_format = extract_parser.add_mutually_exclusive_group()
    extract_format.add_argument('--sssd', action='store_true',
                                help='Disk is SSSD (ibm-3740) format')
    extract_format.add_argument('--combo', action='store_true',
                                help='Disk is combo format (1MB prefix)')
    extract_parser.add_argument('--user', '-u', type=int, default=0,
                                help='User number to extract from (0-15, default 0)')
    extract_parser.add_argument('--output', '-o', default='.',
                                help='Output directory (default: current directory)')
    extract_parser.add_argument('disk', help='Disk image file')
    extract_parser.add_argument('files', nargs='+', help='Files to extract')
    extract_parser.set_defaults(func=cmd_extract)

    args = parser.parse_args()
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
