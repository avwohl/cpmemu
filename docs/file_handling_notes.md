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
- Convert `\n` -> `\r\n` when reading Unix text files into CP/M (a `\r\n`
  already there stays `\r\n`)
- Convert `\r\n` -> `\n` when writing CP/M text files to Unix
- Only apply to text files, not binary, and only with `eol_convert = true`

The configuration decides, and only the configuration: a file's mode comes
from its mapping or mode rule, else `default_mode`, else - `auto` only - a
guess (see File Mode Detection), and `eol_convert` says whether a text file is
converted. A text file written with `eol_convert = true` reaches the host
converted whatever line ends it had there before, so a CR LF host file that
a program rewrites comes back all LF. With `eol_convert = false`, or a binary
mode, the records reach the host as the program wrote them, which is how to
keep CP/M's CR LF on the host.

### 3. Rewriting text in place

A CP/M program may read and write a text file's records in any order - the
classic append reads to the end, backs up a record and writes it again from
its `^Z`. Host text is shorter or longer than the CP/M text it stands for, so
the emulator does not write records into the host file where they fall. A
text file with conversion is held as the file a CP/M disk would hold - the
host text converted, padded with `^Z` to a record - and read and written as
that, sequentially or at random. What changes is written back to the host file
as text, converted as `eol_convert = true` says:

- the text to its first `^Z`, CR LF to LF (a lone CR or LF is written as it
  is), and nothing after it - no `^Z`, no padding;
- whatever form the host file had: a CR LF file, a `^Z`-padded one, or one
  whose lines end some CR LF and some LF comes back all LF, the lines no
  write reached included. Only the lines before both the first change and
  the first line the host file had in another form keep their host bytes, as
  they are already what the write back would make of them.

A file a program only reads is never written back, and nor is one whose
writes changed nothing before the text's `^Z`.

The write happens at once when the text from the start of the changed line to
the end is 64 KB or less - an append, a new file, any change to a small file -
and otherwise when the file is closed, at a disk reset or BDOS 48, before a
directory search, rename or file size, when the program ends, and when
SIGTERM, SIGHUP or SIGINT ends the run (POSIX; the process still dies of the
signal). A `SIGKILL` or a crash before then loses a change that was waiting.
The exit `CPM_BIOS_DISK=error` makes at a BIOS disk call closes every file
first, as the end of the run does. A close whose change cannot be written -
the host file read-only, the disk full - answers `FFh` and says so on stderr.
What a program writes after the text's first `^Z` is not text, and does not
reach the host file.

Every FCB open on one host file shares its image, whichever name or path it
was opened by. A file that opened as text because of what it holds (below)
has to hold text afterwards: a write that would leave it failing the text
rule - NULs in the text, from a record written past the end of text that
fills its last record, or a control character - makes the host file the
image itself, CR LF and padding included, which the next open reads binary,
record for record. That is decided again at every write back, so once the
NULs are written over it is host text again. A file the configuration makes
text - a mapping, a mode rule or `default_mode = text` - stays host text
whatever is written in it.

## Configuration File Format

Configuration files (`.cfg`) specify program settings, file mappings, and modes.

### Basic Directives

```ini
# Program to run (required)
program = /path/to/program.com

# Change to directory before running
cd = /path/to/working/directory

# The mode of every file a program opens or makes that no mapping or mode
# rule names: auto, text, or binary
default_mode = auto

# Enable EOL conversion for text files (default: true).  false keeps CP/M's
# CR LF and ^Z on the host.
eol_convert = true

# Enable debug output
debug = false
```

### File Mappings

File mappings specify how CP/M filenames map to Unix files and set their mode.

**Syntax:** `CPM_PATTERN = unix_path [text|binary]`

#### Pattern Mappings

A `*` on the host side stands in for what the CP/M pattern matched:

```
# PRINTSEP.BAS opens /home/user/basic/printsep.bas
*.BAS = /home/user/basic/*.bas text
*.MAC = /home/user/asm/*.mac text
```

