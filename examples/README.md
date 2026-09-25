# CP/M Emulator Configuration Examples

Example configuration files for the CP/M emulator.

```bash
./src/cpmemu examples/example.cfg
```

## What a config file actually supports

Every directive below was checked against `CPMEmulator::load_config_file` in
`src/cpmemu.cc` and confirmed by running the emulator. Anything not in this
list is **not** a directive: unrecognised keys become file mappings, though a
key that looks like a mistyped directive is now reported (see below).

| Directive | Meaning |
| --- | --- |
| `program` | Program to run. Required. |
| `cd` / `chdir` | Change working directory. Applied immediately, in file order. |
| `default_mode` | `auto`, `text` or `binary`: the mode of every file a program opens or makes that no mapping or mode rule names. |
| `eol_convert` | `true`/`false`. Convert `\r\n` <-> `\n` for text files. `false` keeps CP/M's bytes on the host. |
| `debug` | `true`/`false`. Prints mappings, BDOS calls and file operations. |
| `ctrl_c_exit` | `true`/`false`. Whether five fast ^C quit the emulator. |
| `printer` | File to receive printer output. |
| `aux_input` | File to read for AUX input. |
| `aux_output` | File to receive AUX output. |
| `drive_A` … `drive_P` | Host directory behind a CP/M drive letter. |

`$VAR` and `${VAR}` are expanded in every value. An unset variable expands to
nothing, so `${MISSING}/mbasic.com` becomes `/mbasic.com` and fails with a
message naming that path.

### `cd` is applied while the file is being read

It takes effect at the line it appears on, and `program` is resolved later,
against whatever the working directory ended up as. So this does not work:

```ini
cd = /tmp
program = tests/simple_con.com     # looked for in /tmp, not where you started
```

Use an absolute path for `program` (`${HOME}/...` is expanded). Moving `cd`
below it does **not** help: `program` is resolved in `main()` after the whole
file has been read, so a `cd` anywhere in the file has already taken effect.

### Settings are captured when a mapping is read

A mapping records `default_mode` and `eol_convert` as they stand on the line
it appears on. A setting written below a mapping does not apply to it:

```ini
PRINTSEP.BAS = tests/printsep.bas text
eol_convert  = false                    # too late for the line above
```

Put all the settings first, then the mappings.

## File mappings

Any line that is not one of the directives above is a mapping:

```ini
CPM_NAME = unix/path [text|binary]
```

The CP/M side may be an exact name (`PRINTSEP.BAS`) or an extension pattern
(`*.BAS`). The Unix side is a path, which must exist or the mapping is
skipped and the search falls through.

Working forms, all verified by opening a file through BDOS 15:

```ini
PRINTSEP.BAS = tests/printsep.bas text    # exact name -> one file
*.BAS        = tests/printsep.bas text    # any .BAS -> that one file
*.BAS        = basic/*.bas text           # any .BAS -> the same name in basic/
*.BAS        = text                       # mode only, wherever it is found
```

A `*` on the Unix side takes the text the CP/M pattern matched: with
`*.BAS = basic/*.bas`, `PRINTSEP.BAS` opens `basic/printsep.bas`. For `*` and
`*.*` the whole name stands in, extension included. A path with no `*` is
used exactly as written.

A value that is *only* `text` or `binary` sets the mode for every matching
name without claiming to be a location — the file is still found the normal
way, and the rule only decides how it is read.

### Forms that do not work

These appear in older versions of these examples and in documentation
elsewhere in the repo. None of them do anything:

```ini
verbose = 0                   # not a directive; becomes a mapping named VERBOSE
args = TEST.BAS               # not a directive
```

To set the mode for a whole class of files, use `default_mode`, or a mode
rule (`*.DAT = binary`) for the names it matches. To expose a directory of
files, either give it a drive letter (below) or `cd` into it: a name with no
mapping is looked up lowercased in the working directory.

```ini
default_mode = text
cd = /path/to/my/basic/files
```

A key that is neither a directive nor a real mapping is reported rather than
absorbed: a typo like `verbsoe = 1` says so, and `DEBUG = true` names the
directive it was probably meant to be. The line still becomes a mapping — a
bare word is a legal CP/M name, so refusing it would break real configs — but
it no longer does so in silence.

## Drives

`drive_A` through `drive_P` back a CP/M drive letter with a host directory:

```ini
drive_A = ${HOME}/cpm/work
drive_B = ${HOME}/cpm/basic
```

`B:PROG.BAS` then resolves inside `drive_B`, `DIR B:` lists that directory
and nothing else, and a file made on `B:` is written there. The lookup tries
the lowercased name first, then the name as CP/M spells it.

