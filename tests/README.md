# Z80 Emulator Tests

## Quick Start

Run the quick tests from anywhere in the repo:
```bash
tests/run_tests.sh
```

Each test compares the guest's stdout against an exact expected string and
reports PASS or FAIL; the script exits non-zero if anything failed.

The instruction exercisers are opt-in, because they are slow - about 7 minutes
each for zexdoc and zexall and 3m41s for `8080exm`, measured:
```bash
tests/run_tests.sh --zex                      # cap defaults to 1 hour each
CPMEMU_ZEX_TIMEOUT=7200 tests/run_tests.sh --zex
```

`make -C src test` runs three of the quick tests as an eyeball check with no
assertions; `tests/run_tests.sh` is the one that can fail. `make -C src unit`
builds and runs the 8080 mode unit tests on their own, and does assert.

## POSIX console tests

`tests/pty_console.cc` is the counterpart for everything that is not Windows.
The terminal layer in `src/os/linux/platform.cc` cannot be reached through a
pipe: `enable_raw_mode()` returns immediately when `is_terminal()` is false, so
a redirected run never touches termios at all. Every other test in this file
therefore runs with that whole layer switched off. The exception is
`stdin_has_data()`, which answers for a file and a pipe as well as for a tty, so
the polled redirected cases in `pty_console.cc` do reach it.

So this one allocates a pty, hands the slave to cpmemu as stdin, writes the
bytes a keyboard would send into the master, and compares what the CP/M guest
received. `tests/run_tests.sh` builds and runs it on any POSIX host.

The guest programs live in `tests/con_guests.h` and are shared with the Windows
harness, so a case named the same on both platforms runs the same code and the
two results can be read side by side.

Nine of its cases pin seven-bit console input, in two groups. Five cover the
mask itself - one per console read site, plus a pipe twin of the BDOS 1 case,
because a pty case alone cannot tell the emulator's mask from a line discipline
that strips the bit. Until they were written the only tests asserting any of
this were the four code-page cases in `tests/win_console.cc`, which have never
been executed on any machine. One of the five asserts a loss rather than a
value: BDOS 6 spells "no character" 0, so the byte that masks to zero is
consumed and never delivered. That case is the tripwire - giving the guest
eight bits means rewriting it deliberately.

The other four cover `check_ctrl_c_exit`, which is fed the raw byte on purpose
so that a 0x83 counts as a high byte and not as a ^C. It has four call sites and
the mistake could be made at any one of them, so each is driven separately; each
fails by truncation, because the emulator exits before the guest can finish the
line. Mutation testing is what put them here: with the mask left alone and only
the ^C argument masked, the suite was green before these existed - at all four
sites, not just the three the group had no case for.

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

Two things it deliberately does not do. Most of it does not make the pty the
child's controlling terminal, because nothing in the case table needs one and a
hangup would otherwise kill the guest before it could report. And it delivers
end of input through a file, a pipe or `/dev/null` rather than by closing the
master - not because closing the master fails (measured on macOS, with no master
fd left anywhere, `select()` reports the slave readable and `read()` returns 0)
but because a file, a pipe and `/dev/null` are what a redirected run actually
uses, and they are the cases nothing else on POSIX covers without an assembler.

The two job control cases at the end are the exception to the first of those,
and they have to be. A shell puts its own termios back when it takes a job into
the foreground, so `kill -TSTP` followed by `fg` used to leave the emulator
running against a canonical terminal for the rest of the session. Staging that
needs a controlling terminal, a process group that is not the terminal's
foreground group, and a `tcsetpgrp` from inside the session, so those two cases
build a session and a stand-in shell of their own. A child in an orphaned
process group has `SIGTSTP` discarded rather than delivered, which is why the
simpler setup cannot reach this at all - an earlier draft of the test reported
"SIGTSTP did not stop the emulator" and was measuring its own scaffolding.

