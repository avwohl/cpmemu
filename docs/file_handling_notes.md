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

# Change to directory before running
cd = /path/to/working/directory

# Default file mode: auto, text, or binary
default_mode = auto

# Enable EOL conversion for text files (default: true)
eol_convert = true

# Enable debug output
debug = false
```

### File Mappings

File mappings specify how CP/M filenames map to Unix files and set their mode.

**Syntax:** `CPM_PATTERN = unix_path [text|binary]`

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

`drive_A` through `drive_P` back a CP/M drive letter with a host directory.
A configured drive is confined to it: `B:MISSING.TXT` fails rather than
falling back to the working directory. An unconfigured drive is the working
directory, which is what every drive letter was before this existed. See
`examples/README.md` for the full rules.

## Example Configuration Files

### MBASIC with Test Suite

```ini
# mbasic_tests.cfg
program = /path/to/mbasic.com

# Map BASIC files to test directory
*.BAS = /home/user/mbasic/tests text

# Map specific games
STARTREK.BAS = /home/user/mbasic/superstartrek.bas text
```

### Assembler Setup

```ini
# asm.cfg
program = /path/to/m80.com
cd = /tmp

# Assembly source files in specific directory
*.MAC = ${HOME}/asm/src text
*.ASM = ${HOME}/asm/src text
```

### Compiler with Output Directory

```ini
# compile.cfg
program = ${HOME}/cpm/compilers/hitech_c.com
cd = /tmp/build

# Source files in specific directory
*.C = ${HOME}/projects/myapp/src text
*.H = ${HOME}/projects/myapp/src text
```

## Command Line Usage

```bash
# Run with config file
./cpmemu config.cfg

# Config with CPU mode option
./cpmemu --8080 config.cfg
```

## File Mode Detection

When `default_mode = auto`, the emulator checks the file extension:

**Known text extensions:** .BAS, .MAC, .ASM, .TXT, .DOC, .LST, .PRN, .Z80, .LIB
**Known binary extensions:** .COM, .EXE, .OVL, .OVR, .SYS, .BIN, .DAT, .SPR, .REL, .PRL, .RSP

Files with unrecognized extensions default to binary.

## File Search Order

When a CP/M program opens a file (e.g., `TEST.BAS`):

1. Check file mappings (pattern and exact matches from config)
2. Search in current directory (lowercase, then as-is)

## Notes

- Pattern matching is case-insensitive
- Only `*.EXT` patterns are supported (not `TE*.BAS`)
- Environment variables are expanded in all path values
- Lines starting with `#` are comments
- Blank lines are ignored
