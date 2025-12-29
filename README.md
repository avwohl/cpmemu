# CP/M Emulator

A CP/M 2.2 operating system emulator that runs legacy 8-bit CP/M applications on modern Linux systems. Features both Intel 8080 and Zilog Z80 CPU emulation with  comprehensive BDOS/BIOS support.

cpmemu emulates BIOS and BDOs calls and translates them to Unix.  Most emulators
have a file on the OS containing a native CP/M file system.  Then, when
testing a compiler, it is necessary to import programs to the CP/M disk and export
test run results.  With cpmemu, all the files can be stored in the linux
file system, which is more convenient to manage.

This translated file io emulator idea is not new. The tnylpo
package https://github.com/SvenMb/gbrein_tnylpo has been doing it since 2018.
However, tnylpo only works well with filenames that fit the 8.3 format.
Also, tnylpo comes with a conversion program to handle the EOL conversions.

cpmemu allows mapping files anywhere in the linux file system
of any length with any characters into a fake 8.3 CP/M name.  This allows
better naming of compiler test suite programs.  Also, a config file can
be supplied for the file name mapping and type (text vs binary) for
each file.
## Features

- **Dual CPU modes**: Intel 8080 (default) and Zilog Z80 instruction sets
- **CP/M environment**: BDOS file/console functions and BIOS character I/O
- **File I/O translation**: Maps CP/M file operations to Unix filesystem
- **Text/binary mode**: Automatic EOL conversion between CP/M and Unix
- **Device redirection**: Printer and auxiliary I/O device support
- **Configuration files**: Support for complex setups and file mappings
- **^C handling**: Ctrl+C passes through to CP/M programs (e.g., to interrupt BASIC); press 5 times consecutively to exit emulator

## Installation

### From Packages (Recommended)

**Debian/Ubuntu:**
```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu_amd64.deb
sudo dpkg -i cpmemu_amd64.deb
```

For ARM64 systems, use `cpmemu_arm64.deb` instead.

**RHEL/Fedora:**
```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu.x86_64.rpm
sudo rpm -i cpmemu.x86_64.rpm
```

For ARM64 systems, use `cpmemu.aarch64.rpm` instead.

### From Source

```bash
git clone https://github.com/avwohl/cpmemu.git
cd cpmemu/src
make
sudo cp cpmemu /usr/local/bin/
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
| `--progress[=N]` | Report progress every N million instructions (default: disabled; 100 if flag used without N) |

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
| 10 | Read Console Buffer | Stub (returns 0) |
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
| 38 | Access Free Space | Stub (returns success) |
| 39 | Free Space | Stub (no-op) |
| 40 | Write Random Zero Fill | Supported |

### BIOS Functions

- Console I/O: CONST, CONIN, CONOUT (implemented)
- Device I/O: LIST, PUNCH, READER, LISTST (implemented)
- Disk Operations: Stubs only (return success/fail per CPM_BIOS_DISK setting)

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