The second of the pair starts the emulator in a background process group, where
the `tcsetattr` in `enable_raw_mode()` is stopped by `SIGTTOU`, and then
foregrounds it. What that proves depends on the platform, and the case says
which it got: on Linux the interrupted `ioctl` is restarted after the `fg` and
completes with no handler involved, so it is a guard against that changing. On a
platform that returns `EINTR` instead, the `SIGCONT` handler is the only thing
between the `fg` and a canonical terminal.

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

which runs a hex echo so each key prints what the guest received. What to press
and what should come back is `MANUAL_CHECKS.md` in the repo root.

## Windows console tests

`tests/win_console.cc` is the only part of the suite that cannot run on Linux.
The extended key path in `src/os/windows/platform.cc` sits behind
`is_terminal()`, and `ReadConsoleInputW()` reads the console input buffer rather
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

which runs a hex echo so each key prints what the guest received. What to press
and what should come back is `MANUAL_CHECKS.md` in the repo root.

## Test Programs

### Simple Tests
- **simple_con.asm** - Prints "ABC" to test console output
- **test_call.asm** - Tests CALL/RET instructions
- **test_djnz.asm** - Tests DJNZ (prints "321" counting down)

### Console Translation Tests
- **adm3a.asm** - one of every ADM-3A escape sequence and control code the
  translator in `console_output()` understands, checked against the exact ANSI
  bytes it emits. Assembled at test time; no `.com` is committed.

### Exit and BIOS Tests
- **savemem.asm** - writes `A5 5A` at `0200h` and then finishes the way its
  command tail asks: `0` for BDOS 0 System Reset, `W` for BIOS WBOOT, `J` for a
  jump to `0000h`. All three have to write a `--save-memory` image; before
  4.7.1 only the jump did. The runner checks the marker in the file *and* the
  exit line on stderr, because `0000h` holds a `JP WBOOT` and a jump that
  stopped being trapped would otherwise save through WBOOT and look fine.
  Assembled at test time; no `.com` is committed.
- **bios_disk.asm** - calls BIOS HOME, READ or WRITE through the jump table by
  command tail (`H`, `R`, `W`) and prints the status byte in A as hex.
  `CPM_BIOS_DISK=ok` gives `00` and `=fail` gives `01`; before 4.7.1 both gave
  `00`, so the mode was invisible to a guest. Assembled at test time; no `.com`
  is committed.
- **sectran.asm** - calls BIOS SECTRAN through the jump table and prints what
  it answered, as hex. The command tail picks the case: `T` a table lookup,
  `H` the same with HL dirtied first, `M` the dullest lookup there is, `W` a
  lookup whose DE+BC carries past FFFF, `A` the accumulator instead of HL, and
  anything else no table at all. Five of the six print four hex digits of HL;
  `A` prints two of the accumulator. SECTRAN used to share the `CPM_BIOS_DISK`
  stub group, which sets only A, so HL came back holding the guest's own
  sentinel and the table was never read. `M` exists so the two mode checks
  move for the mode and for nothing else, `W` so the 64K wrap cannot be
  deleted unnoticed, and `A` because the accumulator is the half of the old
  bug no HL check can see. Assembled at test time; no `.com` is committed.

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

#### 8080/8080exm.com
The same exerciser converted to the 8080 by Ian Bartholomew, with CRCs taken
from real hardware, plus Mike Douglas's change to print the CRC on a pass.
- Size: 4608 bytes
- Tests: 25 instruction groups
- Runtime: 3m41s, measured on the machine the zex timings above came from
- Status: completes, all 25 groups, no CRC mismatches
- Run it with `tests/run_tests.sh --zex`, which passes `--8080`

It used to be in the tree three times over: this copy, a byte-identical
`tests/8080EXM.COM`, and `tests/8080exer.com` duplicating
`tests/8080/8080exer.com`. See "The 8080 directory" below for what went and
what stayed.

It must be run under `--8080`. In Z80 mode it is measuring the wrong
processor - the CRCs it holds are an 8080's - and all 25 groups mismatch,
which says nothing about anything.

