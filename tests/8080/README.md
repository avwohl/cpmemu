# 8080 Test Files

Source: https://github.com/superzazu/8080/tree/master/cpu_tests

These test files were copied from superzazu's 8080 emulator repository.

## Test Files

- **8080exer.com** - 8080 instruction exerciser (executable)
- **8080exer.mac** - Source code for 8080exer.com
- **8080exm.com** - 8080 instruction exerciser, modified version (executable)
- **8080exm.mac** - Source code for 8080exm.com
- **8080pre.com** - Preliminary 8080 test (executable)
- **8080pre.mac** - Source code for 8080pre.com

Both 8080exer and 8080exm are modified from Frank D. Cringle's Z80 instruction exerciser,
converted to 8080 by Ian Bartholomew in February 2009.

The tests work by executing all variations of an instruction with different operands
and computing a CRC over the resulting machine state. The CRC is then compared against
the expected value from a real 8080.

## What the test suite runs

This directory is the canonical copy. `tests/run_tests.sh` runs two of the three:

- **8080pre.com** under `--8080`, in the default run. Under a tenth of a second.
- **8080exm.com** under `--8080`, behind `--zex`. 25 groups, no CRC mismatches.
  Minutes rather than seconds; `../README.md` records the timings and the
  machines they came from.
- **8080exer.com** is run by nothing. It is the same 25 groups as 8080exm and
  takes the same minutes, printing `OK` where 8080exm prints the CRC it computed,
  so running it would add no coverage. It stays because this directory is a
  verbatim copy and its `.mac` and `.prn` are here.

There used to be a second, byte-identical copy of 8080exm.com at
`tests/8080EXM.COM` and of 8080exer.com at `tests/8080exer.com`. Both are gone.