**A configured drive is confined to its directory.** If `B:MISSING.TXT` is
not in `drive_B`, the open fails — it does not fall back to the working
directory and quietly open something else. That fallback is what makes a
wrong file look like a right one.

**An unconfigured drive is the working directory.** All sixteen start that
way, so a config with no `drive_` line behaves exactly as it did before
drives existed. Real CP/M would answer `Bdos Err On X: Select` for a drive
with no disk; this emulator has no disks and every letter has always meant
the working directory, so making unconfigured drives fatal would break
command lines that work today. The divergence is deliberate.

Two encodings meet here and the config uses neither directly: the drive byte
in an FCB is 1-based with 0 meaning "the selected drive", while BDOS 14 and
BDOS 25 are 0-based. `drive_A` is simply drive A.

`BDOS 24` (login vector) reports A plus every configured or selected drive —
not all sixteen, which would send `STAT DSK:` walking drives that are not
there.

Note that `cpmemu prog.com B:FILE.TXT` has always parsed the `B:` into the
FCB; before drives it was parsed and ignored. With `drive_B` configured that
argument now resolves inside B's directory.

## Where the CP/M binaries go

This repo ships no CP/M programs - no MBASIC, no M80, no Hi-Tech C. The
examples assume `${HOME}/cpm/com/`, so put your own copies there or edit the
`program` line. `examples/simple_test.cfg` is the one example that runs
against files this repo does have.

## Example files

| File | What it shows |
| --- | --- |
| `example.cfg` | Every directive, with comments. Start here. |
| `simple_test.cfg` | MBASIC against this repo's `tests/*.bas`, `default_mode = text` with a binary rule for what MBASIC saves tokenized. |
| `mbasic_tests.cfg` | MBASIC with a directory of programs reached by a drive letter (`drive_B`). |
| `assembler.cfg` | M80/L80 assembly workflow. |
| `compiler.cfg` | Hi-Tech C workflow. |
| `test.cfg`, `test2.cfg` | Minimal configs for checking env expansion and `cd`. |

## Text vs binary

The configuration decides how a file is read and written, and nothing else
does. A file's mode is, in order:

1. the mapping with a host path that reached the file, if it has a mode: the
   `text` or `binary` on its line, or else `default_mode` as it stood on that
   line when that was `text` or `binary`;
2. a mode rule for the name, `*.BAS = text` or `X.DAT = binary`;
3. `default_mode`, when it is `text` or `binary` - for every file a program
   opens as well as every file it makes;
4. only when all of that says `auto`, a guess: the extension, and for a name
   on the text list what the file holds - a REL library named `.LIB` or a
   tokenized `.BAS` is binary (`docs/file_handling_notes.md` has the rule).

What the configuration names text is text whatever it holds, and what it
names binary is binary; the look at the bytes is for `auto` alone.

A file a program makes is decided the same way. A make puts the file in the
drive's directory, and a rename leaves it in the old file's; a mapping with a
host path decides when that path is the file made or renamed to, so
`N.TXT = n.txt binary` is the mode of the `n.txt` a make of `N.TXT` creates as
well as of the one an open reaches. A mapping to another file does not reach
the made one, and the name's mode rule or `default_mode` decides.

A text file with `eol_convert = true` is read with each LF that has no CR
before it made CR LF (a CR LF already there stays CR LF), ending at the first
`^Z`, and is written to the host converted back: CR LF to LF, ending at the
text's `^Z`, with nothing after it. That is so whatever line ends the host
file had before: a CR LF file that a program rewrites in place, appends to or
replaces comes back all LF, the lines it did not touch as well. With
`eol_convert = false`, or `binary`, the records go to the host as the program
wrote them - CR LF, `^Z` and padding - so either one keeps CP/M's CR LF on the
host. A file only read is never rewritten.

`default_mode = text` or `binary` applies to what a program opens, so a
config that sets it names what has to be the other mode with a rule. Under
`default_mode = text`, a program's `.COM`, `.OVR` or `.REL` files, a
tokenized MBASIC program and anything else binary would be read through the
converter:

```ini
default_mode = text
*.COM = binary
*.OVR = binary
*.REL = binary
*.$$$ = binary      # PIP's, ED's and WordStar's work file: see below
```

PIP, ED and WordStar write `NAME.$$$` and rename it over `NAME.EXT` when they
are done. A file made this run and renamed is converted at the rename when
the new name is text with `eol_convert` - by a mapping, a mode rule or
`default_mode` - so a `*.$$$ = binary` rule leaves a copied program alone and
still gives a copied text file host line ends. A new name that is binary, or
has `eol_convert = false`, leaves the file as written; under `auto` the new
name's extension and the file's bytes decide. A file still open when it is
renamed is decided the same way at its last close.

## See also

- `docs/file_handling_notes.md`
