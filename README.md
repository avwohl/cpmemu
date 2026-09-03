# CP/M Emulator

A CP/M 2.2 operating system emulator that runs legacy 8-bit CP/M applications
on modern systems. Features both Intel 8080 and Zilog Z80 CPU emulation, the
CP/M 2.2 BDOS, and BIOS console and device I/O; file I/O is handled at the BDOS
level, so the BIOS disk calls are stubs.

**Supported Platforms:** Linux (x64, ARM64), macOS (arm64, x64) and Windows (x64)

cpmemu emulates BIOS and BDOS calls and translates them to host OS file and
console calls. Most emulators
have a file on the OS containing a native CP/M file system.  Then, when
testing a compiler, it is necessary to import programs to the CP/M disk and export
test run results.  With cpmemu, all the files can be stored in the host
file system, which is more convenient to manage.

This translated file io emulator idea is not new. The tnylpo
package https://github.com/SvenMb/gbrein_tnylpo has been doing it since 2018.
However, tnylpo only works well with filenames that fit the 8.3 format.
Also, tnylpo comes with a conversion program to handle the EOL conversions.

cpmemu allows mapping files anywhere in the host file system
of any length with any characters into a fake 8.3 CP/M name.  This allows
better naming of compiler test suite programs.  Also, a config file can
be supplied for the file name mapping and type (text vs binary) for
each file.
## Features

- **Dual CPU modes**: Zilog Z80 (default) and Intel 8080 instruction sets
- **CP/M environment**: BDOS file/console functions and BIOS character I/O
- **File I/O translation**: Maps CP/M file operations to the host filesystem
- **Drive letters**: `drive_A` .. `drive_P` in a config file back a CP/M drive
  with a host directory; a configured drive is confined to that directory, an
  unconfigured one resolves against the working directory as before
- **Text/binary mode**: Automatic EOL conversion between CP/M and Unix
- **Terminal output**: ADM-3A escape sequences and control codes, and the
  Kaypro `ESC G` attribute byte, are translated to ANSI/VT100, so
  screen-addressing CP/M programs work in a modern terminal
- **Device redirection**: Printer and auxiliary I/O device support
- **Configuration files**: Support for complex setups and file mappings
- **^C handling**: Ctrl+C passes through to CP/M programs (e.g., to interrupt
  BASIC); five within two seconds exit the emulator, unless turned off with
  `--no-ctrl-c-exit` or `ctrl_c_exit = false`
- **qkz80 library**: the CPU core installs as a library with a `pkg-config`
  entry, so other programs can embed it

## Installation

### Windows

