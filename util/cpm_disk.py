#!/usr/bin/env python3
"""CP/M disk image utility for hd1k disk images.

Supports two disk formats:
  - hd1k: Standard RomWBW hd1k format (8MB single slice)
  - combo: Combo disk with 1MB MBR prefix + 6x8MB slices (51MB total)

Usage:
  cpm_disk.py create <disk.img>                  # Create 8MB hd1k disk
  cpm_disk.py create --combo <disk.img>          # Create 51MB combo disk
  cpm_disk.py add <disk.img> <file1.com> [...]   # Add files to disk
  cpm_disk.py list <disk.img>                    # List files in disk

Combo format is auto-detected for existing disks.
"""

import sys
import os
import struct
import argparse

# Common CP/M constants
SECTOR_SIZE = 512
BLOCK_SIZE = 4096  # 8 sectors per block

# Disk format sizes
HD1K_SINGLE_SIZE = 8388608      # 8 MB
HD1K_SLICE_SIZE = 8388608       # 8 MB per slice
HD1K_MBR_PREFIX = 1048576       # 1 MB MBR prefix for combo
HD1K_COMBO_SLICES = 6           # 6 slices in combo disk
HD1K_COMBO_SIZE = HD1K_MBR_PREFIX + (HD1K_COMBO_SLICES * HD1K_SLICE_SIZE)  # ~51 MB


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

    dir_offset = offset + (BOOT_TRACKS * SECTORS_PER_TRACK * SECTOR_SIZE)
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


class Hd1kDisk:
    """Standard hd1k disk format (RomWBW compatible)."""

    SECTORS_PER_TRACK = 16
    DIR_ENTRIES = 1024
    BOOT_TRACKS = 2
    DIR_START = BOOT_TRACKS * SECTORS_PER_TRACK * SECTOR_SIZE  # 0x4000 (16KB)

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
        """Find highest used block number in directory."""
        max_block = 0
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            if self.data[offset] != 0xE5:
                for j in range(8):
                    block = struct.unpack('<H', self.data[offset+16+j*2:offset+18+j*2])[0]
                    if block > max_block and block < 0xFFFF:
                        max_block = block
        return max_block

    def add_file(self, filename, file_data):
        """Add a file to the disk image."""
        dir_offset = self.find_free_dir_entry()
        if dir_offset is None:
            print(f"No free directory entry for {filename}")
            return False

        next_block = self.find_max_block() + 1

        # Parse filename (8.3 format)
        name, ext = os.path.splitext(filename.upper())
        name = name[:8].ljust(8)
        ext = ext[1:4].ljust(3) if ext else '   '

        num_records = (len(file_data) + 127) // 128
        records_per_block = BLOCK_SIZE // 128
        blocks_needed = (num_records + records_per_block - 1) // records_per_block

        print(f"Adding {filename}: {len(file_data)} bytes, {num_records} records, {blocks_needed} blocks starting at {next_block}")

        # Create directory entry
        entry = bytearray(32)
        entry[0] = 0  # User 0
        entry[1:9] = name.encode('ascii')
        entry[9:12] = ext.encode('ascii')
        entry[12] = 0  # Extent low
        entry[13] = 0  # S1
        entry[14] = 0  # S2
        entry[15] = min(num_records, 128)

        for i in range(min(blocks_needed, 8)):
            struct.pack_into('<H', entry, 16 + i*2, next_block + i)

        self.data[dir_offset:dir_offset+32] = entry

        # Write file data to blocks
        for i in range(blocks_needed):
            block_num = next_block + i
            block_offset = self.DIR_START + (block_num * BLOCK_SIZE)
            data_offset = i * BLOCK_SIZE
            chunk = file_data[data_offset:data_offset + BLOCK_SIZE]
            if len(chunk) < BLOCK_SIZE:
                chunk = chunk + bytes([0x1A] * (BLOCK_SIZE - len(chunk)))
            self.data[block_offset:block_offset + BLOCK_SIZE] = chunk

        return True

    def list_files(self):
        """List all files in the directory."""
        files = {}
        for i in range(self.DIR_ENTRIES):
            offset = self.DIR_START + (i * 32)
            user = self.data[offset]
            if user != 0xE5 and user < 32:
                # Validate filename - must be printable ASCII (0x20-0x7E)
                name_bytes = self.data[offset+1:offset+9]
                ext_bytes = self.data[offset+9:offset+12]
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


