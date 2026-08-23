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

- **Dual CPU modes**: Zilog Z80 (default) and Intel 8080 instruction sets
- **CP/M environment**: BDOS file/console functions and BIOS character I/O
- **File I/O translation**: Maps CP/M file operations to Unix filesystem
- **Text/binary mode**: Automatic EOL conversion between CP/M and Unix
- **Device redirection**: Printer and auxiliary I/O device support
- **Configuration files**: Support for complex setups and file mappings
- **^C handling**: Ctrl+C passes through to CP/M programs (e.g., to interrupt BASIC); press 5 times consecutively to exit emulator

## Installation

### Windows

An MSIX package is published for some releases; it is built by hand rather than
by CI, because signing needs a certificate the release workflow does not carry.
The most recent one is in
[v4.5.1](https://github.com/avwohl/cpmemu/releases/tag/v4.5.1):

```powershell
# Download
curl -LO https://github.com/avwohl/cpmemu/releases/download/v4.5.1/cpmemu.msix

# Install (double-click the file, or use PowerShell)
Add-AppPackage cpmemu.msix
```

After installation, `cpmemu` is available from any command prompt or PowerShell window.

To get a *current* Windows build, build the MSIX from source with
[`packaging/windows/build-msix.ps1`](packaging/windows/build-msix.ps1) (needs
CMake or MinGW plus the Windows 10 SDK), or build `cpmemu.exe` directly with
`src/Makefile.win`. See [docs/BUILDING.md](docs/BUILDING.md).

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

Options may also be written after the program or config file. They are applied
wherever they appear and kept out of the CP/M command tail, so
`cpmemu prog.cfg --no-ctrl-c-exit` works. Only the options listed below are
taken this way; anything else is passed to the program untouched, which leaves
a CP/M-style tail such as `TEST,TEST.COM/N/E` alone.

### Options

| Option | Description |
|--------|-------------|
| `--z80` | Run in Z80 mode (default) |
| `--8080` | Run in 8080 mode |
| `--progress[=N]` | Report progress every N million instructions (default: disabled; 100 if flag used without N) |
| `--save-memory=FILE` | Save memory to FILE on exit (for MOVCPM/SYSGEN) |
| `--save-range=S-E` | Save only range S to E (hex, e.g., DC00-FFFF) |
| `--int-cycles=N` | Enable timer interrupt every N cycles (e.g., 50000) |
| `--int-rst=N` | RST number for interrupt (0-7, default 7 = RST 38H) |
| `--no-ctrl-c-exit` | Disable the five-consecutive-^C emulator exit |
| `--ctrl-c-exit` | Enable the five-consecutive-^C emulator exit (the default) |

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
| `CPM_DEBUG` | Enable debug mode (set to `1`, `true`, or `yes`) |
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

# Console
ctrl_c_exit = true       # five consecutive ^C exit the emulator

# File mappings (supports environment variables)
# *.BAS = ${HOME}/basic text
# DATA.DAT = /path/to/data.dat binary
```

Run with: `./src/cpmemu config.cfg`

A command-line flag overrides the config file: `--no-ctrl-c-exit` turns the exit
off even when the config file sets `ctrl_c_exit = true`. This holds wherever the
flag is written, before the config file or after it.

## Console and Keyboard

### The ^C Escape Hatch

Five consecutive ^C within two seconds exit the emulator. If `--save-memory` was
requested, the memory image is written before the emulator quits. The escape
hatch exists because the emulator puts the terminal in raw mode with ISIG
cleared, so there is no other way out.

Turn it off with `--no-ctrl-c-exit`, or with `ctrl_c_exit = false` in the config
file, when the guest program binds ^C itself. WordStar binds ^C to page-down,
and five page-downs in a row would otherwise kill the emulator and lose the
unsaved document.

### Line Editing (BDOS Function 10)

Read Console Buffer consumes these keys:

| Key | Action |
|-----|--------|
| RUB / ^H | Delete the previous character |
| ^U | Cancel the line, echo `#` and start a new one |
| ^X | Cancel the current physical line, erasing it from the screen |
| ^E | Start a new physical line, keep collecting |
| ^R | Retype the line so far |
| ^P | Toggle console echo to the printer |
| ^S | Ignored |
| CR / LF | End the line |

Every other character reaches the program. Printable characters arrive as
themselves. Control characters, TAB and ESC included, are stored in the buffer
and echoed as `^x`. The read ends on CR, LF, end of input, or a full buffer.

### Windows Special Keys

On Windows, a special key is translated to the WordStar diamond control code:

| Key | Control code |
|-----|--------------|
| Up | ^E |
| Down | ^X |
| Left | ^S |
| Right | ^D |
| Home | ^Q^S |
| End | ^Q^D |
| PgUp | ^R |
| PgDn | ^C |
| Insert | ^V |
| Delete | ^G |
| Ctrl+Left | ^A |
| Ctrl+Right | ^F |

A special key that is not in the table is swallowed rather than passed through.
A translated PgDn does not count toward the ^C exit. At a function 10 prompt the
translated codes are given to the program rather than obeyed as line-editing
commands, so the Down arrow does not cancel the line being typed.

### Known Limitations

**Ctrl+V on Windows.** Windows Terminal is the default console host on Windows
11, and it binds `ctrl+v` to `Terminal.PasteFromClipboard`. That binding does
not fall through to the application
([microsoft/terminal#16280](https://github.com/microsoft/terminal/issues/16280),
still open). No `SetConsoleMode` call the emulator can make changes this. So ^V,
which is insert/overtype in WordStar, never reaches the guest there.

This affects Windows only. ^V reaches the guest normally on Linux and macOS.

There are two workarounds. Press Insert, which the emulator translates to ^V.
Or unbind the key in the Windows Terminal settings, by putting this in the
`actions` array of `settings.json`:

```json
{
  "keys": "ctrl+v",
  "command": "unbound"
}
```

A Windows Terminal fragment extension cannot do this on your behalf. Fragments
may contribute profiles and color schemes only, not keybindings, so this stays a
manual step.

**Ctrl+C when a selection is active.** On the same host, while a quick-edit
selection exists, `ctrl+c` copies instead of falling through. The emulator now
clears `ENABLE_QUICK_EDIT_MODE`, so a stray mouse click no longer starts a
selection. A deliberate selection still shadows ^C until it is cleared with Esc.

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
| 10 | Read Console Buffer | Supported |
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
tests/run_tests.sh          # quick tests, asserts and exits non-zero on failure
tests/run_tests.sh --zex    # adds zexdoc and zexall, about 7 minutes each
make -C src test            # three quick tests, eyeball only, never fails
```

`tests/run_tests.sh` is the one that can fail; `make test` prints output next
to a "should print" string for a human to check. Both zexdoc and zexall
currently complete all 67 instruction groups with no CRC mismatches.

Do not cap the exercisers at 180 seconds, as an earlier version of this
section did: that is about five groups in, and a truncated run looks like a
finished one unless something checks for the "Tests complete" line.

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
├── examples/              # Configuration file examples
└── docs/                  # Documentation and references
```
## Related Projects

- [80un](https://github.com/avwohl/80un) - Unpacker for the CP/M archive and compression formats LBR, ARC, squeeze, crunch, and CrLZH.
- [cpmdroid](https://github.com/avwohl/cpmdroid) - Z80/CP/M emulator for Android phones and tablets. It emulates the RomWBW HBIOS interface and a VT100 terminal.
- [ioscpm](https://github.com/avwohl/ioscpm) - Z80/CP/M emulator for iOS and macOS. It emulates the RomWBW HBIOS interface and runs CP/M 2.2 and CP/M 3.
- [learn-ada-z80](https://github.com/avwohl/learn-ada-z80) - Collection of more than 90 Ada example programs for uada80, the Ada compiler for the Z80 processor and CP/M.
- [mbasic](https://github.com/avwohl/mbasic) - Python interpreter for MBASIC 5.21, the Microsoft BASIC-80 for CP/M. Two compiler backends compile the programs to CP/M .COM files or to JavaScript.
- [mbasic2025](https://github.com/avwohl/mbasic2025) - Reconstruction of the lost source code of MBASIC 5.21, the Microsoft BASIC-80 for CP/M. The MACRO-80 source code assembles to a binary that matches mbasic.com byte for byte.
- [mbasicc](https://github.com/avwohl/mbasicc) - C++17 interpreter for MBASIC 5.21, the Microsoft BASIC-80 for CP/M. It runs on Linux and macOS.
- [mbasicc_web](https://github.com/avwohl/mbasicc_web) - Web browser interpreter for MBASIC 5.21, the Microsoft BASIC-80 for CP/M. Emscripten compiles the mbasicc interpreter to WebAssembly.
- [mpm2](https://github.com/avwohl/mpm2) - Z80 emulator for MP/M II, the multi-user CP/M operating system. Users connect over SSH, and SFTP clients transfer files.
- [romwbw_emu](https://github.com/avwohl/romwbw_emu) - Hardware-level Z80/CP/M emulator for Linux and macOS. It emulates the RomWBW HBIOS interface and switches banks in 512 KB of ROM and 512 KB of RAM.
- [scelbal](https://github.com/avwohl/scelbal) - Floating-point BASIC interpreter for the 8080 processor and CP/M. A translator converts the original 8008 source code to 8080 source code.
- [uada80](https://github.com/avwohl/uada80) - Ada compiler for the Z80 processor and CP/M 2.2. It compiles a subset of Ada 2012 to CP/M .COM files.
- [uc80](https://github.com/avwohl/uc80) - C compiler for the Z80 processor and CP/M. It optimizes for small code size.
- [ucow](https://github.com/avwohl/ucow) - Cowgol compiler for the Z80 processor and CP/M. It runs on Linux in Python.
- [um80_and_friends](https://github.com/avwohl/um80_and_friends) - Linux toolchain that is compatible with Microsoft MACRO-80. It has an assembler, a linker, a librarian, and a disassembler.
- [upeepz80](https://github.com/avwohl/upeepz80) - Peephole optimizer for Z80 compilers that write lowercase Z80 assembly language. It shortens jumps to jr, builds djnz loops, and removes dead stores.
- [uplm80](https://github.com/avwohl/uplm80) - PL/M-80 compiler for the Z80 processor and CP/M. It writes Intel 8080 and Zilog Z80 assembly language.
- [z80cpmw](https://github.com/avwohl/z80cpmw) - Z80/CP/M emulator for Windows. It emulates the RomWBW HBIOS interface and boots CP/M from disk images.

## See Also

- [RomWBW](https://github.com/wwarthen/RomWBW) - The original RomWBW project by Wayne Warthen

