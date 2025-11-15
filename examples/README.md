# CP/M Emulator Configuration Examples

This directory contains example configuration files for the CP/M emulator.

## Current Status

The basic emulator is working and can run `mbasic.com`. Configuration file support is **not yet implemented** but these examples show the planned functionality.

## Example Files

### simple_test.cfg
Minimal configuration for testing a single BASIC program.
```bash
./cpm_emulator examples/simple_test.cfg
```

### mbasic_tests.cfg
Comprehensive configuration mapping the entire MBASIC test suite.
```bash
./cpm_emulator examples/mbasic_tests.cfg
```

## Current Workaround

Until configuration files are implemented, you can:

1. **Direct execution** (limited):
   ```bash
   ./cpm_emulator com/mbasic.com
   ```

2. **Command line file mapping** (to be implemented):
   ```bash
   ./cpm_emulator com/mbasic.com TEST.BAS=/path/to/test.bas
   ```

3. **Copy files locally** (works now):
   ```bash
   cp /home/wohl/cl/mbasic/tests/printsep.bas ./test.bas
   ./cpm_emulator com/mbasic.com test.bas
   # In MBASIC: LOAD "TEST.BAS"
   ```

## Testing the Emulator

### Test Files Available

- **Test Suite**: `/home/wohl/cl/mbasic/tests/` - Over 100 .bas files with `\r\n` line endings
- **Classic Programs**: `/home/wohl/cl/mbasic/site-dev/library/` - Games and utilities
- **Notable**: `superstartrek.bas` - Classic Star Trek game

### File Format Notes

The test files in `/home/wohl/cl/mbasic/tests/` are **already in CP/M format**:
- Line endings: `\r\n` (CR+LF)
- EOF marker: May or may not have `^Z`
- Can be used directly once file I/O is fully implemented

## Future Enhancements

See `docs/file_handling_notes.md` for details on planned features:

1. ✅ Basic CP/M emulation (DONE)
2. ✅ MBASIC runs and displays prompt (DONE)
3. ⏳ File I/O (basic implementation, needs enhancement)
4. ⏳ ^Z EOF handling for text files
5. ⏳ EOL conversion for Unix files
6. ⏳ Configuration file parser
7. ⏳ Wildcard file mapping
8. ⏳ Text/binary mode per file

## Configuration File Format (Planned)

```ini
# Program to run
program = mbasic.com

# File mappings: CPM_NAME = unix_path [text|binary]
*.BAS = /path/to/basic/**/*.bas text
TEST.MAC = ~/asm/test.asm text
DATA.BIN = ./data.bin binary

# Settings
default_mode = text
eol_convert = true    # Convert Unix \n to CP/M \r\n
eof_marker = true     # Add/recognize ^Z in text files
debug = false

# Drive mappings
drive_A = .
drive_B = /home/user/cpm_files
```

## Todo List Priority

See main todo list for implementation order:
1. Handle ^Z EOF marker
2. EOL conversion
3. Config file parser
4. Wildcard support
5. Text/binary mode tracking
