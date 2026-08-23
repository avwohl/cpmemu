# Z80 Emulator Tests

## Quick Start

Run the quick tests from anywhere in the repo:
```bash
tests/run_tests.sh
```

Each test compares the guest's stdout against an exact expected string and
reports PASS or FAIL; the script exits non-zero if anything failed.

zexdoc and zexall are opt-in, because they are slow - about 7 minutes each,
measured:
```bash
tests/run_tests.sh --zex                      # cap defaults to 1 hour each
CPMEMU_ZEX_TIMEOUT=7200 tests/run_tests.sh --zex
```

`make -C src test` runs three of the quick tests as an eyeball check with no
assertions; `tests/run_tests.sh` is the one that can fail.

## POSIX console tests

`tests/pty_console.cc` is the counterpart for everything that is not Windows.
The terminal layer in `src/os/linux/platform.cc` cannot be reached through a
pipe: `enable_raw_mode()` returns immediately when `is_terminal()` is false, and
`stdin_has_data()` answers false for anything that is not a tty by design, so a
redirected run never touches termios and BDOS 6 never sees a byte. Every other
test in this file therefore runs with that whole layer switched off.

So this one allocates a pty, hands the slave to cpmemu as stdin, writes the
bytes a keyboard would send into the master, and compares what the CP/M guest
received. `tests/run_tests.sh` builds and runs it on any POSIX host.

The guest programs live in `tests/con_guests.h` and are shared with the Windows
harness, so a case named the same on both platforms runs the same code and the
two results can be read side by side.

The case it exists for is `^V` and `^O`. BSD and XNU gate `VLNEXT` and
`VDISCARD` on `IEXTEN` outside the `ICANON` block, so without the `IEXTEN` clear
in `enable_raw_mode()` the line discipline eats those two bytes before the guest
sees them. Linux does not - `IEXTEN` is inert there once `ICANON` is off - which
is why a Linux pty could never show that clear was load-bearing. Measured on
macOS 27, arm64: with the mask as written every byte `0x01`-`0x1F` and `0x7F`
reaches the guest; with `IEXTEN` left set, `0x0F` and `0x16` vanish and nothing
else changes.

The first case is a control that re-measures this wherever it runs, and prints
what the clear was worth on that machine:

```
PASS  control: the raw mode lets every control byte through
      note: without the IEXTEN clear this platform loses: 0F(^O) 16(^V)
            so the ^V and ^O cases below are testing something real.
```

On Linux that note says the opposite, and the `^V` and `^O` cases below it are
then known to be passing for free rather than passing on merit.

Two things it deliberately does not do. It does not make the pty the child's
controlling terminal, because nothing under test needs one and a hangup would
otherwise kill the guest before it could report. And it delivers end of input
through a file, a pipe or `/dev/null` rather than by closing the master - not
because closing the master fails (measured on macOS, with no master fd left
anywhere, `select()` reports the slave readable and `read()` returns 0) but
because a file, a pipe and `/dev/null` are what a redirected run actually uses,
and they are the cases nothing else on POSIX covers without an assembler.

One trap worth knowing if you write a probe of your own here: a master fd that
survives into the child keeps the terminal alive, so the reader blocks forever
with no EOF and no error. That looks exactly like a platform that cannot report
a hangup, and it is easy to conclude the wrong thing from it.

What it cannot answer: writing into a pty steps past the terminal program
itself. If Terminal.app or iTerm binds a key for its own use, these tests still
pass and the user still loses the key. For that, press the keys:

```bash
c++ -std=c++11 -Wall -Wextra -o pty_console tests/pty_console.cc
./pty_console --manual src/cpmemu
```

which runs a hex echo so each key prints what the guest received.

## Windows console tests

`tests/win_console.cc` is the only part of the suite that cannot run on Linux.
The extended key path in `src/os/windows/platform.cc` sits behind
`is_terminal()`, and `_getch()`/`_kbhit()` read the console input buffer rather
than stdin, so nothing arriving through a pipe touches it. The test therefore
drives a real console: it starts cpmemu with stdin bound to the console it is
attached to, writes the `INPUT_RECORD`s a keyboard would produce with
`WriteConsoleInput`, and compares the bytes the CP/M guest received.

```cmd
tests\win_console.bat
tests\win_console.bat path\to\cpmemu.exe
```

`tests/run_tests.sh` runs it when it is running on Windows and prints
`SKIP  windows console` everywhere else.

One thing it cannot answer: `WriteConsoleInput` writes into the console input
buffer directly, so it steps past whatever the terminal program does with a
keystroke first. If Windows Terminal ever binds ctrl+left for itself, these
tests still pass and the user still loses the key. For that, press the keys:

```cmd
tests\win_console.exe --manual path\to\cpmemu.exe
```

which runs a hex echo so each key prints what the guest received.

## Test Programs

### Simple Tests
- **simple_con.asm** - Prints "ABC" to test console output
- **test_call.asm** - Tests CALL/RET instructions
- **test_djnz.asm** - Tests DJNZ (prints "321" counting down)

### Flag Verification Tests
- **test_n_flag.asm** - Verifies N flag is set/cleared correctly
  - Expected output: `20 02 00`
- **test_sequence.asm** - Tests flag propagation through operations
  - Expected output: `00 00 94 00`
- **tflags.asm** - Comprehensive flag test
  - Expected output: `94 51 10 3E`
- **test_adc.asm** - Tests ADC with carry flag states

