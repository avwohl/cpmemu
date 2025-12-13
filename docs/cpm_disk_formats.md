# CP/M Disk Formats and BIOS Implementation

This document describes the disk image formats and CP/M BIOS interface used by the emulator.

## References

### IMD (ImageDisk) Format
- [ImageDisk Manual (PDF)](https://oldcomputers-ddns.org/public/pub/manuals/imd.pdf) - Official documentation
- [IMD.TXT Documentation](https://acorn.huininga.nl/pub/software/x86/Disk%20imaging/IMD/IMD.TXT) - Full format specification
- [imd-utils on GitHub](https://github.com/hharte/imd-utils) - Cross-platform utilities with source code

### CP/M BIOS Documentation
- [CP/M BIOS Reference](https://www.seasip.info/Cpm/bios.html) - Comprehensive BIOS documentation
- [CP/M 2.2 System Alteration Guide](http://www.gaby.de/cpm/manuals/archive/cpm22htm/ch6.htm) - Official Digital Research manual
- [Setting up CP/M on New Hardware](http://cpuville.com/Code/CPM-on-a-new-computer.html) - Practical implementation guide
- [CP/M Structure](http://www.primrosebank.net/computers/cpm/cpm_structure.htm) - Overview of CP/M components

---

## IMD File Format

ImageDisk (.IMD) files preserve floppy disk images with full format metadata.

### File Structure

```
+---------------------------+
| ASCII Header              | "IMD 1.17: DD/MM/YYYY HH:MM:SS\r\n"
| Optional Comment          | Free-form text
| EOF Marker                | 0x1A
+---------------------------+
| Track 0 Record            |
| Track 1 Record            |
| ...                       |
| Track N Record            |
+---------------------------+
```

### Track Record Format

Each track contains:

| Field | Size | Description |
|-------|------|-------------|
| Mode | 1 byte | Transfer rate and encoding |
| Cylinder | 1 byte | Physical cylinder (0-76 typical) |
| Head | 1 byte | Side (0-1), bits 6-7 indicate optional maps |
| Sector Count | 1 byte | Number of sectors on track |
| Sector Size | 1 byte | Size code (see below) |
| Sector Numbering Map | N bytes | Physical sector IDs |
| [Sector Cylinder Map] | N bytes | Optional, if Head bit 7 set |
| [Sector Head Map] | N bytes | Optional, if Head bit 6 set |
| Sector Data Records | varies | Data for each sector |

**Mode Values:**
| Value | Encoding | Data Rate |
|-------|----------|-----------|
| 0 | FM (SD) | 500 kbps |
| 1 | FM (SD) | 300 kbps |
| 2 | FM (SD) | 250 kbps |
| 3 | MFM (DD) | 500 kbps |
| 4 | MFM (DD) | 300 kbps |
| 5 | MFM (DD) | 250 kbps |

**Sector Size Codes:**
| Code | Size |
|------|------|
| 0 | 128 bytes |
| 1 | 256 bytes |
| 2 | 512 bytes |
| 3 | 1024 bytes |
| 4 | 2048 bytes |
| 5 | 4096 bytes |

**Sector Data Status Byte:**
| Value | Meaning |
|-------|---------|
| 0x00 | Sector data unavailable (read error) |
| 0x01 | Normal data follows |
| 0x02 | Compressed - all bytes equal (1 byte follows) |
| 0x03 | Normal data with Deleted-Data mark |
| 0x04 | Compressed with Deleted-Data mark |
| 0x05 | Normal data with read error |
| 0x06 | Compressed data with error |
| 0x07 | Deleted data with error |
| 0x08 | Compressed deleted data with error |

### Sector Skew

Physical floppy drives couldn't read consecutive sectors due to processing time between reads. Sectors were numbered non-sequentially (interleaved) to allow the disk to rotate to the next logical sector while the CPU processed the previous one.

**Example (6:1 skew on 26-sector track):**
- Logical sectors: 1, 2, 3, 4, 5, 6, 7, 8...
- Physical layout: 1, 7, 13, 19, 25, 5, 11, 17, 23, 3, 9, 15, 21, 2, 8, 14, 20, 26, 6, 12, 18, 24, 4, 10, 16, 22

The IMD file stores the actual sector numbering in the Sector Numbering Map, so software can read sectors in logical order.

---

## Raw .DSK File Format

Raw .dsk files are simple sector dumps with no metadata. The format depends on the disk geometry:

### Standard 8" SSSD (Single-Sided Single-Density)
- 77 tracks
- 26 sectors per track
- 128 bytes per sector
- Total: 256,256 bytes (250 KB)
- 2 reserved tracks for system (boot + CCP/BDOS)

### SIMH Altair Format
The SIMH Altair emulator uses a different format:
- Larger images (1,113,536 bytes seen in cpm2.dsk)
- May include multiple logical disks or special geometry

### File Organization
```
Track 0, Sector 1 (128 bytes)
Track 0, Sector 2 (128 bytes)
...
Track 0, Sector 26 (128 bytes)
Track 1, Sector 1 (128 bytes)
...
Track 76, Sector 26 (128 bytes)
```

**System Tracks (typically Track 0-1):**
- Track 0: Boot sector, cold boot loader
- Track 1: CCP (Console Command Processor) and BDOS
- The BIOS is typically at the end of the BDOS image

---

## CP/M Memory Map

For a 64K system:
```
0000-00FF   Page Zero (BIOS vectors, FCB, etc.)
0100-xxxx   TPA (Transient Program Area)
xxxx-FDFF   BDOS
FE00-FFFF   BIOS
```

### Page Zero Layout
| Address | Contents |
|---------|----------|
| 0000-0002 | JMP WBOOT (warm boot entry) |
| 0003 | IOBYTE (I/O configuration) |
| 0004 | Current drive/user |
| 0005-0007 | JMP BDOS (BDOS entry point) |
| 005C-007F | Default FCB |
| 0080-00FF | Default DMA buffer / command tail |

---

## CP/M BIOS Jump Table

The BIOS provides 17 entry points (51 bytes of jump instructions):

| Offset | Function | Description |
|--------|----------|-------------|
| 00 | BOOT | Cold start |
| 03 | WBOOT | Warm boot (reload CCP) |
| 06 | CONST | Console status (A=0xFF if char ready) |
| 09 | CONIN | Console input (returns char in A) |
| 12 | CONOUT | Console output (char in C) |
| 15 | LIST | Printer output (char in C) |
| 18 | PUNCH | Paper tape punch output (char in C) |
| 21 | READER | Paper tape reader input (returns in A) |
| 24 | HOME | Move disk to track 0 |
| 27 | SELDSK | Select disk (drive in C, returns HL=DPH or 0) |
| 30 | SETTRK | Set track (BC=track number) |
| 33 | SETSEC | Set sector (BC=sector number) |
| 36 | SETDMA | Set DMA address (BC=address) |
| 39 | READ | Read sector (returns A=0 OK, A=1 error) |
| 42 | WRITE | Write sector (C=write type, returns A=0 OK) |
| 45 | LISTST | Printer status (A=0xFF if ready) |
| 48 | SECTRAN | Sector translation (BC=logical, DE=table, returns HL=physical) |

### BIOS Function Details

**CONST (Console Status):**
- Returns A=0xFF if character available, A=0x00 if not

**CONIN (Console Input):**
- Waits for character, returns in A (bit 7 stripped)

**CONOUT (Console Output):**
- Character in C register
- Outputs to console device

**SELDSK (Select Disk):**
- Drive number in C (0=A:, 1=B:, etc.)
- Returns HL pointing to Disk Parameter Header (DPH), or HL=0 if invalid

**READ/WRITE:**
- Uses track/sector/DMA set by previous calls
- Returns A=0 for success, A=1 for error

### Disk Parameter Header (DPH)

Each disk has a DPH structure:
```
DPH:    DW XLT      ; Sector translate table address (or 0)
        DW 0,0,0    ; Scratch area for BDOS
        DW DIRBUF   ; Directory buffer address
        DW DPB      ; Disk Parameter Block address
        DW CSV      ; Checksum vector address
        DW ALV      ; Allocation vector address
```

### Disk Parameter Block (DPB)

Defines disk geometry:
```
DPB:    DW SPT      ; Sectors per track
        DB BSH      ; Block shift (log2(BLS/128))
        DB BLM      ; Block mask (BLS/128-1)
        DB EXM      ; Extent mask
        DW DSM      ; Total blocks - 1
        DW DRM      ; Directory entries - 1
        DB AL0,AL1  ; Directory allocation bitmap
        DW CKS      ; Checksum vector size
        DW OFF      ; Reserved tracks for system
```

**Standard 8" SSSD DPB:**
```
SPT = 26        ; 26 sectors per track
BSH = 3         ; Block size = 1024 bytes
BLM = 7
EXM = 0
DSM = 242       ; 243 blocks total
DRM = 63        ; 64 directory entries
AL0 = 0xC0      ; First 2 blocks for directory
AL1 = 0x00
CKS = 16        ; Checksum entries
OFF = 2         ; 2 reserved tracks
```

---

## Implementation Notes

### Emulator Approaches

**BDOS-Level Emulation (current cpmemu approach):**
- Trap BDOS calls (CALL 0005)
- Implement file operations using host filesystem
- Simpler, good for running programs
- Cannot boot real CP/M

**BIOS-Level Emulation (new altair_emu approach):**
- Trap BIOS calls
- Implement disk I/O using disk image files
- Can boot real CP/M from disk images
- More complex but more authentic

### Trapping BIOS Calls

Place RET (0xC9) instructions at BIOS entry points:
```cpp
// At BIOS_BASE + offset, place 0xC9 (RET)
// When CPU executes RET at these addresses, detect and handle
```

Or use HLT instruction and handle in execute loop.

### Disk Image Access

For reading .IMD files:
1. Skip ASCII header until 0x1A
2. Parse track records
3. Build sector map accounting for interleave
4. Translate logical sector requests to file offsets

For reading raw .dsk files:
1. Calculate offset: `(track * sectors_per_track + sector - 1) * sector_size`
2. Read/write directly at calculated offset

---

## Files in ~/in/cpm2/

| File | Size | Description |
|------|------|-------------|
| cpm2.dsk | 1,113,536 | CP/M 2.2 boot disk (SIMH format) |
| app.dsk | 1,113,536 | Application disk |
| i.dsk | 8,388,608 | Large hard disk image |
| appleiicpm.dsk | 143,360 | Apple II CP/M disk (140K) |
| 128sssd.imd | 260,722 | 8" SSSD disk in IMD format |

### CPM22RED.IMD

The file `~/in/CPM22RED.IMD` is a CP/M 2.2 OEM Redistribution disk containing:
- Boot sector code
- CP/M BDOS
- MOVCPM (memory configuration utility)
- Serialization code
- System generation tools
- Source files: BIOS.ASM, BOOT.ASM, etc.

This is an authentic OEM distribution kit that can be used to build a custom CP/M system.
