# CP/M 2.2 Emulator - Current Status

## What's Working ✅

### Core Emulation
- ✅ CP/M 2.2 memory layout properly implemented
- ✅ BIOS jump table at correct location (0xFA00)
- ✅ BDOS entry point at 0x0005
- ✅ Low memory setup (0x0000-0x00FF)
- ✅ TPA (Transient Program Area) from 0x0100

### BDOS Functions Implemented
- ✅ Console I/O (functions 1, 2, 9, 11)
- ✅ System info (function 12 - CP/M version)
- ✅ Drive operations (functions 14, 25)
- ✅ File operations (functions 15-22: open, close, read, write, make, delete)
- ✅ DMA buffer management (function 26)
- ✅ User number (function 32)

### BIOS Functions Implemented
- ✅ CONST (console status)
- ✅ CONIN (console input)
- ✅ CONOUT (console output)
- ✅ WBOOT (warm boot/exit)

### File I/O
- ✅ Basic file operations (open, close, read, write)
- ✅ FCB (File Control Block) parsing
- ✅ CP/M 8.3 filename format
- ✅ Sequential file access
- ✅ Lowercase Unix filename matching
- ✅ File mapping system (programmable)

### Testing
- ✅ **MBASIC.COM runs successfully!**
  ```
  BASIC-80 Rev. 5.21
  [CP/M Version]
  Copyright 1977-1981 (C) by Microsoft
  Created: 28-Jul-81
  34866 Bytes free
  Ok
  ```

## What Needs Work 🚧

### High Priority
1. ⏳ **^Z EOF handling** - Text files use 0x1A as EOF marker
2. ⏳ **EOL conversion** - Unix `\n` ↔ CP/M `\r\n` (critical for m80.com)
3. ⏳ **128-byte record padding** - CP/M writes in 128-byte blocks
4. ⏳ **Configuration file support** - Better than hardcoding file paths

### Medium Priority
5. ⏳ **Wildcard file mapping** - `*.BAS = /path/**/*.bas`
6. ⏳ **Text/binary mode tracking** - Per-file type specification
7. ⏳ **File type detection** - Heuristics for auto-detecting text vs binary
8. ⏳ **Console input buffering** - Currently limited

### Lower Priority
9. ⏳ **Search first/next** - Directory scanning (functions 17-18)
10. ⏳ **Random access** - File positioning (functions 33-34, 36)
11. ⏳ **Extended BDOS** - Functions beyond CP/M 2.2 standard

## Test Resources 📁

### Available Test Files

**Location**: `/home/wohl/cl/mbasic/`

1. **Test Suite** (`tests/` directory)
   - 100+ .BAS test files
   - 2753 lines of BASIC code
   - Already in CP/M format (`\r\n` line endings)
   - Examples: `printsep.bas`, `recurse.bas`, `test_while_for_mix.bas`

2. **Classic Programs** (`site-dev/library/`)
   - Business: budget, finance, mortgage, diary
   - Data management: databases, file utilities
   - Utilities: sort, convert, charfreq
   - Games: Super Star Trek

3. **Notable Files**
   - `superstartrek.bas` - Classic game
   - `com/trand.bas` - Random number tests

## Usage

### Current (Simple)
```bash
cd /home/wohl/qkz80
src/cpm_emulator com/mbasic.com
```

### Planned (With Config)
```bash
# Using configuration file
src/cpm_emulator examples/mbasic_tests.cfg

# Direct file mapping
src/cpm_emulator com/mbasic.com TEST.BAS=/path/to/test.bas
```

## File Format Issues

### The Problem
1. **CP/M**: Files are 128-byte records, text files end at `^Z`, uses `\r\n`
2. **Unix**: Exact file sizes, no EOF marker, uses `\n`
3. **No automatic detection**: Can't reliably distinguish text from binary

### The Solution
Configuration files specify:
- Which files are text vs binary
- Whether to convert EOL
- Whether to handle `^Z`
- File path mappings (including wildcards)

See `docs/file_handling_notes.md` for detailed explanation.

## Building

```bash
cd /home/wohl/qkz80/src
g++ -g -O2 -o cpm_emulator cpm_main.cc qkz80.cc qkz80_mem.cc \
    qkz80_errors.cc qkz80_reg_set.cc
```

## Architecture

### File Structure
- `src/cpm_main.cc` - Main CP/M emulator (851 lines)
- `src/qkz80.cc` - 8080 CPU emulator
- `src/qkz80_*.cc` - CPU support files
- `docs/file_handling_notes.md` - Detailed file I/O design
- `examples/*.cfg` - Example configuration files

### Key Classes
- `CPMEmulator` - Main emulator class
  - Handles BDOS/BIOS calls
  - Manages file mappings
  - Tracks open files
- `qkz80` - CPU emulator (existing)

### Memory Layout (64K system)
```
0x0000-0x0002: JMP WBOOT
0x0003:        IOBYTE
0x0004:        Drive/User
0x0005-0x0007: JMP BDOS
0x005C-0x007F: Default FCB
0x0080-0x00FF: DMA buffer / command line
0x0100:        TPA start (programs load here)
0xE400:        CCP (not implemented, reserved)
0xEC00:        BDOS entry (magic address)
0xFA00:        BIOS jump table (magic addresses)
```

## Next Steps

### Immediate (to get file I/O working)
1. Add ^Z EOF detection to `bdos_read_sequential()`
2. Add 128-byte padding to `bdos_write_sequential()`
3. Implement basic .cfg parser
4. Add text/binary mode flag to `OpenFile` structure

### Short Term (for m80.com compatibility)
1. Implement EOL conversion filters
2. Add file type heuristics
3. Test with m80.com assembler

### Long Term (polish)
1. Wildcard file mapping
2. Multiple drive support
3. Random access file I/O
4. Better error handling
5. Interactive console input

## Success Criteria

- ✅ MBASIC runs and shows prompt
- ⏳ MBASIC can LOAD and RUN .BAS files
- ⏳ M80.COM assembler can process .ASM files
- ⏳ Round-trip: write file in CP/M, read in Unix
- ⏳ Full test suite passes

## Notes

The emulator is significantly better than `main2.cc`:
- Complete BDOS implementation (not just 3 functions)
- BIOS support (critical for MBASIC)
- Proper CP/M memory layout
- File mapping infrastructure
- Designed for extensibility

Main advantage over tnylpo: integrated file handling with automatic
conversion rather than requiring pre-conversion of files.
