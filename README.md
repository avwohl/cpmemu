# CP/M Emulator

cpmemu runs CP/M 2.2 `.com` programs on a modern machine. It emulates the Intel
8080 and Zilog Z80 and answers BDOS and BIOS calls against host files and the
host console, so there is no disk image to manage: CP/M programs read and write
ordinary files in ordinary directories, which is what makes it convenient for
running and testing CP/M compilers and their test suites. The CPU core, qkz80,
also installs as a standalone library.

**Supported platforms:** Linux (x64, ARM64), macOS (arm64, x64) and Windows (x64).

## Features

- **Dual CPU modes**: Z80 (default) and 8080 instruction sets
- **Host-file I/O**: CP/M file operations map to the host filesystem, with any
  host path or length mapped into a fake 8.3 CP/M name
- **Drive letters**: `drive_A`..`drive_P` back a CP/M drive with a host
  directory, and a configured drive is confined to it
- **Text/binary mode**: EOL conversion between CP/M and Unix for the files the
  configuration makes text, guessed by name and content only where it says `auto`
- **Terminal output**: ADM-3A escape sequences and the Kaypro `ESC G` attribute
  byte are translated to ANSI/VT100
- **Device redirection**: printer and auxiliary I/O
- **Configuration files**: file mappings and complex setups
- **^C handling**: Ctrl+C reaches the guest; five within two seconds exit the
  emulator
- **qkz80 library**: the CPU core installs with a `pkg-config` entry

## Install

Debian/Ubuntu (use `cpmemu_arm64.deb` on ARM64):

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu_amd64.deb
sudo dpkg -i cpmemu_amd64.deb
```

RHEL/Fedora (use `cpmemu.aarch64.rpm` on ARM64):

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu.x86_64.rpm
sudo rpm -i cpmemu.x86_64.rpm
```

macOS 12 or later:

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu-macos-universal.tar.gz
tar xzf cpmemu-macos-universal.tar.gz
xattr -dr com.apple.quarantine cpmemu-*-Darwin-arm64-x86_64
sudo cp cpmemu-*-Darwin-arm64-x86_64/bin/* /usr/local/bin/
```

From source, with a C++11 compiler:

```bash
make -C src
sudo make -C src install
```

[docs/install.md](docs/install.md) has the Windows MSIX package and the notes
for every platform.

## Usage

```
cpmemu [options] <program.com|config.cfg> [args...]
```

```
cpmemu program.com                  # Z80, the default
cpmemu --8080 program.com           # 8080 mode
cpmemu program.com file.dat         # goes in the command tail and FCB 1, uppercased
cpmemu config.cfg                   # settings from a config file
```

[docs/usage.md](docs/usage.md) lists every option, the environment variables
and the config file format.

## Documentation

- [docs/install.md](docs/install.md) - packages for every platform, and the build from source
- [docs/usage.md](docs/usage.md) - options, environment variables, config files
- [docs/console.md](docs/console.md) - ^C handling, line editing, terminal translation, Windows keys, known limitations
- [docs/CPM_SUPPORT.md](docs/CPM_SUPPORT.md) - the BDOS and BIOS tables and the memory map
- [docs/qkz80_library.md](docs/qkz80_library.md) - the qkz80 CPU library, and the sibling projects that compile qkz80
- [docs/testing.md](docs/testing.md) - running the test suite
- [docs/repository_layout.md](docs/repository_layout.md) - the source tree, and where `cpm_disk` ships
- [CHANGELOG.md](CHANGELOG.md) - changes, from v4.7.0 on
- [docs/BUILDING.md](docs/BUILDING.md) - every platform, CMake, MinGW, cross-compiling, packaging
- [examples/README.md](examples/README.md) - the config-file reference
- [tests/README.md](tests/README.md) - the test suite
- [MANUAL_CHECKS.md](MANUAL_CHECKS.md) - what needs a person at a keyboard
- [docs/qkz80_interrupts.md](docs/qkz80_interrupts.md) - interrupts in the CPU core
- [docs/console_seven_bit.md](docs/console_seven_bit.md) - why console input is seven bits
- [docs/cpm_disk_formats.md](docs/cpm_disk_formats.md) - CP/M on-disk structures
- [docs/file_handling_notes.md](docs/file_handling_notes.md) - mode detection and file search order (its mapping section is older than [examples/README.md](examples/README.md), which is the reference)
- [docs/macos-signing.md](docs/macos-signing.md) - signing and notarizing the macOS release
- `todo.txt` - open work

GNU General Public License v3.0 - see [LICENSE](LICENSE).

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
