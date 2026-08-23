#!/bin/bash
# Test runner for the cpmemu CP/M 2.2 emulator.
#
# Every test compares the guest's stdout against an exact expected byte
# string and reports PASS or FAIL; the script exits non-zero if anything
# failed, so it is usable from CI.
#
# The emulator writes its own diagnostics ("CPU mode", "Loaded N bytes",
# "Program exit") to stderr and nothing else, so stdout carries the guest's
# output alone.  Nothing here merges the two: an earlier version of this
# script did, and then had to filter the banners back out by deleting whole
# lines - which silently deleted the one test whose output shares a line with
# the exit banner, because the guest emits no trailing newline.
#
# Guest output uses CP/M line endings, so the expectations below are written
# with explicit \r\n.
#
# Usage: tests/run_tests.sh [--zex] [--help]
#   --zex   also run zexdoc and zexall.  These are long: a sampled rate of
#           roughly four instruction groups per 40s against 67 groups puts a
#           full run in the tens of minutes, so they are opt-in rather than
#           part of the default run.  Override the cap with
#           CPMEMU_ZEX_TIMEOUT (seconds, default 3600).

set -u

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
root=$(dirname -- "$here")
emu=$root/src/cpmemu
run_zex=0
zex_timeout=${CPMEMU_ZEX_TIMEOUT:-3600}

for arg in "$@"; do
    case $arg in
        --zex)  run_zex=1 ;;
        --help|-h)
            sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *)
            echo "unknown option: $arg (try --help)" >&2
            exit 2 ;;
    esac
done

passed=0
failed=0
skipped=0

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

# Render a file with control characters visible, so a CR/LF mismatch is
# readable in the failure output instead of invisible.
show() {
    sed -n l -- "$1" | sed 's/^/      /'
}

# check <name> <program.com> <expected>
# <expected> is passed through printf %b, so it may contain \r and \n.
check() {
    local name=$1 prog=$2 expected=$3
    local got=$tmp/got want=$tmp/want rc

    if [ ! -f "$root/$prog" ]; then
        printf 'SKIP  %s\n        missing: %s\n' "$name" "$prog"
        skipped=$((skipped + 1))
        return
    fi

    "$emu" "$root/$prog" >"$got" 2>"$tmp/err" </dev/null
    rc=$?
    printf '%b' "$expected" >"$want"

    if [ $rc -ne 0 ]; then
        printf 'FAIL  %s\n        emulator exited %d\n' "$name" "$rc"
        sed 's/^/        /' <"$tmp/err"
        failed=$((failed + 1))
        return
    fi

    if cmp -s "$want" "$got"; then
        printf 'PASS  %s\n' "$name"
        passed=$((passed + 1))
    else
        printf 'FAIL  %s\n' "$name"
        printf '    expected:\n'; show "$want"
        printf '    got:\n';      show "$got"
        failed=$((failed + 1))
    fi
}

# check_zex <name> <program.com>
# zexdoc and zexall print one line per instruction group ending in "OK", a
# line containing "ERROR" on a CRC mismatch, and "Tests complete" at the end.
# The run goes straight to a file rather than through a pipe, so `timeout`'s
# own exit status is visible instead of being replaced by the last stage's.
check_zex() {
    local name=$1 prog=$2
    local out=$tmp/zex rc groups errors

    if [ ! -f "$root/$prog" ]; then
        printf 'SKIP  %s\n        missing: %s\n' "$name" "$prog"
        skipped=$((skipped + 1))
        return
    fi

    printf '      %s: running, up to %ss\n' "$name" "$zex_timeout"
    timeout "$zex_timeout" "$emu" "$root/$prog" >"$out" 2>/dev/null </dev/null
    rc=$?
    groups=$(grep -c 'OK$' "$out")
    errors=$(grep -c 'ERROR' "$out")

    if [ $rc -eq 124 ]; then
        printf 'FAIL  %s\n' "$name"
        printf '        timed out after %ss. groups complete: %s, CRC mismatches: %s\n' \
               "$zex_timeout" "$groups" "$errors"
        printf '        raise CPMEMU_ZEX_TIMEOUT to run it to the end.\n'
        failed=$((failed + 1))
        return
    fi
    if [ $rc -ne 0 ]; then
        printf 'FAIL  %s\n        emulator exited %d. groups complete: %s\n' "$name" "$rc" "$groups"
        failed=$((failed + 1))
        return
    fi
    if [ "$errors" -ne 0 ]; then
        printf 'FAIL  %s\n        CRC mismatches: %s, groups complete: %s\n' "$name" "$errors" "$groups"
        grep 'ERROR' "$out" | sed 's/^/        /'
        failed=$((failed + 1))
        return
    fi
    if ! grep -q 'Tests complete' "$out"; then
        printf 'FAIL  %s\n' "$name"
        printf '        ended without reaching "Tests complete". groups complete: %s\n' "$groups"
        tail -3 "$out" | sed 's/^/        /'
        failed=$((failed + 1))
        return
    fi
    printf 'PASS  %s (groups complete: %s, no CRC mismatches)\n' "$name" "$groups"
    passed=$((passed + 1))
}

if [ ! -x "$emu" ]; then
    echo "emulator not found at $emu, building it"
    if ! make -C "$root/src"; then
        echo "build failed" >&2
        exit 1
    fi
    if [ ! -x "$emu" ]; then
        echo "build succeeded but produced no $emu" >&2
        exit 1
    fi
    echo
fi

echo "cpmemu test suite"
echo "================="
echo "emulator: $emu"
echo

check "console output (BDOS 9)"   tests/simple_con.com  'ABC'
check "DJNZ loop"                 tests/test_djnz.com   '321\r\n'
check "N flag"                    tests/test_n_flag.com '20\r\n02\r\n00\r\n'
check "flag comparison (tflags)"  tests/tflags.com      '94\r\n51\r\n10\r\n3E\r\n'

if [ $run_zex -eq 1 ]; then
    echo
    check_zex "zexdoc (documented instructions)" tests/zexdoc.com
    check_zex "zexall (all instructions)"        tests/zexall.com
else
    echo
    echo "SKIP  zexdoc and zexall (pass --zex to run them)"
    skipped=$((skipped + 2))
fi

echo
echo "================="
printf '%d passed, %d failed, %d skipped\n' "$passed" "$failed" "$skipped"
[ $failed -eq 0 ] || exit 1