**A host path with no `*` is used exactly as written** (`expand_unix_pattern`
returns it untouched), so `*.BAS = /home/user/basic` maps every `.BAS` to the
directory itself rather than to a file in it. There is no directory-mapping
form; to expose a directory, give it a drive letter or `cd` into it.
[examples/README.md](../examples/README.md) is the reference for mapping
forms, including the ones that look right and do nothing.

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
*.BAS = /home/user/mbasic/tests/*.bas text

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
./src/cpmemu config.cfg

# Config with CPU mode option
./src/cpmemu --8080 config.cfg
```

## File Mode Detection

A file's mode is the configuration's, for a file a program opens and for one
it makes alike:

1. the mapping with a host path that reached the file, if its line gave a
   mode - `text` or `binary`, or `default_mode` as it stood there;
2. a mode rule for the name (`*.LIB = binary`, `X.TXT = text`), the last
   one that matches;
3. `default_mode`, if it is `text` or `binary`.

A file a program makes or renames is decided the same way. A make puts the
file in the drive's directory and a rename leaves it in the old file's
directory, so a mapping with a host path decides for it when that path is the
file made or renamed to - `N.TXT = n.txt binary` for the `n.txt` a make of
`N.TXT` creates - and a mapping that reaches another file does not.

What the configuration calls text is text, and binary binary, without a look
at the file. Only when none of those says - `default_mode = auto` and no
mapping or rule for the name - does the emulator guess, from the extension:

**Text extensions:** .BAS, .MAC, .ASM, .TXT, .DOC, .LST, .PRN, .Z80, .LIB
**Binary extensions:** .COM, .EXE, .OVL, .OVR, .SYS, .BIN, .DAT, .SPR, .REL, .PRL, .RSP

Every name on the text list also names binary files - `.LIB` is a macro
library to MAC and RMAC and a REL library to LINK and L80, `.BAS` is an ASCII
program or a tokenized one, `.DOC` and `.TXT` are WordStar's document-mode
files - so for those the file itself decides. A file under a text extension
opens as text only if the bytes before its end (the first `^Z`, or a NUL with
nothing but NULs and `^Z`s after it) hold:

- no NUL and no control character other than BS, TAB, LF, VT, FF, CR and ESC;
- the end, the `^Z` or the NUL, in the last record: at most 128 bytes after
  it;
- UTF-8, which ASCII is - unless the lines end in bare LFs and none in CR LF,
  which is a host file with a Latin-1 or 8-bit character that still needs its
  LFs converted.

Anything else opens binary: the guest reads the bytes that are there, which
loses nothing. The first 64 KB of the file are looked at. A REL file opens
with a byte of 84h or 85h, which is not UTF-8, a tokenized MBASIC program with
FFh, and a WordStar document has 8Dh soft returns among CR LF hard ones. A
CP/M text file that fails the rule for a stray 8-bit byte or text after its
`^Z` reads the same either way, since its lines already end in CR LF.

Under `auto`, a file a program makes under a text extension is written as it
comes, like one under a name on neither list, and at its last close - or at a
disk reset or the end of the run, if the program never closes it - becomes
host text if it is text by the rule above. If the program wrote any of it at
random, its text must also read back as it was written - every LF after a CR -
and must not end in NULs, which host text would read back as `^Z`s, since a
random file's records have to stay what they were; what follows a `^Z` a text
open drops either way. A file written in sequence is converted as a text file
always was, a bare LF becoming a line end like any other. Until something has
been written in it, it stays undecided: opened again, by the FCB that made it
or another, it is still written as it comes. So a listing or an ASCII `SAVE
"X",A` lands as host text, and a tokenized `SAVE "X"` or a library written
directly under a `.LIB` name keeps its bytes. With `eol_convert = false`
nothing is converted, and a made file keeps the bytes it was written with.

Text that fails the rule only for being 8-bit - not UTF-8, its lines ending
CR LF, as CP/M text with a Latin-1 or code page 437 character in it does - is
host text too, at a close or a rename, if its text reads back as it was
written. Its host copy has bare LFs, which the rule accepts. A WordStar
document does not read back - its `8Dh` LF soft returns would gain a CR and
become hard ones - and is kept as written.

Files with unrecognized extensions default to binary: read and written as
they are, so a text file a program makes under such a name - `.HEX` from ASM,
`.SYM` from RMAC - keeps CP/M's CR LF line ends and `^Z` padding on the host.
Add a mode rule (`*.HEX = text`) for a name you know is text.

A file a program makes under an unrecognized name and then renames is decided
by the name it ends up with. PIP, ED and WordStar write `NAME.$$$` and rename
it when they are done; when the new name is a text one by the guess, the host
file is turned into host text at the rename if it is text and its text reads
back as it was written. A binary file renamed to a text name - DRI LIB's
`X.$$$` renamed `X.LIB` - is left as it was written.

With the configuration saying, it decides the rename too, and without a look:
a file made this run and written as it came - under `auto`, or as `binary`,
or with `eol_convert = false` - is converted at a rename to a name that is
text with `eol_convert = true` by a mapping, a mode rule or `default_mode`,
and is left as written at a rename to a name that is `binary` or has
`eol_convert = false`. So under `default_mode = text`, rules `*.$$$ = binary`
and `*.COM = binary` have PIP copy a program byte for byte and a text file to
host text. A file that was not made this run keeps its bytes at a rename.

A file still open when it is renamed - CP/M allows it, though PIP, ED and
WordStar close first - cannot be converted under the open stream, and is
decided at its last close, or at a disk reset or the end of the run, as a
file made under its new name is.

## File Search Order

When a CP/M program opens a file (e.g., `TEST.BAS`) - the mode it is read in
is under File Mode Detection, above:

1. Check file mappings (pattern and exact matches from config)
2. If the drive is configured (`drive_A`..`drive_P`), look in that directory
   **and stop there** - a configured drive is confined, and never falls back to
   the working directory
3. Otherwise search the working directory (lowercase, then as-is)

## Notes

- Pattern matching is case-insensitive
- `*.EXT`, `*` and `*.*` are supported; partial stems such as `TE*.BAS` are not. For `*` and `*.*` the whole name, extension included, stands in for a `*` on the host side
- Environment variables are expanded in all path values
- Lines starting with `#` are comments
- Blank lines are ignored
