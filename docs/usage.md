# Usage

```
cpmemu [options] <program.com|config.cfg> [args...]
```

Options may be written after the program or config file as well as before -
`cpmemu prog.cfg --no-ctrl-c-exit` works. Any argument that is not one of the
options below is passed to the guest untouched, which leaves a CP/M command tail
such as `TEST,TEST.COM/N/E` intact.

## Options

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

## Examples

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
[examples/README.md](../examples/README.md); `examples/` carries working files.
