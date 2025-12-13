# CP/M Emulator Configuration Examples

This directory contains example configuration files for the CP/M emulator.

## Quick Start

```bash
# Run with a config file
./src/cpmemu examples/example.cfg

# Show config file help
./src/cpmemu --help-cfg
```

## Example Files

### example.cfg
Basic configuration template showing all available options.

### mbasic_tests.cfg
Run MBASIC with test files from multiple directories:
- Maps drive B: to the test directory
- Maps specific classic programs like STARTREK.BAS
- Sets all .BAS files to text mode

### assembler.cfg
M80 assembler workflow:
- Source files on drive B:
- Output to working directory
- Proper text/binary modes for assembly files

### compiler.cfg
Hi-Tech C development setup:
- Source on drive B:
- Include files on drive C:
- Build output in working directory

## Configuration File Syntax

```ini
# Program to run (required)
program = path/to/program.com

# Arguments passed to program
args = ARG1 ARG2

# Working directory
cd = /path/to/dir

# File mode mappings
*.BAS = text           # Mode only
*.DAT = binary         # Mode only
*.MAC = /path text     # Directory + mode
TEST.BAS = /path/test.bas text  # Exact mapping

# Drive mappings
drive_A = .
drive_B = /path/to/files

# Settings
default_mode = auto    # auto, text, or binary
eol_convert = true     # Convert \n <-> \r\n
debug = false

# Device redirection
printer = /path/to/printer.txt
aux_input = /path/to/input.txt
aux_output = /path/to/output.txt
```

## Usage Patterns

### Pass arguments on command line
```bash
# Config args are used if no command-line args given
./src/cpmemu config.cfg

# Command-line args are appended to config args
./src/cpmemu config.cfg MYFILE.BAS
```

### Multiple source directories
Use drive mappings to access files from different locations:
```ini
drive_A = .                    # Current directory
drive_B = ${HOME}/source       # Source code
drive_C = ${HOME}/libraries    # Libraries
```

In CP/M program:
```
LOAD B:MYFILE.BAS       ; Load from source directory
SAVE "A:OUTPUT.BAS"     ; Save to current directory
```

### Text vs Binary Files

The emulator handles line ending conversion automatically:
- **Text files**: Convert `\n` (Unix) <-> `\r\n` (CP/M)
- **Binary files**: No conversion

Set modes explicitly to avoid conversion issues:
```ini
*.BAS = text    # BASIC source
*.MAC = text    # Assembly source
*.COM = binary  # Executables
*.REL = binary  # Object files
```

## See Also

- `docs/file_handling_notes.md` - Detailed documentation
- `./src/cpmemu --help-cfg` - Built-in configuration help