It sat in the tree, referenced by nothing, from the initial commit until it was
wired in here - and it was right and the emulator was wrong. Wiring it in found
two bugs in 8080 mode, both of them Z80 rules applied where the 8080 has
different ones:

- `DAA` read bit 1 of the flag register as the Z80's N flag. The 8080 has no
  subtract flag and bit 1 of its flag register reads back as 1 whatever
  happened, so every `DAA` in 8080 mode took the subtract path: `DAA` on `1Ah`
  gave `14h` where an 8080 gives `20h`. That is every BCD program under
  `--8080`, not an edge case.
- `CMA`, `STC` and `CMC` applied the Z80's `CPL`/`SCF`/`CCF` half-carry rules.
  On the 8080 `CMA` affects no condition bit and `STC` and `CMC` affect only
  the carry, and the bit being written is the 8080's auxiliary carry.

The second of those is why 19 of the 25 groups mismatched rather than the two
that name `DAA` and the rotates: the exerciser's own harness runs an `STC` after
every test instruction to restore the carry it clobbered reading the stack
pointer, and that `STC` was clearing the auxiliary carry the instruction under
test had just produced. A wrong `STC` therefore corrupted the recorded state of
almost every other group, `MOV` and `MVI` included, which set no flags at all.

Both are covered by `tests/unit_8080.cc` as well, so the four-minute opt-in run
is not the only thing standing between them and a regression.

#### 8080/8080pre.com
The preliminary test that comes with the exerciser: a fixed sequence rather than
an exhaustive walk, printing `8080 Preliminary tests complete` and nothing else
if every check passes, and stopping at the first one that does not.
- Size: 1024 bytes
- Runtime: 0.09s, measured on the machine the timings above came from
- Status: passes
- Part of the default `tests/run_tests.sh`, not behind `--zex`

It is in the default suite precisely because of that runtime: it is three orders
of magnitude cheaper than the exerciser beside it, so there is nothing to weigh
up. It is a much weaker check than `8080exm` - a fixed sequence against no CRCs -
and it is not a substitute for either that or `tests/unit_8080.cc`.

### The 8080 directory

`tests/8080/` is a copy of the `cpu_tests` directory from superzazu's 8080
emulator, and it holds each program with the `.mac` it was assembled from and a
`.prn` listing, plus a README naming where they came from. That is what makes it
the canonical copy, and two loose duplicates elsewhere in `tests/` have been
deleted:

- `tests/8080EXM.COM` - was run by `--zex` and was byte for byte
  `tests/8080/8080exm.com`. Deleted; `--zex` runs the copy in `tests/8080/`.
- `tests/8080exer.com` - was run by nothing and was byte for byte
  `tests/8080/8080exer.com`. Deleted.
- `tests/8080/8080exer.com` - run by nothing before and run by nothing now. Kept,
  for the reason below.

`8080exer.com` is kept and not run, deliberately. It is the same 25 instruction
groups as `8080exm.com` and takes the same minutes, and where `8080exm` prints
the CRC it computed, `8080exer` prints `OK` - so a failing group tells you less,
and a passing run tells you nothing `8080exm` did not already tell you. It stays
because the directory is a verbatim copy of an upstream set: deleting one `.com`
would leave its `.mac` and `.prn` describing a program that is not there.

## 8080 mode unit tests

`--8080` is a real feature of the emulator and, until `tests/unit_8080.cc`,
nothing tested it. zexdoc and zexall cover the Z80 core thoroughly, but they run
the CPU as a Z80, so every rule that makes 8080 mode different - parity instead
of overflow, the auxiliary carry, the fixed flag bits, no N flag - was reachable
by no test at all.