An MSIX package is published for some releases, built by hand rather than by
CI. It is unsigned - there is no `AppxSignature.p7x` in it, and its identity is
the placeholder `CN=CPMEmuTest` - and Windows installs an MSIX only from a
signature it trusts, so double-clicking it does not install it. The most recent
one is in [v4.5.1](https://github.com/avwohl/cpmemu/releases/tag/v4.5.1):

```powershell
# curl.exe, not curl: in Windows PowerShell `curl` is an alias for
# Invoke-WebRequest, which has no -LO
curl.exe -LO https://github.com/avwohl/cpmemu/releases/download/v4.5.1/cpmemu.msix

# There is no signature to trust, so this needs Developer Mode turned on.
# Nobody here has a Windows machine to confirm it on.
Add-AppPackage -AllowUnsigned cpmemu.msix
```

That binary is from 2025-12-30 and predates the Windows console work: it takes
keys through `_getch()`, has no special-key table, and leaves the console in raw
mode if it is killed with Ctrl+Break. Little of the Console and Keyboard section
below applies to it - the five-^C exit and the RUB/^H line editing were already
there, but the terminal translation, the special-key table, the two-second
window and the ^C switches were not.

To get a *current* Windows build, build the MSIX from source with
[`packaging/windows/build-msix.ps1`](packaging/windows/build-msix.ps1) (it
builds with CMake and the Visual Studio 2022 generator, and needs the Windows 10
SDK for `makeappx`), or build `cpmemu.exe` directly with `src/Makefile.win`. See
[docs/BUILDING.md](docs/BUILDING.md).

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

Both packages carry more than the emulator: `/usr/bin/cpmemu`, the qkz80 CPU
core as `/usr/lib/libqkz80.a` and `/usr/lib/libqkz80.so`, its seven headers in
`/usr/include/qkz80/`, and `/usr/lib/pkgconfig/qkz80.pc`, so `pkg-config
--cflags --libs qkz80` answers `-I/usr/include/qkz80 -lqkz80` once one is
installed. README and LICENSE go to `/usr/share/doc/cpmemu/`. There is no
separate `-dev` package; see [The qkz80 library](#the-qkz80-library).

### macOS

The release workflow builds one universal binary covering both Apple silicon
and Intel, published as `cpmemu-macos-universal.tar.gz`. v4.7.0 is the first
release that carries it; v4.6.0 and earlier have none:

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu-macos-universal.tar.gz
tar xzf cpmemu-macos-universal.tar.gz
xattr -dr com.apple.quarantine cpmemu-*-Darwin-arm64-x86_64
sudo cp cpmemu-*-Darwin-arm64-x86_64/bin/cpmemu /usr/local/bin/
```

It is built with a macOS 12 deployment target, so macOS 12 is the oldest
release it will load on.

**Skipping the `xattr` line does not get you an error message.** A browser
download is tagged `com.apple.quarantine`, and macOS `tar` copies that tag onto
every file it extracts, so it ends up on `bin/cpmemu` as well as on the archive.
The binary is signed ad hoc and is not notarized, so Gatekeeper refuses a tagged
copy - `spctl -a -t exec` on it says `rejected` - and from a terminal that
refusal has no visible form at all. Measured on macOS 27: the process starts,
prints nothing, and sits in state `SN` until it is killed. The same binary with
the attribute removed runs immediately. The line costs nothing when there is
nothing to remove.

`xattr -l /usr/local/bin/cpmemu` shows whether the attribute is still there; an
empty answer means it is gone. Downloading with `curl`, as above, avoids the
whole thing: `curl` sets no quarantine attribute, and the line costs nothing
when there is nothing to remove.

Notarizing properly needs a Developer ID certificate, which needs a paid Apple
Developer Program membership that this project does not have. The `notarytool`
half is written and sits in `.github/workflows/release.yml` behind a check for
the certificate, so it skips; [`docs/macos-signing.md`](docs/macos-signing.md)
lists the five secrets that would turn it on, and is honest about the limit -
a notarization ticket cannot be stapled to a bare executable, so even signed,
a browser download would still need this line on a machine that is offline.

The archive also carries `libqkz80.a` and the headers, and deliberately no
dylib. See [The qkz80 library](#the-qkz80-library).

### From Source

See [docs/BUILDING.md](docs/BUILDING.md) for detailed build instructions.

**Quick start (Linux):**
```bash
git clone https://github.com/avwohl/cpmemu.git
cd cpmemu/src
make
sudo cp cpmemu /usr/local/bin/
```

**Quick start (macOS):**
```bash
git clone https://github.com/avwohl/cpmemu.git
cd cpmemu/src
make
sudo cp cpmemu /usr/local/bin/
```

Do not add `STATIC=1` there. macOS ships no `crt0.o` and no static libc or
libc++, so a static link cannot be made at all, and `-static-libstdc++` is
accepted and silently ignored - so the makefile refuses the flag rather than
hand you a dynamic binary you believe is static. For portability across macOS
versions, set `MACOSX_DEPLOYMENT_TARGET` instead.

**Quick start (Windows with Visual Studio):**
```cmd
git clone https://github.com/avwohl/cpmemu.git
cd cpmemu\src
do_build.bat
```

`do_build.bat` looks for `vcvarsall.bat` under `C:\Program Files\Microsoft
Visual Studio\18\Community` and stops if it is not there. For another edition
or version, change `VSDIR` at the top of the script, or take the CMake or MinGW
route in [docs/BUILDING.md](docs/BUILDING.md).

`make install` installs a second program, `cpm_disk`, for creating and editing
CP/M disk images; it is not in the `.deb` or the `.rpm`. See
[docs/cpm_disk_formats.md](docs/cpm_disk_formats.md).

## Usage

```
cpmemu [options] <program.com|config.cfg> [args...]
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
| `--save-memory=FILE` | Save memory to FILE on exit (for MOVCPM/SYSGEN). Written however the program finishes: BDOS 0, BIOS WBOOT, a jump to 0x0000, the five-^C exit or the end-of-input give-up |
| `--save-range=S-E` | Save only range S to E (hex, e.g., DC00-FFFF). Needs `--save-memory`; a range that does not parse is ignored without a message and the whole 64K is written |
| `--int-cycles=N` | Enable timer interrupt every N cycles (e.g., 50000) |
| `--int-rst=N` | RST number for the timer interrupt (0-7, default 7 = RST 38H). Only used when `--int-cycles` is given, and that also puts the CPU in IM 1, where every interrupt vectors to 0038h whatever N says - N takes effect only for a guest that switches itself to IM 0. N is masked to its low three bits rather than checked, so `--int-rst=9` is RST 1 |
| `--no-ctrl-c-exit` | Disable the five-consecutive-^C emulator exit |
| `--ctrl-c-exit` | Enable the five-consecutive-^C emulator exit (the default) |

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

### Running Microsoft BASIC

`mbasic.com` is not shipped here; supply your own copy.
`examples/mbasic_tests.cfg` runs one against a directory of `.bas` files.

```
> cpmemu mbasic.com
CPU mode: Z80
Loaded 24320 bytes from mbasic.com
BASIC-80 Rev. 5.21
[CP/M Version]
Ok
10 PRINT "Hello, CP/M!"
20 END
RUN
Hello, CP/M!
Ok
SYSTEM
Program exit via JMP 0
```

The `CPU mode`, `Loaded` and `Program exit` lines are the emulator's, on stderr.
The byte count is whatever your copy of mbasic.com is. SYSTEM warm boots by
jumping to 0000h, so the exit line is `Program exit via JMP 0`, not `System
reset`.

## Environment Variables

| Variable | Description |
|----------|-------------|
| `CPM_PROGRESS=N` | Progress reporting every N million instructions |
| `CPM_DEBUG` | Enable debug mode (set to `1`, `true`, or `yes`) |
| `CPM_PRINTER` | File for LIST device (printer) output, and for the `^P` console echo |
| `CPM_AUX_IN` | File for Reader device input; end of file, or no file, reads as `^Z` |
| `CPM_AUX_OUT` | File path for Punch device output |
| `CPM_BIOS_DISK` | BIOS disk stubs: `ok` (default) returns A = 0, `fail` returns A = 1 (the CP/M permanent error), `error` exits the emulator. Covers HOME, SETTRK, SETSEC, SETDMA, READ and WRITE; SECTRAN is implemented and answers the same in all three modes |
| `CPM_DEBUG_BDOS` | Trace these BDOS functions: comma-separated decimal function numbers |
| `CPM_DEBUG_BIOS` | Trace these BIOS entries: comma-separated decimal offsets into the jump table (6 CONST, 9 CONIN, 12 CONOUT, 15 LIST, 27 SELDSK) |

The environment is read after the config file, so `CPM_PRINTER`, `CPM_AUX_IN`
and `CPM_AUX_OUT` replace the `printer`, `aux_input` and `aux_output`
directives, and `CPM_DEBUG` can turn debugging on but not off. A `--progress`
flag outranks `CPM_PROGRESS`.

With no printer file, LIST output goes to stdout a character at a time as
`[PRINTER] c`; with no punch file, BIOS PUNCH writes `[PUNCH] c` and BDOS 4
discards the character. The Reader, Punch and LIST devices are seven-bit - the
high bit is stripped in both directions - so a file moved through them is not
byte-exact for eight-bit data.

## Configuration Files

For complex setups, use a `.cfg` file:

```ini
# Program to run
program = /path/to/program.com

# Working directory, applied at the line it appears on
cd = /path/to/work

# Drives: a CP/M drive letter backed by a host directory
drive_A = ${HOME}/cpm/work
drive_B = ${HOME}/cpm/basic

# File mode settings
default_mode = auto      # auto, text, or binary
eol_convert = true       # Convert Unix \n <-> CP/M \r\n

# Device redirection
printer = /tmp/printer.txt
aux_input = /tmp/input.txt
aux_output = /tmp/output.txt

# Console
ctrl_c_exit = true       # five consecutive ^C exit the emulator

# Diagnostics: mappings, BDOS calls, file operations
debug = false

# File mappings
# DATA.DAT = /path/to/data.dat binary   # one name to one file
# *.BAS    = ${HOME}/basic/*.bas        # '*' takes the name that matched
# *.DAT    = binary                     # a mode rule, not a location
```

Run with: `./src/cpmemu config.cfg`. Every directive the parser accepts, and
the forms a file mapping can take, are in
[examples/README.md](examples/README.md).

`$VAR` and `${VAR}` are expanded in the value of every directive, not just in
file mappings. An unset variable expands to nothing.

A `*` on the host side stands in for the text the CP/M pattern matched,
lowercased: `*.BAS = ${HOME}/basic/*.bas` opens `${HOME}/basic/printsep.bas`
for `PRINTSEP.BAS`. A host path with no `*` is used exactly as written, so
naming a directory there points every matching name at the directory itself -
the open succeeds and every read is EOF. A value that is only `text` or
`binary` says how matching names are read wherever they turn out to live, so it
composes with a drive rather than competing with it.

`cd` takes effect at the line it appears on, so a relative `printer`,
`aux_input` or `aux_output` written above it is opened in the old directory.
`program` is different: it is resolved only after the whole file has been read,
so a relative `program` is looked for in the directory `cd` moved to whichever
line it sits on. Give it an absolute path if that is not what you want.

Any key that is not a directive becomes a file mapping rather than an error.
`DEBUG = true` does not turn debug on - it becomes a mapping, and says so on
stderr, as does a key like `verbsoe = 1` whose value names nothing on disk. The
line still becomes a mapping either way, because a bare word is a legal CP/M
name.

A command-line flag overrides the config file: `--no-ctrl-c-exit` turns the exit
off even when the config file sets `ctrl_c_exit = true`. This holds wherever the
flag is written, before the config file or after it.

### Drives

`drive_A` through `drive_P` back a CP/M drive letter with a host directory.
With `drive_B = ${HOME}/cpm/basic`, `B:HELLO.BAS` is looked for in that
directory and nowhere else. A configured drive is confined to it:
`B:MISSING.TXT` fails rather than falling back to the working directory and
opening something unrelated, which is the failure that reads to the guest as
success. Open, make, delete, rename, file size and directory search all follow
the drive.

A drive with no `drive_` line means the working directory, as every drive
letter did before this existed, and selecting one is not an error. Real CP/M
answers `Bdos Err On X: Select`; this emulator has no disks behind the letters.
BDOS 24 reports A: plus every drive that has been configured or selected,
rather than claiming all sixteen exist.

### End of redirected input

When stdin is a file or a pipe and it runs out, the first BDOS 1 read still
returns CR, so a line the program was part way through submits. Every BDOS 1
read after that returns `^Z`, CP/M's end-of-input character, which is what BIOS
CONIN returns from the first read on - a program that checks for it stops on
its own. Function 10 ends the line it was collecting. BDOS 6 has no way to say
`^Z`: 0 is its answer for both "nothing waiting" and end of input, so a program
that only polls never sees the end. A program that reads on regardless is
stopped after 1024 consecutive reads past the end, with a message on stderr,
rather than being left to loop forever.

## Console and Keyboard

### The ^C Escape Hatch

Five consecutive ^C within two seconds exit the emulator. If `--save-memory` was
requested, the memory image is written before the emulator quits. The escape
hatch exists because the emulator takes ^C away from the host — ISIG cleared on
POSIX, `ENABLE_PROCESSED_INPUT` cleared on Windows — so ^C reaches the guest
instead.

On Windows there is one other way out: ctrl+break is not gated on
`ENABLE_PROCESSED_INPUT`, so it still ends the process. A console control
handler puts the console mode back on the way through. It writes no
`--save-memory` image; every exit listed under `--save-memory` above does.

Turn the hatch off with `--no-ctrl-c-exit`, or with `ctrl_c_exit = false` in the
config file, when the guest program binds ^C itself. WordStar binds ^C to
page-down, and five page-downs in a row would otherwise kill the emulator and
lose the unsaved document.

### Suspend and Signals

ISIG is cleared, so ^Z and ^\ are guest characters too — a typed ^Z reaches the
program as 1A — and the emulator cannot be suspended from the keyboard. A `kill
-TSTP` from another terminal still stops it, and so does starting it in the
background and bringing it forward with `fg`. Both put the terminal back before
the process stops and take it again on resume.

The terminal is also put back on the signals that end the process without
running exit handlers: SIGHUP, SIGINT, SIGQUIT, SIGTERM and the three crash
signals. Each handler re-raises with the default disposition, so the exit status
a caller sees is unchanged, and a signal already ignored — under `nohup`, or a
shell with job control off — stays ignored.

### Line Editing (BDOS Function 10)

Read Console Buffer consumes these keys:

| Key | Action |
|-----|--------|
| RUB / ^H | Delete the previous character |
| ^U | Cancel the line, echo `#` and start a new one |
| ^X | Cancel the current physical line, erasing it from the screen |
| ^E | Start a new physical line, keep collecting |
| ^R | Echo `#` and retype the line so far on a new line |
| ^P | Toggle console echo to the printer; does nothing unless a printer file is configured |
| ^S | Ignored |
| CR / LF | End the line |

Every other character below 0x80 reaches the program. Printable characters
arrive as themselves. Control characters, TAB and ESC included, are stored in
the buffer and echoed as `^x`. The read ends on CR, LF, end of input, or a full
buffer.

^C is not special here. Real CP/M 2.2 warm boots on a ^C in column one; a warm
boot in this emulator is program termination, so ^C is stored and echoed as `^C`
like any other control character and the program decides what it means. It still
counts toward the five-^C exit, so five of them at a function 10 prompt leave the
emulator.

### Terminal Output

Console output is translated from ADM-3A, the terminal most CP/M software was
written for, to the ANSI sequences a modern terminal understands. The
translation is always on; there is no flag to turn it off.

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

BS, BEL, CR and LF pass through unchanged. An escape sequence that is not in
the table passes through as ESC plus the character that followed it. A control
character below 0x20 that is not in the table is dropped, TAB included, so a
program that lays out columns with tabs loses them; DEL (0x7F) is written out.
The high bit is stripped from every byte, as on a real 7-bit console.

This covers BDOS 2, BDOS 6 output, BDOS 9 and BIOS CONOUT. It does not cover
the function 10 line editor's echo: those bytes are the user's own keystrokes
coming back, echoed in the `^x` form described above rather than run through
the translator.

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
| Ctrl+Up | ^W |
| Ctrl+Down | ^Z |

A special key that is not in the table is swallowed rather than passed through.
A translated PgDn does not count toward the ^C exit. At a function 10 prompt the
translated codes are given to the program rather than obeyed as line-editing
commands, so the Down arrow does not cancel the line being typed.

There is no equivalent on Linux or macOS. A POSIX terminal hands over the raw
escape sequence and nothing translates it, so Up reaches the guest as ESC [ A —
the three bytes 1B 5B 41 — and at a function 10 prompt those are stored and
echoed as `^[[A` rather than acting as WordStar's cursor-up. A guest that wants
arrow keys there has to decode the sequence itself.

### Known Limitations

**Console input is seven bits.** All four console read sites — BDOS 1, the BDOS
10 buffer store, BDOS 6 and BIOS CONIN — mask with `& 0x7F`, so no byte at or
above 0x80 reaches the guest intact. BDOS 3 and BIOS READER mask the same way,
which makes six read sites in all; those two are the Reader device, which is
seven-bit in both directions. The layers below do keep the eighth bit: POSIX
raw mode clears ISTRIP, and the Windows console path encodes the key in the
console input code page. The mask is above them. Measured: the UTF-8 for
e-acute, alpha and `A` piped at a hex-echo guest comes back as `43 29 4E 31 41`.

This is deliberate and it is not going to change. A CP/M program written for
this hardware expects seven bits, and the two things eight bits would buy —
accented characters and a byte-exact console — are not what the software in
this emulator's reach does. Dropping the masks is also not four line deletions:
BDOS 6 spells "no character" as 0, so it would need another way to say it, and
the code-page expectations in `tests/win_console.cc` are written against the
masked bytes.

What it costs, measured, so nobody has to rediscover it:

- On the polled path a byte that masks to 0x00 is dropped outright, because
  BDOS 6 reads 0 as "no character". Two input bytes do this, 0x00 and 0x80, and
  the byte is consumed rather than left waiting: `80 41` at a BDOS 6 guest
  yields `41` alone. CP/M 2.2 already spells "no character" 0, so a genuine NUL
  is ambiguous on real hardware too; what this emulator adds is the second byte.
- A guest that polls BIOS CONST until it says a key is ready and only then calls
  BDOS 6 gets 0 back for a typed 0x80. Status and read contradict each other.
- The mask is applied to what gets stored, after every raw-byte test, so a raw
  0x8D is stored as CR rather than ending a BDOS 10 line early, a raw 0xFF is
  stored as 0x7F rather than acting as rubout, and five raw 0x83 reach the guest
  as five `03` without tripping the five-^C exit.

The seven-bit cases in `tests/pty_console.cc` pin the console sites, over a pty
and - for the blocking read, where it separates this mask from a line discipline
that strips the bit - over a pipe as well, so the answer cannot change by
accident. The two Reader sites are documented here but asserted by nothing.

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
| 24 | Get Login Vector | Supported (A: plus every configured or selected drive) |
| 25-27 | Current Drive, Set DMA, Allocation Vector | Supported |
| 28-30 | Write Protect, Read-Only Vector, Set Attributes | Stub (accepted, nothing stored or enforced) |
| 31-32 | Get DPB, Get/Set User | Supported |
| 33-34 | Read/Write Random | Supported |
| 35 | Compute File Size | Supported |
| 36 | Set Random Record | Supported |
| 37 | Reset Drive | Supported (the DE bitmap is ignored; every open file closes) |
| 38 | Access Free Space | Stub (returns success) |
| 39 | Free Space | Stub (no-op) |
| 40 | Write Random Zero Fill | Supported |
| 48 | Flush Buffers | Supported (writes go straight to the host file, so this is a no-op) |

The allocation vector BDOS 27 points at is initialised all-free and never
updated, so a program that reads it to compute free space gets the same answer
whatever is on the drive. Any other function number prints
`Unimplemented BDOS function N` on stderr and returns 0xFF.

### BIOS Functions

- Console I/O: CONST, CONIN, CONOUT (implemented)
- Device I/O: LIST, PUNCH, READER, LISTST (implemented)
- SELDSK: returns the Disk Parameter Header at 0xFE33 for any drive A: to P:,
  and 0 above that. Every letter is selectable because an unconfigured drive
  means the working directory, not an absent disk.
- WBOOT: exits the emulator, as does a jump to 0x0000. BOOT is not implemented.
- SECTRAN: implemented, not a stub. BC is the logical sector and DE the
  translate table, and the physical sector comes back in HL: the byte at DE +
  BC with H = 0, which is the skeletal CBIOS lookup, or HL = BC when DE is 0,
  which is the no-translation convention that listing omits. It is arithmetic
  over the guest's own memory and reaches no media, so
  it cannot fail and does not answer to `CPM_BIOS_DISK` - all three modes give
  the same answer, and `error` no longer ends the run on a table lookup. The
  call leaves A untouched, as the skeletal listing does.
- HOME, SETTRK, SETSEC, SETDMA, READ, WRITE: stubs, returning A = 0 or A = 1
  per `CPM_BIOS_DISK`. Of the six, only READ and WRITE return a status in A;
  HOME, SETTRK, SETSEC and SETDMA return nothing, so the byte is simply unread
  there. File I/O is handled at the BDOS level, so a program that drives the
  disk through the BIOS will not work.

## CP/M Memory Layout

```
0x0000-0x0002  JMP to BIOS WBOOT
0x0003         IOBYTE (device control)
0x0004         Current drive/user
0x0005-0x0007  JMP to the BDOS entry
0x005C-0x007F  Default FCBs, the first at 0x005C and the second at 0x006C
0x0080-0x00FF  Default DMA buffer (command tail)
0x0100-0xFCFF  TPA (Transient Program Area); a .COM file is read into it
0xFD00         BDOS entry - a trap address, no code in memory
0xFE00-0xFE32  BIOS jump table, 17 entries of three bytes
0xFE33-0xFF51  BIOS disk workspace: DPH, DPB, directory buffer,
               allocation and check vectors
0xFFF0         Initial stack pointer
```

There is no CCP: nothing runs above the TPA, and a program that returns lands
back in the emulator rather than at a command prompt.

## Testing

```bash
tests/run_tests.sh          # quick tests, asserts and exits non-zero on failure
tests/run_tests.sh --zex    # adds zexdoc, zexall and 8080exm
tests/run_tests.sh --help   # the flags, and the measured run times
make -C src unit            # 8080-mode CPU unit tests, under a second
make -C src test            # three quick tests, eyeball only, never fails
```

`tests/run_tests.sh` is the one that can fail; `make test` prints output next
to a "should print" string for a human to check. zexdoc and zexall take about
seven minutes each and complete all 67 instruction groups with no CRC
mismatches; `tests/8080/8080exm.com`, which `--zex` runs under `--8080`, adds
25 more groups clean in about four minutes. Each exerciser is capped at an
hour; `CPMEMU_ZEX_TIMEOUT` (seconds) overrides that.

Do not cap the exercisers at 180 seconds, as an earlier version of this
section did: that is about five groups in, and a truncated run looks like a
finished one unless something checks for the "Tests complete" line.

Two parts of the suite need more than a compiler. The drive, mapping, config,
CLI, ADM-3A, save-memory, BIOS disk and SECTRAN guests are assembled at test
time, so 42 checks skip unless
`pasmo` or `z80asm` is on `PATH`. With `x86_64-w64-mingw32-g++` on `PATH` the
suite also cross-compiles the Windows half of the platform layer and fails on
any warning; without it, that step skips.

Both of those skip quietly and exit 0, which on a bare machine means a green
tick over three fifths of the suite. `tests/run_tests.sh --require` makes any
skip a machine could fix by installing something a failure instead, and names
what to install. `.github/workflows/ci.yml` runs the suite that way on
`ubuntu-latest`, runs it on `macos-latest`, and runs `tests\win_console.bat` on
`windows-latest`, on every push.

The terminal layer is unreachable through a pipe, so it has harnesses of its
own: `tests/pty_console.cc` gives the emulator a real pty and runs everywhere
except Windows, and `tests/win_console.cc` writes `INPUT_RECORD`s into a real
Windows console and runs only there. What neither can reach is in
[`MANUAL_CHECKS.md`](MANUAL_CHECKS.md); [`tests/README.md`](tests/README.md)
documents the whole suite.

Besides the console and flag programs, the suite checks CP/M drive letters
mapped to host directories, the file mapping forms, config diagnostics for a
mistyped or wrong-case key, emulator options written after the program name,
end of console input, the ADM-3A to ANSI translation, and 8080 mode against
the documented 8080 rules (`tests/unit_8080.cc`, 3.1 million ALU cases among
them).

## Project Structure

```
cpmemu/
├── src/
│   ├── cpmemu.cc          # Main emulator and CP/M system
│   ├── qkz80.h/cc         # Z80/8080 CPU core
│   ├── qkz80_reg_set.*    # Register set implementation
│   ├── qkz80_mem.*        # Memory management
│   ├── qkz80_errors.cc    # Error text for the CPU core
│   ├── os/
│   │   ├── platform.h     # Platform abstraction interface
│   │   ├── linux/         # POSIX implementation, used on macOS too
│   │   └── windows/       # Windows implementation
│   ├── makefile           # Linux and macOS build, and the qkz80 library
│   ├── Makefile.win       # Windows/MinGW build
│   ├── CMakeLists.txt     # CMake cross-platform build
│   ├── qkz80.pc.in        # pkg-config template, filled in by the makefile
│   └── do_build.bat       # Windows/MSVC build script
├── packaging/
│   └── windows/           # MSIX packaging for Windows Store
├── tests/                 # run_tests.sh, its guests and C++ harnesses
│   └── 8080/              # 8080 exercisers and their MACRO-80 sources
├── util/                  # cpm_disk disk-image utility
├── examples/              # Configuration file examples
├── docs/                  # Documentation and references
├── .github/workflows/     # release.yml, the deb, rpm and macOS release build
├── CHANGELOG.md           # Changes, from v4.7.0 on
├── MANUAL_CHECKS.md       # Checks that need a person at a keyboard
├── todo.txt               # Open work
└── LICENSE
```

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

From source, `make -C src libs` builds both libraries and `sudo make -C src
install-lib` installs them, the headers and `qkz80.pc` under `PREFIX`
(`/usr/local` by default); `LIBDIR`, `INCLUDEDIR`, `PKGCONFIGDIR` and `DESTDIR`
are honoured, and `qkz80.pc` is generated from whichever of them the install
used, so a moved `LIBDIR` does not leave the `.pc` naming a directory the files
are not in. `make -C src uninstall-lib` removes them.

```bash
c++ -std=c++11 $(pkg-config --cflags qkz80) prog.cc $(pkg-config --libs qkz80)
c++ -std=c++11 $(pkg-config --cflags qkz80) prog.cc $(pkg-config --static --libs qkz80)
```

`--static` adds the `-lstdc++` (`-lc++` on macOS) that `Libs.private` names,
because everything in the archive is C++ and a final link driven by `cc` fails
without it. Only `--static` reads that field, so the dynamic link is unaffected.

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
at instruction boundaries — `execute()` runs one instruction and delivers
nothing by itself. `cycles` counts a flat five per instruction, which is for
interrupt timing and not a cycle-accurate figure. See
[docs/qkz80_interrupts.md](docs/qkz80_interrupts.md).

Compiling the core with `-DQKZ80_NO_TRACE` compiles every trace call out of the
instruction decoder and roughly halves its text. It has to be defined when
`qkz80.cc` itself is compiled, so the shipped libraries are built with tracing
in and a consumer defining it in its own translation units changes nothing.

The shared library is `libqkz80.so` on Linux and `libqkz80.<major>.dylib` plus
an unversioned symlink on macOS; see [docs/BUILDING.md](docs/BUILDING.md).

## Who else compiles qkz80

`src/qkz80*.{cc,h}` is the CPU core, and three of the projects listed below
compile those source files directly out of a sibling working tree rather than
depending on a cpmemu *release*. An edit to `qkz80.cc` therefore lands in all
three on their next build, with no notification:

- **ioscpm** - 11 symlinks in `iOSCPM/Core/` pointing at `../cpmemu/src/qkz80*`.
  Built as Objective-C++ for iOS (`SDKROOT = iphoneos`, deployment target 15.0)
  at `c++17`/`gnu++20`; its `Tests/run_tests.sh` compiles the same files with
  `-std=c++11 -Wall`.
- **z80cpmw** - `z80cpmw/z80cpmw.vcxproj` compiles the four
  `$(SolutionDir)..\cpmemu\src\qkz80*.cc` in place and lists the seven headers,
  with MSVC at `/W3` and `/std:c++17`, and C4244 (narrowing) disabled on those
  four files.
- **romwbw_emu** - `src/makefile` resolves qkz80 in four steps:
  `QKZ80_CFLAGS`/`QKZ80_LIBS` set by the caller or by the gitignored
  `src/local.mk`; then pkg-config; then the sibling `../../cpmemu/src` for the
  headers and `libqkz80.a`; then `/usr/local`, with a warning. `make
  qkz80-source` prints which of the four a tree would take, without building
  anything. Builds with `-std=c++11 -Wall`.

Only romwbw_emu has a version gate, and only in CI: `release.yml` and `test.yml`
clone `avwohl/cpmemu` and check out a pinned `CPMEMU_REF`, `9a94e8d` at the
v4.7.0 tag. The pin is behind this tree, but nothing in `src/qkz80*.{cc,h}` has
changed since it, so the core its CI compiles is the one this release ships.

ioscpm and z80cpmw have no gate at all. Neither does a local romwbw_emu build,
but it need not reach this working tree to get there: a `qkz80.pc` left by
`make install-lib` here wins over the sibling directory, and a `local.mk` wins
over both.

06262ff is the mechanism working quietly in the good direction: it added
`QKZ80_NO_TRACE` and all three picked it up on their next build without being
told. It can work the other way just as quietly, so check the three before
changing qkz80's public surface. Two of the paths do build it the way this repo
does - romwbw_emu's makefile and ioscpm's `Tests/run_tests.sh`, both
`-std=c++11` with the host compiler, differing only in the warning set:
`-Wall -Wextra` here, a bare `-Wall` in ioscpm's script, and `-Wall` plus a
Clang-only `-Wimplicit-int-conversion` in romwbw_emu's makefile. The rest do
not: ioscpm's app targets are Objective-C++ at `c++17`/`gnu++20`, and z80cpmw
is `cl /W3 /std:c++17`. romwbw_emu's Windows CI job compiles only that
project's own sources with `cpmemu\src` on the include path, so cl sees qkz80's
headers there and none of its `.cc` files. Nothing in this repo's CI compiles
qkz80 with MSVC; only `src/do_build.bat` does, by hand on a Windows machine.

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
