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

Tests are assembled with z88dk:
```bash
z88dk.z88dk-z80asm -b test.asm
cp test.bin test.com
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

1. The console layer has no automated test at all. A pty-driven harness is
   the missing piece - `stdin_has_data()` returns false for a non-tty, so a
   pipe cannot drive BDOS 6 on POSIX.
2. There is no BDOS function 10 line-editor test, despite it being the most
   intricate console code in the emulator.
3. The 14 `tests/*.cc` unit tests are not built or run by any make target.
4. There is no CI job running any of this; `.github/workflows/release.yml`
   builds and packages only.
