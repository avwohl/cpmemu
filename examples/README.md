# CP/M Emulator Configuration Examples

Example configuration files for the CP/M emulator.

```bash
./src/cpmemu examples/example.cfg
```

## What a config file actually supports

Every directive below was checked against `CPMEmulator::load_config_file` in
`src/cpmemu.cc` and confirmed by running the emulator. Anything not in this
list is **not** a directive: unrecognised keys silently become file mappings,
so a typo produces no error at all.

| Directive | Meaning |
| --- | --- |
| `program` | Program to run. Required. |
| `cd` / `chdir` | Change working directory. Applied immediately, in file order. |
| `default_mode` | `auto`, `text` or `binary`. |
| `eol_convert` | `true`/`false`. Convert `\n` <-> `\r\n` for text files. |
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

Use an absolute path for `program`, or put `cd` after it.

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

To set the mode for a whole class of files, use `default_mode`. To expose a
directory of files, either give it a drive letter (below) or `cd` into it: a
name with no mapping is looked up lowercased in the working directory.

```ini
default_mode = text
cd = /path/to/my/basic/files
```

A key that is not a directive and not meant as a mapping is still silent;
`todo.txt` records that.

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
| `simple_test.cfg` | MBASIC against this repo's `tests/*.bas`. |
| `mbasic_tests.cfg` | MBASIC with a directory of programs reached by `cd`. |
| `assembler.cfg` | M80/L80 assembly workflow. |
| `compiler.cfg` | Hi-Tech C workflow. |
| `test.cfg`, `test2.cfg` | Minimal configs for checking env expansion and `cd`. |

## Text vs binary

Text files get `\n` <-> `\r\n` conversion; binary files do not. `default_mode
= auto` guesses from the extension. Set it explicitly when a guess would be
wrong, and use the per-file mapping form to override one file.

## See also

- `docs/file_handling_notes.md`