class ComboDisk:
    """Combo disk with 1MB prefix."""

    SECTORS_PER_TRACK = 16
    TRACK_SIZE = SECTOR_SIZE * SECTORS_PER_TRACK
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

    def add_file(self, filename, file_data, user=0):
        """Add a file to the disk image."""
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

        while block_idx < len(allocated_blocks):
            dir_idx = self.find_free_dir_entry()
            if dir_idx < 0:
                print(f"Error: No free directory entry for {filename}")
                return False

            entry_offset = self.dir_offset + (dir_idx * 32)

            entry = bytearray(32)
            entry[0] = user
            entry[1:9] = name.encode('ascii')
            entry[9:12] = ext.encode('ascii')
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

        print(f"Added {filename}: {len(file_data)} bytes, {num_blocks} blocks")
        return True

    def list_files(self):
        """List all files in the directory."""
        files = {}
        for i in range(self.DIR_ENTRIES):
            offset = self.dir_offset + (i * 32)
            user = self.data[offset]
            if user != 0xE5 and user < 32:
                # Validate filename - must be printable ASCII (0x20-0x7E)
                name_bytes = self.data[offset+1:offset+9]
                ext_bytes = self.data[offset+9:offset+12]
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


def cmd_create(args):
    """Create a new empty formatted disk image."""
    if os.path.exists(args.disk) and not args.force:
        print(f"Error: {args.disk} already exists (use --force to overwrite)")
        return 1

    disk_data = create_hd1k_disk(combo=args.combo)

    with open(args.disk, 'wb') as f:
        f.write(disk_data)

    if args.combo:
        size_desc = f"{len(disk_data) // 1048576}MB combo (6 slices)"
    else:
        size_desc = f"{len(disk_data) // 1048576}MB single slice"

    print(f"Created {size_desc} disk: {args.disk}")
    return 0


def is_combo_disk(disk_data):
    """Auto-detect if disk is combo format by checking MBR signature and size."""
    if len(disk_data) < HD1K_COMBO_SIZE:
        return False
    # Check MBR signature
    if disk_data[0x1FE] == 0x55 and disk_data[0x1FF] == 0xAA:
        # Check partition type at 0x1BE + 4
        if disk_data[0x1BE + 4] == 0x2E:  # RomWBW hd1k partition type
            return True
    return False


def cmd_add(args):
    """Add files to a disk image."""
    with open(args.disk, 'rb') as f:
        disk_data = bytearray(f.read())

    combo = args.combo or is_combo_disk(disk_data)

    if combo:
        if len(disk_data) < ComboDisk.PREFIX_SIZE + ComboDisk.SLICE_SIZE:
            print(f"Error: {args.disk} is too small to be a combo disk")
            return 1
        disk = ComboDisk(disk_data)
    else:
        disk = Hd1kDisk(disk_data)

    for filepath in args.files:
        filename = os.path.basename(filepath)
        with open(filepath, 'rb') as f:
            file_data = f.read()
        if not disk.add_file(filename, file_data):
            return 1

    with open(args.disk, 'wb') as f:
        f.write(disk_data)

    print(f"Successfully updated {args.disk}")
    return 0


def cmd_list(args):
    """List files in a disk image."""
    with open(args.disk, 'rb') as f:
        disk_data = bytearray(f.read())

    combo = args.combo or is_combo_disk(disk_data)

    if combo:
        disk = ComboDisk(disk_data)
    else:
        disk = Hd1kDisk(disk_data)

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


def main():
    parser = argparse.ArgumentParser(
        description='CP/M disk image utility',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )

    subparsers = parser.add_subparsers(dest='command', required=True)

    # Create command
    create_parser = subparsers.add_parser('create', help='Create new empty disk image')
    create_parser.add_argument('--combo', action='store_true',
                               help='Create combo format (51MB) instead of single (8MB)')
    create_parser.add_argument('--force', '-f', action='store_true',
                               help='Overwrite existing file')
    create_parser.add_argument('disk', help='Disk image file to create')
    create_parser.set_defaults(func=cmd_create)

    # Add command
    add_parser = subparsers.add_parser('add', help='Add files to disk image')
    add_parser.add_argument('--combo', action='store_true',
                           help='Disk is combo format (1MB prefix)')
    add_parser.add_argument('disk', help='Disk image file')
    add_parser.add_argument('files', nargs='+', help='Files to add')
    add_parser.set_defaults(func=cmd_add)

    # List command
    list_parser = subparsers.add_parser('list', help='List files in disk image')
    list_parser.add_argument('--combo', action='store_true',
                            help='Disk is combo format (1MB prefix)')
    list_parser.add_argument('disk', help='Disk image file')
    list_parser.set_defaults(func=cmd_list)

    args = parser.parse_args()
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