It links the CPU core directly rather than running a CP/M guest, so a failure
names the instruction, the operands and the flag bit instead of a CRC over sixty
thousand cases. Where the input space is small enough it is walked exhaustively:
3.1 million ALU cases across the register, immediate and memory forms, 65536 per
16-bit increment, every value of the flag byte. It runs in under a second and is
part of the default `tests/run_tests.sh`.

```bash
make -C src unit          # builds and runs it on its own
```

The expected values are the documented 8080 rules written out in the test, not a
recording of what this emulator does. One place the Intel manual will not do as
written is `DAA`: it says the second correction applies when, "after the
incrementing", the top four bits exceed nine, which is wrong for `A` in
`FAh..FFh` where adding six carries out of the accumulator and leaves a top
nibble of zero. The condition the hardware applies is on the accumulator before
either correction, `A` greater than `99h`. Twelve of that group's 1024 cases are
the ones that tell the two readings apart, and the test uses the second reading.

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

Each of these runs for minutes, so give it room; the 180-second cap an earlier
version of this file suggested stops about five groups in and prints the partial
output as though it were the result.

```bash
# Run zexdoc (documented instructions)
src/cpmemu tests/zexdoc.com

# Run zexall (all instructions)
src/cpmemu tests/zexall.com

# Run the 8080 exerciser.  The --8080 is not optional: without it this is
# measuring a Z80 against an 8080's CRCs and every group mismatches.
src/cpmemu --8080 tests/8080/8080exm.com

# The preliminary test, which the default suite already runs.  Under a tenth
# of a second, and prints one line.
src/cpmemu --8080 tests/8080/8080pre.com
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
- `.mac` - Macro-80 source, in `tests/8080/` only
- `.prn` - Macro-80 listing, in `tests/8080/` only

## Assembler

The committed `.com` files under `tests/` were assembled with z88dk:
```bash
z88dk.z88dk-z80asm -b test.asm
cp test.bin test.com
```

The drive mapping sources, the two console end-of-input programs, `cli_tail.asm`,
`adm3a.asm`, `savemem.asm`, `bios_disk.asm` and `sectran.asm` are assembled at
test time instead, so no binary for them is committed. `tests/run_tests.sh` uses
`pasmo` if it is on `PATH` and `z80asm` otherwise, and skips the whole group - 42
checks - when neither is:
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
- `8080exm` under `--8080`: 25 groups, no CRC mismatches ✅, in 3m41s, after the
  `DAA` and `CMA`/`STC`/`CMC` fixes described above. Before them, 19 of the 25
  mismatched.
- `8080pre` under `--8080`: passes, in 0.09s. In the default suite.

## Next Steps

The CRC hunt that used to sit here is finished; all three exercisers pass clean.
What is left is coverage of everything they do not reach:

1. The console layer now has `tests/pty_console.cc` on POSIX and
   `tests/win_console.cc` on Windows, including two BDOS function 10
   line-editor cases. What neither covers is a person actually pressing the
   keys; both have a `--manual` mode for that and nobody has run either. The
   four terminal programs to try, and the bytes each key should print, are in
   `MANUAL_CHECKS.md` in the repo root.
2. The drive mapping group needs an assembler. It takes `pasmo` or `z80asm`,
   which covers Homebrew and Debian, but on a machine with neither it still
   skips 42 checks, about two fifths of the suite. CI installs `z80asm` and
   runs with `--require`, so the gate can no longer hide there; a local run on
   a machine with neither assembler still skips them, and committing those
   thirteen `.com` files as byte arrays the way `tests/con_guests.h` does would
   de-gate it entirely.
3. `.github/workflows/ci.yml` now runs this suite on `ubuntu-latest` and
   `macos-latest` and `tests\win_console.bat` on `windows-latest`, on every
   push. The Windows console cases ran there for the first time and report 28
   passed, 0 failed, which includes the four code-page cases and the
   `CTRL_BREAK_EVENT` one. Pass `--require` (CI does) to make a skip for want
   of a tool a failure: without it the first CI run reported 60 passed and a
   green tick, having skipped the 42 checks behind the assembler gate.