All flag tests have been verified to match tnylpo output exactly.

### Comprehensive Test Suites

#### zexdoc.com
Tests **documented Z80 instructions only**.
- Size: 8704 bytes
- Tests: 67 instruction groups
- Runtime: about 7 minutes
- Status: completes, all 67 groups, no CRC mismatches

#### zexall.com
Tests **all Z80 instructions** including undocumented behavior.
- Size: 8704 bytes
- Tests: 67 instruction groups (with undocumented flag behavior)
- Runtime: about 7 minutes
- Status: completes, all 67 groups, no CRC mismatches

**Note**: both suites pass clean. `tests/run_tests.sh --zex` ran the pair
end to end in 13m46s, 67 groups each and zero CRC mismatches. Timings are
from one development machine and will vary; the runner's cap defaults to
an hour per suite and is overridable with `CPMEMU_ZEX_TIMEOUT`.

An earlier version of this file recorded that every group reported a CRC
error and attributed it to instruction bugs. That is not what a full run
shows. The claim predates a runner that could tell a finished run from a
truncated one: the old script capped the suites at 180 seconds, which is
about five groups in, and printed the partial output as though it were
the result.

## Running Individual Tests

### Console Output Tests
```bash
src/cpmemu tests/simple_con.com
# Should print: ABC

src/cpmemu tests/test_djnz.com
# Should print: 321
```

### Flag Tests
```bash
src/cpmemu tests/test_n_flag.com
# Should print: 20 02 00

src/cpmemu tests/tflags.com
# Should print: 94 51 10 3E
```

### Comprehensive Tests
```bash
# Run zexdoc (documented instructions)
timeout 180 src/cpmemu tests/zexdoc.com

# Run zexall (all instructions)
timeout 180 src/cpmemu tests/zexall.com
```

## Test Results Comparison with tnylpo

All simple flag tests match tnylpo exactly:

| Test | Our Output | tnylpo Output | Status |
|------|------------|---------------|---------|
| test_n_flag | 20 02 00 | 20 02 00 | ✅ PASS |
| tflags | 94 51 10 3E | 94 51 10 3E | ✅ PASS |
| test_sequence | 00 00 94 00 | 00 00 94 00 | ✅ PASS |

## File Types

- `.asm` - Assembly source code
- `.bin` - Raw binary output from assembler
- `.com` - CP/M executable (same as .bin for these tests)
- `.o` - Object file from z88dk assembler

## Assembler

The committed `.com` files under `tests/` were assembled with z88dk:
```bash
z88dk.z88dk-z80asm -b test.asm
cp test.bin test.com
```

The drive mapping sources are assembled at test time instead, so no binary for
them is committed. `tests/run_tests.sh` uses `pasmo` if it is on `PATH` and
`z80asm` otherwise, and skips the group when neither is:
```bash
brew install z80asm        # macOS; pasmo is not in Homebrew
apt install z80asm         # or pasmo
```

## Known Issues

### zexdoc/zexall CRC errors: none as of the last full run

This section used to list CRC mismatches across 16-bit arithmetic, DAA,
RLD/RRD, the block operations, rotate/shift X/Y flags and indexed addressing.
A full run of both suites now reports 67 groups each with zero mismatches, so
that list is out of date and has been removed rather than left to mislead.

What the result does and does not say: every instruction behaviour the two
exercisers cover, undocumented flag behaviour included, matches the reference
CRCs. That is the strongest single check available here, but it is still only
the instructions zexall exercises - it says nothing about BDOS, BIOS, the
console layer or the file system, which have their own tests or none.

## Historical Test Results

### Before Stack Fix (Commit 473ea14)
- simple_con: Printed "A" only (crashed on first BDOS call)
- zexdoc: Exited immediately after printing header
- All tests broken

### After Stack Fix
- simple_con: Prints "ABC" ✅
- Flag tests: All match tnylpo ✅
- zexdoc: Stopped at "unimplemented opcode 0x10"

### After Relative Jumps (Commit d36a5c7)
- zexdoc: Progressed through ~45 tests
- Hung on "ld (<ix,iy>+1),nn" test

### After Indexed LD Fix (Commit b2d5362)
- zexdoc: Completes all tests ✅
- zexall: Completes all tests ✅
- Both show CRC errors (expected - other bugs)

### Current
- zexdoc: 67 groups, no CRC mismatches ✅
- zexall: 67 groups, no CRC mismatches ✅
- Pair runs end to end in 13m46s

## Next Steps

The CRC hunt that used to sit here is finished; both exercisers pass clean.
What is left is coverage of everything they do not reach:

1. The console layer now has `tests/pty_console.cc` on POSIX and
   `tests/win_console.cc` on Windows, including two BDOS function 10
   line-editor cases. What neither covers is a person actually pressing the
   keys; both have a `--manual` mode for that and nobody has run the POSIX one
   yet on a terminal program other than the one it was written on.
2. The drive mapping group needs an assembler. It takes `pasmo` or `z80asm`,
   which covers Homebrew and Debian, but on a machine with neither it still
   skips 25 checks - more than half the suite. Committing those nine `.com`
   files as byte arrays the way `tests/con_guests.h` does would de-gate it
   entirely.
3. The 14 `tests/*.cc` unit tests are not built or run by any make target.
4. There is no CI job running any of this; `.github/workflows/release.yml`
   builds and packages only, and only for Linux and Windows.
