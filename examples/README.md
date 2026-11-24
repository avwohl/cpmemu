# CP/M Emulator Configuration Examples

Example configuration files for the CP/M emulator.

```bash
./src/cpmemu examples/example.cfg
```

[docs/config_file.md](../docs/config_file.md) is the reference for the config
file: each directive, the mapping forms, the drives, and the text and binary
rules.

## Where the CP/M binaries go

This repo ships no CP/M programs - no MBASIC, no M80, no HI-TECH C. The
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
| `compiler.cfg` | HI-TECH C workflow with the `C.COM` driver. A one-line `ENVIRON` file tells the driver that its passes, libraries and headers are on C:. |
| `test.cfg`, `test2.cfg` | Minimal configs for checking env expansion and `cd`. |

## See also

- `docs/file_handling_notes.md`
