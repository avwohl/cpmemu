#!/bin/bash
# Smoke test: start MBASIC under the emulator and show what it prints.
#
# This is an eyeball test with no assertions - tests/run_tests.sh is the one
# that can fail.  MBASIC is not in this repo, so point at a copy with $MBASIC
# or drop one at com/mbasic.com.  Without it everything here skips rather than
# reporting a failure that cannot be told apart from a missing file.
#
# Every run takes its input from /dev/null and is capped with timeout, so the
# script never waits for a terminal and is safe to run unattended.

set -u

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
root=$(dirname -- "$here")
emu=$root/src/cpmemu
mbasic=${MBASIC:-$root/com/mbasic.com}
cfg=$root/examples/simple_test.cfg
bas=$root/tests/printsep.bas

if [ ! -x "$emu" ]; then
    echo "emulator not found at $emu - run: make -C $root/src" >&2
    exit 1
fi

echo "=== CP/M emulator smoke test (MBASIC) ==="
echo "emulator: $emu"
echo "mbasic:   $mbasic"
echo

if [ ! -f "$mbasic" ]; then
    echo "SKIP  everything: no MBASIC at $mbasic"
    echo "      set MBASIC=/path/to/mbasic.com, or put a copy at com/mbasic.com"
    exit 0
fi

echo "Test 1: MBASIC starts and shows its prompt"
echo "------------------------------------------"
timeout 2 "$emu" "$mbasic" </dev/null 2>&1 | head -20
echo

echo "Test 2: loading through a config file"
echo "------------------------------------------"
if [ -f "$cfg" ]; then
    timeout 2 "$emu" "$cfg" </dev/null 2>&1 | head -20
else
    echo "SKIP  no $cfg"
fi
echo

echo "Test 3: mapping a file from the command line"
echo "------------------------------------------"
if [ -f "$bas" ]; then
    timeout 2 "$emu" "$mbasic" "$bas" </dev/null 2>&1 | head -20
else
    echo "SKIP  no $bas"
fi
echo

echo "=== done ==="
echo "To use interactively:"
echo "  $emu $mbasic"
echo "  $emu $cfg"
