# CP/M Emulator

A CP/M 2.2 operating system emulator that runs legacy 8-bit CP/M applications on modern systems. Features both Intel 8080 and Zilog Z80 CPU emulation with comprehensive BDOS/BIOS support.

**Supported Platforms:** Linux (x64, ARM64) and Windows (x64)

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

### Windows

Download and install the MSIX package:
```powershell
# Download
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu.msix

# Install (double-click the file, or use PowerShell)
Add-AppPackage cpmemu.msix
```

After installation, `cpmemu` is available from any command prompt or PowerShell window.

### Linux (Debian/Ubuntu)

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu_amd64.deb
sudo dpkg -i cpmemu_amd64.deb
```

For ARM64 systems, use `cpmemu_arm64.deb` instead.

### Linux (RHEL/Fedora)

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu.x86_64.rpm
sudo rpm -i cpmemu.x86_64.rpm
```

For ARM64 systems, use `cpmemu.aarch64.rpm` instead.

### From Source

See [docs/BUILDING.md](docs/BUILDING.md) for detailed build instructions.

**Quick start (Linux):**
```bash
git clone https://github.com/avwohl/cpmemu.git
cd cpmemu/src
make
sudo cp cpmemu /usr/local/bin/
```

**Quick start (Windows with Visual Studio):**
```cmd
git clone https://github.com/avwohl/cpmemu.git
cd cpmemu\src
do_build.bat
```

## Usage

```
cpmemu [options] <program.com> [args...]
```

### Options

| Option | Description |
|--------|-------------|
| `--8080` | Run in 8080 mode (default) |
| `--z80` | Run in Z80 mode with full instruction set |
| `--progress[=N]` | Report progress every N million instructions (default: disabled; 100 if flag used without N) |

### Examples

**Windows:**
```cmd
cpmemu program.com
cpmemu --z80 program.com
cpmemu mbasic.com myprogram.bas
```

**Linux:**
```bash
cpmemu program.com
cpmemu --z80 program.com
cpmemu mbasic.com myprogram.bas
```

### Running Microsoft BASIC

```
> cpmemu mbasic.com
BASIC-80 Rev. 5.21
[CP/M Version]
Ok
10 PRINT "Hello, CP/M!"
20 END
RUN
Hello, CP/M!
Ok
SYSTEM
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
│   ├── os/
│   │   ├── platform.h     # Platform abstraction interface
│   │   ├── linux/         # Linux/POSIX implementation
│   │   └── windows/       # Windows implementation
│   ├── makefile           # Linux build
│   ├── Makefile.win       # Windows/MinGW build
│   ├── CMakeLists.txt     # CMake cross-platform build
│   └── do_build.bat       # Windows/MSVC build script
├── packaging/
│   └── windows/           # MSIX packaging for Windows Store
├── tests/                 # Test programs (.com and .asm)
├── com/                   # Sample CP/M programs (mbasic.com)
├── examples/              # Configuration file examples
└── docs/                  # Documentation and references
```
## Related Projects

- [80un](https://github.com/avwohl/80un) - Unpacker for CP/M compression and archive formats (LBR, ARC, squeeze, crunch, CrLZH)
- [cpmdroid](https://github.com/avwohl/cpmdroid) - Z80/CP/M emulator for Android with RomWBW HBIOS compatibility and VT100 terminal
- [ioscpm](https://github.com/avwohl/ioscpm) - Z80/CP/M emulator for iOS and macOS with RomWBW HBIOS compatibility
- [learn-ada-z80](https://github.com/avwohl/learn-ada-z80) - Ada programming examples for the uada80 compiler targeting Z80/CP/M
- [mbasic](https://github.com/avwohl/mbasic) - Modern MBASIC 5.21 Interpreter & Compilers
- [mbasic2025](https://github.com/avwohl/mbasic2025) - MBASIC 5.21 source code reconstruction - byte-for-byte match with original binary
- [mbasicc](https://github.com/avwohl/mbasicc) - C++ implementation of MBASIC 5.21
- [mbasicc_web](https://github.com/avwohl/mbasicc_web) - WebAssembly MBASIC 5.21
- [mpm2](https://github.com/avwohl/mpm2) - MP/M II multi-user CP/M emulator with SSH terminal access and SFTP file transfer
- [romwbw_emu](https://github.com/avwohl/romwbw_emu) - Hardware-level Z80 emulator for RomWBW with 512KB ROM + 512KB RAM banking and HBIOS support
- [scelbal](https://github.com/avwohl/scelbal) - SCELBAL BASIC interpreter - 8008 to 8080 translation
- [uada80](https://github.com/avwohl/uada80) - Ada compiler targeting Z80 processor and CP/M 2.2 operating system
- [uc80](https://github.com/avwohl/uc80) - ANSI C compiler targeting Z80 processor and CP/M 2.2 operating system
- [ucow](https://github.com/avwohl/ucow) - Unix/Linux Cowgol to Z80 compiler
- [um80_and_friends](https://github.com/avwohl/um80_and_friends) - Microsoft MACRO-80 compatible toolchain for Linux: assembler, linker, librarian, disassembler
- [upeepz80](https://github.com/avwohl/upeepz80) - Universal peephole optimizer for Z80 compilers
- [uplm80](https://github.com/avwohl/uplm80) - PL/M-80 compiler targeting Intel 8080 and Zilog Z80 assembly language
- [z80cpmw](https://github.com/avwohl/z80cpmw) - Z80 CP/M emulator for Windows (RomWBW)

## See Also

- [RomWBW](https://github.com/wwarthen/RomWBW) - The original RomWBW project by Wayne Warthen

