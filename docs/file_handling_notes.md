# CP/M File Handling and Configuration

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

**Solution:**
- When reading CP/M text files: stop at `^Z`
- When writing CP/M text files: pad to 128 bytes with `^Z`
- Track file type (text vs binary) per file

### 2. End-of-Line Handling

**CP/M:**
- Uses `\r\n` (0x0D 0x0A) for line endings
- M80 assembler and other tools expect this format

**Unix:**
- Uses `\n` (0x0A) only

**Solution:**
- Convert `\n` -> `\r\n` when reading Unix text files into CP/M
- Convert `\r\n` -> `\n` when writing CP/M text files to Unix
- Only apply to text files, not binary (controlled by `eol_convert`)

## Configuration File Format

Configuration files (`.cfg`) specify program settings, file mappings, and modes.

### Basic Directives

```ini
# Program to run (required)
program = /path/to/program.com

# Command-line arguments to pass to program
args = ARG1 ARG2 ARG3

# Change to directory before running
cd = /path/to/working/directory

# Default file mode: auto, text, or binary
default_mode = auto

# Enable EOL conversion for text files (default: true)
eol_convert = true

# Enable debug output
debug = false
```

### Drive Mappings

Map CP/M drives (A:-P:) to Unix directories:

```ini
drive_A = .
drive_B = /home/user/cpm_files
drive_C = ${HOME}/cpm/programs
```

When a CP/M program accesses `B:TEST.BAS`, it will look for the file in `/home/user/cpm_files/test.bas`.

### File Mappings

File mappings specify how CP/M filenames map to Unix files and set their mode.

**Syntax:** `CPM_PATTERN = [unix_path] [text|binary]`

#### Mode-Only Mappings

Set the file mode for patterns without specifying a path. Files are found via normal search (current directory or drive mapping).

```ini
# All .BAS files are text
*.BAS = text

# All .DAT files are binary
*.DAT = binary

# Symbol files are text
*.SYM = text
```

#### Directory Mappings

Look for files matching the pattern in a specific directory:

```ini
# Find .BAS files in this directory
*.BAS = /home/user/basic text

# Find any matching file in a specific location
*.MAC = /home/user/asm text
```

#### Exact File Mappings

Map specific CP/M filenames to specific Unix paths:

```ini
# Map specific files
TEST.BAS = /home/user/projects/test.bas text
DATA.DAT = ./data/mydata.dat binary
STARTREK.BAS = /home/user/games/superstartrek.bas text
```

### Device Redirection

```ini
printer = /tmp/printer.txt
aux_input = /path/to/input.txt
aux_output = /path/to/output.txt
```

### Environment Variables

Paths support `${VAR}` and `$VAR` syntax:

```ini
program = ${HOME}/cpm/mbasic.com
drive_B = $HOME/basic_programs
```

## Example Configuration Files

### MBASIC with Test Suite

```ini
# mbasic_tests.cfg
program = com/mbasic.com
args = TEST.BAS

# Text mode for BASIC files
*.BAS = text

# Drive mappings
drive_A = .
drive_B = /home/user/mbasic/tests

# Map specific games
STARTREK.BAS = /home/user/mbasic/superstartrek.bas text
```

### Assembler Setup

```ini
# asm.cfg
program = com/m80.com
cd = /tmp

# Assembly source files are text
*.MAC = text
*.ASM = text
*.LST = text

# Object/binary files
*.REL = binary
*.COM = binary

# Look for source in specific directory
*.MAC = ${HOME}/asm/src text
```

### Compiler with Output Directory

```ini
# compile.cfg
program = ${HOME}/cpm/compilers/hitech_c.com
cd = /tmp/build

# Source is text, output is binary
*.C = text
*.H = text
*.OBJ = binary
*.COM = binary

# Drive B is the source directory
drive_B = ${HOME}/projects/myapp/src
```

## Command Line Usage

```bash
# Run with config file
./cpmemu config.cfg

# Run with config and additional arguments (appended to config args)
./cpmemu config.cfg MYFILE.BAS

# Config with CPU mode option
./cpmemu --8080 config.cfg

# Show config file help
./cpmemu --help-cfg
```

## File Mode Detection

When `default_mode = auto`, the emulator uses these heuristics:

**Known text extensions:** .BAS, .MAC, .ASM, .TXT, .DOC, .LST, .PRN
**Known binary extensions:** .COM, .EXE, .OVL, .OVR, .SYS, .BIN, .DAT

For other files, it scans the first 512 bytes:
- If more than 5% are control characters (excluding CR, LF, TAB, ^Z), treat as binary
- Otherwise treat as text

## File Search Order

When a CP/M program opens a file (e.g., `TEST.BAS`):

1. Check explicit file mappings (exact CP/M name to Unix path)
2. Check pattern mappings (e.g., `*.BAS = /some/dir`)
3. Search in drive directory (if drive mapping exists)
4. Search in current directory (lowercase, then uppercase)

## Notes

- Pattern matching is case-insensitive
- Only `*.EXT` patterns are supported (not `TE*.BAS`)
- Environment variables are expanded in all path values
- Lines starting with `#` are comments
- Blank lines are ignored
