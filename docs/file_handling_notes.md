# CP/M File Handling Issues and Solutions

## Key Differences Between CP/M and Unix Files

### 1. End-of-File Handling

**CP/M:**
- Files are written in 128-byte records
- Text files use `^Z` (0x1A) as EOF marker
- File size is always a multiple of 128 bytes
- Padding after EOF is undefined (usually 0x00 or 0x1A)

**Unix:**
- Files have exact byte sizes
- No special EOF marker
- File ends at last byte

**Solution Needed:**
- When reading CP/M text files: stop at `^Z`
- When writing CP/M text files: pad to 128 bytes with `^Z`
- Track file type (text vs binary) per file

### 2. End-of-Line Handling

**CP/M:**
- Uses `\r\n` (0x0D 0x0A) for line endings
- M80 assembler and other tools expect this format

**Unix:**
- Uses `\n` (0x0A) only

**Solution Needed:**
- Convert `\n` → `\r\n` when reading Unix text files into CP/M
- Convert `\r\n` → `\n` when writing CP/M text files to Unix
- Only apply to text files, not binary

### 3. File Name Mapping

**Current Implementation:**
- Simple lowercase matching
- File mapping dictionary

**Issues:**
- Can't handle deep directory structures
- No wildcards
- Must specify every file individually
- No way to specify text vs binary mode

## Proposed Configuration File Format

Create a `.cfg` file to specify:

```
# Example: mbasic.cfg

# Program to run
program = mbasic.com

# File mappings with type specification
# Format: cpm_pattern = unix_path [text|binary]

# Map all .BAS files from tests directory as text
*.BAS = tests/**/*.bas text

# Map specific files
TEST.BAS = ~/basic_programs/test.bas text
DATA.DAT = ./data/mydata.dat binary

# Map assembler files
*.MAC = src/**/*.mac text
*.ASM = src/**/*.asm text
*.HEX = build/*.hex binary

# Default mode for unmapped files
default_mode = text

# Drive mappings (optional)
drive_A = ./
drive_B = /home/user/cpm_files/

# Debug options
debug = false
```

## Implementation Plan

### Phase 1: EOF Handling
1. Add file mode tracking (text/binary) to `OpenFile` structure
2. Implement `^Z` detection in sequential read for text files
3. Implement `^Z` padding in sequential write for text files

### Phase 2: EOL Conversion
1. Add line-ending conversion filters
2. Apply during read/write for text-mode files
3. Create buffer for conversion (may change byte count)

### Phase 3: Configuration File
1. Design .cfg file parser
2. Implement wildcard matching for file patterns
3. Add file type (text/binary) specification
4. Support glob patterns (`**/*.bas`)

### Phase 4: Auto-Detection Heuristics
Since there's no foolproof way to detect text vs binary:
- Check file extension (.BAS, .MAC, .ASM, .TXT → text)
- Check for binary extensions (.COM, .OVL, .HEX → binary)
- Scan first 512 bytes for control characters (heuristic)
- Default to binary if unsure (safer)

## Testing Resources

### Available Test Files

1. **MBASIC Test Suite**: `/home/wohl/cl/mbasic/tests/`
   - Over 100 .bas test files (2753 lines total)
   - Already in CP/M format with `\r\n` line endings
   - Written for testing modern MBASIC interpreter emulation
   - Examples: printsep.bas, test_while_for_mix.bas, recurse.bas

2. **Classic BASIC Programs**: `/home/wohl/cl/mbasic/site-dev/library/`
   - Business applications (budget, finance, mortgage, etc.)
   - Data management tools (database utilities)
   - Utilities (sort, convert, charfreq, etc.)
   - Organized by category

3. **Notable Programs**:
   - `/home/wohl/cl/mbasic/superstartrek.bas` - Classic Star Trek game
   - `/home/wohl/cl/mbasic/com/trand.bas` - Random number test

### Testing Strategy

1. Test with m80.com assembler (requires proper `\r\n`)
2. Test MBASIC loading .BAS files from `/home/wohl/cl/mbasic/tests/`
3. Test binary file operations (.COM files)
4. Test file creation and round-trip (write then read)
5. Run comprehensive test suite:
   ```bash
   # Simple test
   ./cpm_emulator com/mbasic.com /home/wohl/cl/mbasic/tests/printsep.bas

   # With configuration file mapping entire test directory
   ./cpm_emulator mbasic_tests.cfg
   ```

## Reference: tnylpo Approach

tnylpo includes `tnylpo-convert` utility:
- Standalone tool for manual conversion
- User explicitly converts files before use
- Simple but requires extra steps

Our approach:
- Automatic conversion based on configuration
- More convenient for users
- Requires careful configuration

## Edge Cases to Handle

1. **Mixed line endings in source file**: Handle gracefully
2. **Binary data that looks like text**: Use configuration
3. **Files without extensions**: Default to binary
4. **Large files**: Stream conversion, don't load all in memory
5. **Partial record writes**: Pad correctly

## Command Line Usage

```bash
# Simple mode (auto-detect, may not work for all programs)
./cpm_emulator mbasic.com

# With configuration file
./cpm_emulator mbasic.cfg

# Configuration file specifies program and all mappings
```

## Notes for Future Enhancement

- Consider supporting CP/M 3.0 timestamps
- Add support for random access files (r0-r2 in FCB)
- Support for user numbers (0-15)
- Multiple drive support with directory mapping
