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

## More

- [docs/test_suite.md](../docs/test_suite.md) - the console harnesses, every
  test program, the 8080 unit tests, the assembler, known issues and past results
- [docs/testing.md](../docs/testing.md) - the short version, for a first run
- [MANUAL_CHECKS.md](../MANUAL_CHECKS.md) - what needs a person at a keyboard
