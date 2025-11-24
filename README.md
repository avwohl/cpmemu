# CP/M Emulator

A CP/M 2.2 operating system emulator that runs legacy 8-bit CP/M applications on modern Linux systems. Features both Intel 8080 and Zilog Z80 CPU emulation with comprehensive BDOS/BIOS support.

## Features

- **Dual CPU modes**: Intel 8080 (default) and Zilog Z80 instruction sets
- **Complete CP/M environment**: BDOS and BIOS function emulation
- **File I/O translation**: Maps CP/M file operations to Unix filesystem
- **Text/binary mode**: Automatic EOL conversion between CP/M and Unix
- **Device redirection**: Printer and auxiliary I/O device support
- **Configuration files**: Support for complex setups and file mappings

## Building

```bash
cd src/
make
```

**Requirements:**
- C++11 compatible compiler (gcc or clang)
- POSIX-compatible system (Linux)
- No external dependencies

## Usage

```bash
./src/cpmemu [options] <program.com> [args...]
```

### Options

| Option | Description |
|--------|-------------|
| `--8080` | Run in 8080 mode (default) |
| `--z80` | Run in Z80 mode with full instruction set |
| `--progress[=N]` | Report progress every N million instructions (default: 100) |

### Examples

```bash
# Run a CP/M program
./src/cpmemu program.com

# Run with arguments
./src/cpmemu program.com input.dat output.dat

# Run in Z80 mode
./src/cpmemu --z80 program.com

# Run with progress reporting
./src/cpmemu --progress program.com

# Run Microsoft BASIC
./src/cpmemu com/mbasic.com
```

## Environment Variables

| Variable | Description |
|----------|-------------|
| `CPM_PROGRESS=N` | Progress reporting every N million instructions |
| `CPM_PRINTER` | File path for LIST device (printer) output |
| `CPM_AUX_IN` | File path for Reader device input |
| `CPM_AUX_OUT` | File path for Punch device output |
| `CPM_BIOS_DISK` | Control BIOS disk behavior: `ok`, `fail`, or `error` |
| `CPM_DEBUG_BDOS` | Debug specific BDOS functions (comma-separated numbers) |
| `CPM_DEBUG_BIOS` | Debug specific BIOS offsets (comma-separated numbers) |

## Configuration Files

For complex setups, use a `.cfg` file:

```ini
# Program to run
program = /path/to/program.com

# File mode settings
default_mode = auto      # auto, text, or binary
eol_convert = true       # Convert Unix \n <-> CP/M \r\n

# Device redirection
printer = /tmp/printer.txt
aux_input = /tmp/input.txt
aux_output = /tmp/output.txt

# File mappings (supports environment variables)
# *.BAS = ${HOME}/basic/*.bas text
# DATA.DAT = /path/to/data.dat binary
```

Run with: `./src/cpmemu config.cfg`

## Supported CP/M Functions

### BDOS Functions

| # | Function | Status |
|---|----------|--------|
| 0 | System Reset | Supported |
| 1 | Console Input | Supported |
| 2 | Console Output | Supported |
| 3-5 | Auxiliary/List I/O | Supported |
| 6 | Direct Console I/O | Supported |
| 7-8 | Get/Set IOBYTE | Supported |
| 9 | Print String | Supported |
| 10 | Read Console Buffer | Not implemented |
| 11 | Console Status | Supported |
| 12 | Get Version | Supported |
| 13-14 | Reset/Select Disk | Supported |
| 15-16 | Open/Close File | Supported |
| 17-18 | Search First/Next | Supported |
| 19 | Delete File | Supported |
| 20-21 | Read/Write Sequential | Supported |
| 22 | Make File | Supported |
| 23 | Rename File | Supported |
| 24-32 | Disk Operations | Supported |
| 33-34 | Read/Write Random | Supported |
| 35 | Compute File Size | Supported |
| 36 | Set Random Record | Supported |
| 37 | Reset Drive | Supported |
| 40 | Write Random Zero Fill | Supported |

### BIOS Functions

- Console I/O: CONST, CONIN, CONOUT
- Device I/O: LIST, PUNCH, READER, LISTST
- Disk Operations: SELDSK, SETTRK, SETSEC, SETDMA, READ, WRITE

## CP/M Memory Layout

```
0x0000-0x0004  Bootstrap vector to WBOOT
0x0003         IOBYTE (device control)
0x0004         Current drive/user
0x0005         Entry point to BDOS
0x005C-0x006B  Default FCBs
0x0080-0x00FF  Default DMA buffer (command line)
0x0100-0xFBFF  TPA (Transient Program Area)
0xFC00         CCP (Console Command Processor)
0xFD00         BDOS jump table
0xFE00         BIOS jump table
```

## Testing

```bash
cd src/
make test                                    # Run quick tests
timeout 180 ./cpmemu ../tests/zexdoc.com     # Z80 documented instruction test
timeout 180 ./cpmemu ../tests/zexall.com     # Z80 all instructions test
```

The `tests/` directory contains various test programs including:
- Console and flag tests
- Zexdoc/Zexall Z80 instruction verification
- 8080-specific tests in `tests/8080/`

## Project Structure

```
cpmemu/
├── src/
│   ├── cpmemu.cc          # Main emulator and CP/M system
│   ├── qkz80.h/cc         # Z80/8080 CPU core
│   ├── qkz80_reg_set.*    # Register set implementation
│   ├── qkz80_mem.*        # Memory management
│   └── makefile           # Build configuration
├── tests/                 # Test programs (.com and .asm)
├── com/                   # Sample CP/M programs (mbasic.com)
├── examples/              # Configuration file examples
└── docs/                  # Development documentation
```

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.
