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
- **Text/binary mode**: automatic EOL conversion between CP/M and Unix
- **Terminal output**: ADM-3A escape sequences and the Kaypro `ESC G` attribute
  byte are translated to ANSI/VT100
- **Device redirection**: printer and auxiliary I/O
- **Configuration files**: file mappings and complex setups
- **^C handling**: Ctrl+C reaches the guest; five within two seconds exit the
  emulator
- **qkz80 library**: the CPU core installs with a `pkg-config` entry

## Getting Started

### Debian/Ubuntu

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu_amd64.deb
sudo dpkg -i cpmemu_amd64.deb
```

Use `cpmemu_arm64.deb` on ARM64.

### RHEL/Fedora

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu.x86_64.rpm
sudo rpm -i cpmemu.x86_64.rpm
```

Use `cpmemu.aarch64.rpm` on ARM64. Both package families also install the qkz80
library and headers - see [The qkz80 library](#the-qkz80-library).

### macOS

macOS 12 or later.

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu-macos-universal.tar.gz
tar xzf cpmemu-macos-universal.tar.gz
xattr -dr com.apple.quarantine cpmemu-*-Darwin-arm64-x86_64
sudo cp cpmemu-*-Darwin-arm64-x86_64/bin/cpmemu /usr/local/bin/
```

The release is not notarized, so the `xattr` line is what stops Gatekeeper
refusing the binary. [docs/macos-signing.md](docs/macos-signing.md) has the
detail.

### Windows

An MSIX package is published for some releases, built by hand rather than by CI.
It is unsigned - identity `CN=CPMEmuTest` - so it installs only with Developer
Mode on:

```powershell
# curl.exe, not curl: in Windows PowerShell `curl` is an alias for
# Invoke-WebRequest, which has no -LO
curl.exe -LO https://github.com/avwohl/cpmemu/releases/download/v4.5.1/cpmemu.msix
Add-AppPackage -Path .\cpmemu.msix -AllowUnsigned
```

The most recent one is in
[v4.5.1](https://github.com/avwohl/cpmemu/releases/tag/v4.5.1). For a current
build, build from source with `src/do_build.bat` (MSVC) or package one with
`packaging/windows/build-msix.ps1`.

### From source

```bash
make -C src
```

Needs a C++11 compiler. This leaves the binary at `src/cpmemu`; `sudo make -C
src install` puts it on `PATH` (with `cpm_disk` beside it).
[docs/BUILDING.md](docs/BUILDING.md) covers every platform, CMake, MinGW and
cross-compiling. `STATIC=1` is refused on macOS, which has no static libc to
link against.

Then:

```bash
cpmemu program.com          # or ./src/cpmemu program.com, uninstalled
```

## Usage

```
cpmemu [options] <program.com|config.cfg> [args...]
```

Options may be written after the program or config file as well as before -
`cpmemu prog.cfg --no-ctrl-c-exit` works. Any argument that is not one of the
options below is passed to the guest untouched, which leaves a CP/M command tail
such as `TEST,TEST.COM/N/E` intact.

### Options

| Option | Description |
|--------|-------------|
| `--z80` | Z80 mode (default) |
| `--8080` | 8080 mode |
| `--progress[=N]` | Report progress every N million instructions (disabled by default; 100 if the flag is given without N) |
| `--save-memory=FILE` | Save memory to FILE on exit, for MOVCPM/SYSGEN. Written however the program finishes |
| `--save-range=S-E` | Save only range S to E (hex). Needs `--save-memory` [^1] |
| `--int-cycles=N` | Timer interrupt every N cycles |
| `--int-rst=N` | RST number for the timer interrupt (0-7, default 7 = RST 38H) [^2] |
| `--no-ctrl-c-exit` | Disable the five-consecutive-^C exit |
| `--ctrl-c-exit` | Enable it (the default) |

[^1]: A range that does not parse is ignored without a message and the whole
64K is written.
[^2]: Only used with `--int-cycles`, which also puts the CPU in IM 1, where
every interrupt vectors to 0038h whatever N says - N takes effect only for a
guest that switches itself to IM 0. N is masked to its low three bits rather
than checked, so `--int-rst=9` is RST 1.

### Examples

The command is the same on Linux, macOS and Windows.

```
cpmemu program.com                  # Z80, the default
cpmemu --8080 program.com           # 8080 mode
cpmemu program.com file.dat         # goes in the command tail and FCB 1, uppercased
cpmemu --progress=50 program.com    # report every 50M instructions
cpmemu config.cfg                   # settings from a config file
cpmemu config.cfg --no-ctrl-c-exit  # an option after the file still counts
```

`mbasic.com` is not shipped here; supply your own copy.
`examples/mbasic_tests.cfg` runs one against a directory of `.bas` files. The
emulator's own `CPU mode`, `Loaded` and `Program exit` lines go to stderr.

## Environment Variables

| Variable | Description |
|----------|-------------|
| `CPM_PROGRESS=N` | Progress reporting every N million instructions |
| `CPM_DEBUG` | Enable debug mode (`1`, `true` or `yes`) |
| `CPM_PRINTER` | File for LIST device output, and for the `^P` console echo |
| `CPM_AUX_IN` | File for Reader device input; end of file, or no file, reads as `^Z` |
| `CPM_AUX_OUT` | File for Punch device output |
| `CPM_BIOS_DISK` | BIOS disk stubs: `ok` (default) returns A = 0, `fail` returns A = 1, `error` exits the emulator |
| `CPM_DEBUG_BDOS` | Trace these BDOS functions: comma-separated decimal numbers |
| `CPM_DEBUG_BIOS` | Trace these BIOS entries: comma-separated decimal jump-table offsets |

The environment is read **after** the config file, so `CPM_PRINTER`,
`CPM_AUX_IN` and `CPM_AUX_OUT` replace the matching directives, and `CPM_DEBUG`
can turn debugging on but not off. A `--progress` flag outranks `CPM_PROGRESS`.

With no printer file, LIST output goes to stdout as `[PRINTER] c`; with no punch
file, BIOS PUNCH writes `[PUNCH] c` and BDOS 4 discards. The Reader, Punch and
LIST devices are seven-bit in both directions, so a file moved through them is
not byte-exact for eight-bit data.

## Configuration Files

A config file describes a whole run - the program, its file mappings, the
drives and the devices. It is a flat list of `key = value` lines: **there are no
`[section]` headers**, and a line without an `=` is reported as
`Config line N: invalid format (missing =)`.

```
# The program to run, and the directory to run it in.
program = mbasic.com
cd = /path/to/tests

# A drive letter is a host directory, and a configured drive is confined to it.
drive_A = /path/to/a
drive_B = /path/to/b

# Devices.
printer = out.prn

# Anything the loader does not recognise as a directive is a file mapping.
# A trailing `text` or `binary` sets the mode, separated by a SPACE.
TEST.BAS = my long test program.bas
OUT.DAT  = results.dat binary
```

`$VAR` and `${VAR}` are expanded in values. Every directive the parser accepts,
the mapping forms that do and do not work, and the drive rules are in
[examples/README.md](examples/README.md); `examples/` carries working files.

## Console and Keyboard

### The ^C escape hatch

Five consecutive ^C within two seconds exit the emulator, writing the
`--save-memory` image first if one was requested. The hatch exists because the
emulator takes ^C away from the host - ISIG cleared on POSIX,
`ENABLE_PROCESSED_INPUT` cleared on Windows - so ^C reaches the guest instead.

Turn it off with `--no-ctrl-c-exit`, or `ctrl_c_exit = false` in the config,
when the guest binds ^C itself: WordStar binds it to page-down, and five
page-downs in a row would otherwise kill the emulator.

On Windows ctrl+break is the other way out, since it is not gated on
`ENABLE_PROCESSED_INPUT`. It writes no `--save-memory` image.

ISIG is also cleared, so ^Z and ^\ are guest characters and the emulator cannot
be suspended from the keyboard; `kill -TSTP` and `fg` still work, and the
terminal is restored on HUP, INT, QUIT, TERM and the three crash signals.

### Line editing (BDOS function 10)

| Key | Action |
|-----|--------|
| RUB / ^H | Delete the previous character |
| ^U | Cancel the line, echo `#` and start a new one |
| ^X | Cancel the current physical line, erasing it from the screen |
| ^E | Start a new physical line, keep collecting |
| ^R | Echo `#` and retype the line so far on a new line |
| ^P | Toggle console echo to the printer; nothing unless a printer file is configured |
| ^S | Ignored |
| CR / LF | End the line |

Every other character below 0x80 reaches the program: printable ones as
themselves, control characters (TAB and ESC included) stored in the buffer and
echoed as `^x`.

^C is not special here. Real CP/M 2.2 warm boots on a ^C in column one; a warm
boot in this emulator is program termination, so ^C is stored and echoed like
any other control character and the program decides what it means. It still
counts toward the five-^C exit.

### Terminal output

Console output is translated from ADM-3A, the terminal most CP/M software was
written for, to ANSI. The translation is always on; there is no flag to turn it
off.

```
ESC *        clear the screen and home the cursor
ESC T        clear to end of line
ESC Y        clear to end of screen
ESC )        reverse video on
ESC (        reverse video off
ESC G n      Kaypro/Televideo attribute: 0 normal, 4 reverse, 2 dim,
             1 underline; anything else resets
ESC = r c    cursor to row r, column c, each byte biased by 32
^Z           clear the screen and home
^^           home the cursor
^K           cursor up
^L           cursor right
```

BS, BEL, CR and LF pass through. An escape sequence not in the table passes
through as ESC plus the character that followed it. A control character below
0x20 that is not in the table is dropped, **TAB included**, so a program that
lays out columns with tabs loses them; DEL (0x7F) is written out. The high bit
is stripped from every byte, as on a real 7-bit console.

This covers BDOS 2, BDOS 6 output, BDOS 9 and BIOS CONOUT. It does not cover the
function 10 line editor's echo, which is the user's own keystrokes coming back
in the `^x` form above.

### Windows special keys

On Windows a special key is translated to the WordStar diamond control code:

| Key | Code | Key | Code | Key | Code |
|-----|------|-----|------|-----|------|
| Up | ^E | Home | ^Q^S | Insert | ^V |
| Down | ^X | End | ^Q^D | Delete | ^G |
| Left | ^S | PgUp | ^R | Ctrl+Left | ^A |
| Right | ^D | PgDn | ^C | Ctrl+Right | ^F |
| | | | | Ctrl+Up | ^W |
| | | | | Ctrl+Down | ^Z |

A special key not in the table is swallowed. A translated PgDn does not count
toward the ^C exit. At a function 10 prompt the translated codes are given to
the program rather than obeyed as line-editing commands.

**There is no equivalent on Linux or macOS.** A POSIX terminal hands over the
raw escape sequence and nothing translates it, so Up reaches the guest as
`ESC [ A` and at a function 10 prompt is echoed as `^[[A`. A guest that wants
arrow keys there has to decode the sequence itself.

## Known Limitations

**Console input is seven bits.** The four console read sites - BDOS 1, the
BDOS 10 buffer store, BDOS 6 and BIOS CONIN - mask with `& 0x7F`, so no byte at
or above 0x80 reaches the guest intact. BDOS 3 and BIOS READER mask the same
way, which makes six read sites in all; those two are the Reader device. This is deliberate and is
not going to change: CP/M software expects seven bits, and BDOS 6 spells "no
character" as 0, so a byte that masks to 0x00 is dropped outright.
[docs/console_seven_bit.md](docs/console_seven_bit.md) has the measurements and
the consequences.

**Ctrl+V cannot reach the guest under Windows Terminal**, which binds it to
paste and does not fall through
([microsoft/terminal#16280](https://github.com/microsoft/terminal/issues/16280),
still open). No `SetConsoleMode` call changes this. Press **Insert**, which the
emulator translates to ^V, or unbind the key in `settings.json`:

```json
{ "keys": "ctrl+v", "command": "unbound" }
```

A Windows Terminal fragment extension cannot do this for you - fragments may
contribute profiles and colour schemes only. ^V reaches the guest normally on
Linux and macOS.

**Ctrl+C is shadowed by an active selection** on the same host. The emulator
clears `ENABLE_QUICK_EDIT_MODE`, so a stray click no longer starts one, but a
deliberate selection still shadows ^C until Esc clears it.

## CP/M Support

BDOS functions 0-40 and 48 are implemented, with 28-30, 38 and 39 as stubs;
41-47 are not implemented and return 0xFF with a message on stderr. File I/O is
handled at the BDOS level, so the BIOS disk calls (HOME, SETTRK, SETSEC, SETDMA,
READ, WRITE) are stubs and a program that drives the disk through the BIOS will
not work. There is no CCP: nothing runs above the TPA, and a program that
returns lands back in the emulator rather than at a command prompt.

The full BDOS and BIOS tables, the emulator's memory map, and what happens when
redirected input runs out, are in
[docs/CPM_SUPPORT.md](docs/CPM_SUPPORT.md).

## Testing

```bash
tests/run_tests.sh          # quick tests, asserts and exits non-zero on failure
tests/run_tests.sh --zex    # adds zexdoc, zexall and 8080exm
make -C src unit            # 8080-mode CPU unit tests, under a second
make -C src test            # three quick tests, eyeball only, never fails
```

`tests/run_tests.sh` is the one that can fail. 42 checks assemble their guests
at test time and skip unless `um80` and `ul80` are on `PATH`
(`pip install um80`); with `x86_64-w64-mingw32-g++` present the suite also
cross-compiles the Windows half of the platform layer. Both skip quietly and
exit 0, so `tests/run_tests.sh --require` turns any fixable skip into a failure
and names what to install - which is how `.github/workflows/ci.yml` runs it.

The terminal layer is unreachable through a pipe and has harnesses of its own:
`tests/pty_console.cc` (everywhere but Windows) and `tests/win_console.cc`
(Windows only). [`tests/README.md`](tests/README.md) documents the whole suite;
what no test can reach is in [`MANUAL_CHECKS.md`](MANUAL_CHECKS.md).

## The qkz80 library

The CPU core is a library as well as being linked into `cpmemu`. The `.deb` and
the `.rpm` install it beside the emulator; there is no separate `-dev` package.

```
/usr/lib/libqkz80.a
/usr/lib/libqkz80.so
/usr/lib/pkgconfig/qkz80.pc
/usr/include/qkz80/        seven headers, qkz80.h first
```

The macOS tarball carries `libqkz80.a` and the headers and no dylib: a dylib's
install name is an absolute path, so one unpacked wherever the user likes would
be a library dyld cannot find.

From source, `make -C src libs` builds both and `sudo make -C src install-lib`
installs them, the headers and `qkz80.pc` under `PREFIX` (`/usr/local`);
`LIBDIR`, `INCLUDEDIR`, `PKGCONFIGDIR` and `DESTDIR` are honoured, and
`qkz80.pc` is generated from whichever of them the install used.
`make -C src uninstall-lib` removes them.

```bash
c++ -std=c++11 $(pkg-config --cflags qkz80) prog.cc $(pkg-config --libs qkz80)
c++ -std=c++11 $(pkg-config --cflags qkz80) prog.cc $(pkg-config --static --libs qkz80)
```

`--static` adds the `-lstdc++` (`-lc++` on macOS) that `Libs.private` names,
because everything in the archive is C++ and a final link driven by `cc` fails
without it.

A minimal consumer:

```c++
#include "qkz80.h"

qkz80_cpu_mem mem;                    // plain 64K; subclass it for banking or I/O
qkz80 cpu(&mem);
cpu.set_cpu_mode(qkz80::MODE_8080);   // MODE_Z80 is the default
cpu.get_mem()[0x100] = 0x76;          // HALT
cpu.set_reg16(0x100, qkz80::regp_PC);
cpu.execute();                        // one instruction
```

`set_trace()` takes a `qkz80_trace` subclass. `int_pending`, `nmi_pending` and
`int_vector` are what `check_interrupts()` reads, and the caller has to call it
at instruction boundaries - `execute()` runs one instruction and delivers
nothing by itself. `cycles` counts a flat five per instruction, which is for
interrupt timing and is not a cycle-accurate figure. See
[docs/qkz80_interrupts.md](docs/qkz80_interrupts.md).

Compiling the core with `-DQKZ80_NO_TRACE` compiles every trace call out of the
instruction decoder and roughly halves its text. It has to be defined when
`qkz80.cc` itself is compiled, so the shipped libraries are built with tracing
in.

## Who else compiles qkz80

`src/qkz80*.{cc,h}` is the CPU core, and four sibling projects consume it out of
a neighbouring working tree rather than depending on a cpmemu *release* - three
by compiling the sources, and one linking the archive for the `.cc` files while
compiling the headers like everybody else. An edit to `qkz80.cc` lands in three
of them on their next build and in romwbw_emu once `libqkz80.a` is next built;
an edit to a header lands in all four on their next compile. No notification,
and no version gate:

- **ioscpm** - 11 symlinks in `iOSCPM/Core/` pointing at
  `../../../cpmemu/src/qkz80*`. Built as Objective-C++ for iOS at
  `c++17`/`gnu++20`; its `Tests/run_tests.sh` compiles the same files with
  `-std=c++11 -Wall`.
- **z80cpmw** - `z80cpmw/z80cpmw.vcxproj` compiles the four
  `$(SolutionDir)..\cpmemu\src\qkz80*.cc` in place, MSVC at `/W3` and
  `/std:c++17`, with C4244 disabled on those files.
- **cpmdroid** - `app/src/main/cpp/CMakeLists.txt` compiles the same four
  `${CPMEMU_SRC}/qkz80*.cc` in place, under the Android NDK.
- **romwbw_emu** - the odd one out for the `.cc` files only: it links
  `libqkz80.a` rather than compiling them, so an edit to `qkz80.cc` reaches it
  once that archive is rebuilt. A header edit does not wait for the archive.
  `QKZ80_CFLAGS` puts `-I` on a qkz80 source tree and `qkz80_reg_pair.h`'s
  inline `set_low()`/`set_high()` bodies compile into every one of its object
  files, which is why its own build reports `qkz80_reg_pair.h:32` and `:35`. It
  compiles at `-std=c++11 -Wall`, plus `-Wimplicit-int-conversion` and
  `-Wshorten-64-to-32` where the compiler accepts them - both probed, because
  GCC fails on an unknown `-W` rather than ignoring it. `src/makefile` resolves
  `QKZ80_CFLAGS` and `QKZ80_LIBS` four ways each - the caller, pkg-config, the
  sibling checkout, `/usr/local` - and `make qkz80-source` prints which it
  took. Only the caller and the sibling checkout reach this working tree; the
  other two name an install prefix, and an installed qkz80 - the `.pc` file
  pkg-config reads included - waits for `make install-lib`.

All four follow this repository's `main` rather than any tag, so a commit
pushed here is what their next build takes. **Check them before changing
qkz80's public surface.**

## Repository Layout

```
src/                the emulator, the qkz80 core, and the platform layer
                    under os/linux and os/windows; makefile, Makefile.win,
                    CMakeLists.txt and do_build.bat
tests/              run_tests.sh, its guests and the C++ harnesses; 8080/
                    holds the exercisers
util/               cpm_disk.py, its tests, and unreleased.sh
examples/           config file examples, and the config-file reference
packaging/windows/  MSIX packaging
docs/               the documents linked from this file
.github/workflows/  ci.yml (the test suite) and release.yml (deb, rpm, macOS)
```

`make install` also installs `cpm_disk`, the CP/M disk-image tool; the `.deb`
and `.rpm` do not carry it.

## Documentation

- [CHANGELOG.md](CHANGELOG.md) - changes, from v4.7.0 on
- [docs/BUILDING.md](docs/BUILDING.md) - every platform, CMake, MinGW, cross-compiling, packaging
- [docs/CPM_SUPPORT.md](docs/CPM_SUPPORT.md) - the BDOS and BIOS tables and the memory map
- [examples/README.md](examples/README.md) - the config-file reference
- [tests/README.md](tests/README.md) - the test suite
- [MANUAL_CHECKS.md](MANUAL_CHECKS.md) - what needs a person at a keyboard
- [docs/qkz80_interrupts.md](docs/qkz80_interrupts.md) - interrupts in the CPU core
- [docs/cpm_disk_formats.md](docs/cpm_disk_formats.md) - CP/M on-disk structures
- [docs/file_handling_notes.md](docs/file_handling_notes.md) - mode detection and file search order (its mapping section is older than [examples/README.md](examples/README.md), which is the reference)
- [docs/macos-signing.md](docs/macos-signing.md) - signing and notarizing the macOS release
- `todo.txt` - open work

## License

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
