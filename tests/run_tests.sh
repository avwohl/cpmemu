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
#   --zex   also run zexdoc and zexall.  Each takes about 7 minutes on the
#           machine this was measured on - 13m46s for the pair, 67 groups
#           each - so they are opt-in rather than part of the default run.
#           The cap defaults to an hour apiece, which is generous headroom
#           for slower hardware; override it with CPMEMU_ZEX_TIMEOUT
#           (seconds).

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

    printf '      %s: running, about 7 minutes, cap %ss\n' "$name" "$zex_timeout"
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

# ---------------------------------------------------------------------------
# Drive mapping (BDOS 14/15/17/18/22/24 against drive_X directories).
#
# These need an assembler, because committing a .com for each would put five
# more opaque binaries in the tree.  pasmo is what tests/README.md names; if
# it is missing the whole group skips rather than failing.
# ---------------------------------------------------------------------------

drive_sandbox() {
    sb=$tmp/sb
    rm -rf "$sb"
    mkdir -p "$sb/a" "$sb/b"
    printf 'AAA' >"$sb/a/hello.txt"
    printf 'BBB' >"$sb/b/hello.txt"
    printf 'CCC' >"$sb/b/other.txt"
    printf 'ZZZ' >"$sb/zonly.txt"      # cwd decoy: must never be reached from a mapped drive
    { echo "drive_A = $sb/a"; echo "drive_B = $sb/b"; } >"$sb/drives.cfg"
}

# check_drive <name> <program.com> <cfg> <arg> <expected-stdout>
check_drive() {
    local name=$1 prog=$2 cfg=$3 arg=$4 expected=$5
    local got=$tmp/dgot want=$tmp/dwant rc

    ( cd "$sb" && "$emu" "$cfg" $arg ) >"$got" 2>"$tmp/derr"
    rc=$?
    printf '%b' "$expected" >"$want"

    if [ $rc -ne 0 ]; then
        printf 'FAIL  %s\n        emulator exited %d\n' "$name" "$rc"
        sed 's/^/        /' <"$tmp/derr"
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

if ! command -v pasmo >/dev/null 2>&1; then
    echo
    echo "SKIP  drive mapping tests (pasmo not on PATH)"
    skipped=$((skipped + 6))
else
    echo
    asm_ok=1
    for src in drv_read drv_dir drv_make drv_sel drv_login drv_ren cli_tail; do
        if ! pasmo "$root/tests/$src.asm" "$tmp/$src.com" >"$tmp/asm.log" 2>&1; then
            echo "FAIL  assembling tests/$src.asm"
            sed 's/^/        /' <"$tmp/asm.log"
            failed=$((failed + 1))
            asm_ok=0
        fi
    done
    if [ $asm_ok -eq 1 ]; then
        drive_sandbox
        cfg=$sb/drives.cfg

        # An explicit drive letter picks the directory, and the two drives
        # hold different bytes under the same CP/M name.
        printf 'program = %s/drv_read.com\n' "$tmp" >"$tmp/read.cfg"
        cat "$cfg" >>"$tmp/read.cfg"
        check_drive "drive: A:HELLO.TXT reads A" "$tmp/drv_read.com" "$tmp/read.cfg" "A:HELLO.TXT" 'AAA'
        check_drive "drive: B:HELLO.TXT reads B" "$tmp/drv_read.com" "$tmp/read.cfg" "B:HELLO.TXT" 'BBB'

        # The one that matters most: a mapped drive must not fall back to the
        # working directory.  zonly.txt exists only there, so a fallback would
        # report success on the wrong file.
        check_drive "drive: no fallback to cwd" "$tmp/drv_read.com" "$tmp/read.cfg" "B:ZONLY.TXT" 'NF'

        # BDOS 14 selects B, then an FCB with dr=0 must follow it.
        printf 'program = %s/drv_sel.com\n' "$tmp" >"$tmp/sel.cfg"
        cat "$cfg" >>"$tmp/sel.cfg"
        check_drive "drive: BDOS 14 sets the default" "$tmp/drv_sel.com" "$tmp/sel.cfg" "" 'BBB'

        # Search is scoped to the drive: B has two files, and the cwd decoy is
        # not among them.
        printf 'program = %s/drv_dir.com\n' "$tmp" >"$tmp/dir.cfg"
        cat "$cfg" >>"$tmp/dir.cfg"
        check_drive "drive: search scopes to the drive" "$tmp/drv_dir.com" "$tmp/dir.cfg" "B:" \
            'OTHER   TXT\r\nHELLO   TXT\r\n'

        # Login vector reports exactly the configured drives.
        printf 'program = %s/drv_login.com\n' "$tmp" >"$tmp/login.cfg"
        cat "$cfg" >>"$tmp/login.cfg"
        check_drive "drive: login vector" "$tmp/drv_login.com" "$tmp/login.cfg" "" '0003'

        # Make writes into the drive directory, not the working directory.
        printf 'program = %s/drv_make.com\n' "$tmp" >"$tmp/make.cfg"
        cat "$cfg" >>"$tmp/make.cfg"
        check_drive "drive: make lands on the drive" "$tmp/drv_make.com" "$tmp/make.cfg" "B:NEW.TXT" 'MADE'
        if [ -f "$sb/b/new.txt" ] && [ ! -f "$sb/new.txt" ]; then
            printf 'PASS  drive: make wrote to B, not to the cwd\n'
            passed=$((passed + 1))
        else
            printf 'FAIL  drive: make wrote to B, not to the cwd\n'
            [ -f "$sb/b/new.txt" ] || printf '        missing %s\n' "$sb/b/new.txt"
            [ -f "$sb/new.txt" ] && printf '        stray %s\n' "$sb/new.txt"
            failed=$((failed + 1))
        fi

        # A rename on a mapped drive stays inside it, and must not plant a
        # drive-less alias that answers for other drives too.
        printf 'program = %s/drv_ren.com\n' "$tmp" >"$tmp/ren.cfg"
        cat "$cfg" >>"$tmp/ren.cfg"
        check_drive "drive: rename stays on the drive" "$tmp/drv_ren.com" "$tmp/ren.cfg" \
            "B:OTHER.TXT RENAMED.TXT" 'REN'
        printf 'program = %s/drv_read.com\n' "$tmp" >"$tmp/rd2.cfg"
        cat "$cfg" >>"$tmp/rd2.cfg"
        check_drive "drive: renamed file readable on B" "$tmp/drv_read.com" "$tmp/rd2.cfg" \
            "B:RENAMED.TXT" 'CCC'
        check_drive "drive: rename leaks no cross-drive alias" "$tmp/drv_read.com" "$tmp/rd2.cfg" \
            "A:RENAMED.TXT" 'NF'

        # --- file mapping forms -------------------------------------------
        # A '*' on the host side takes the name the CP/M pattern matched.
        mkdir -p "$sb/bas"
        printf 'PPP' >"$sb/bas/one.bas"
        printf 'QQQ' >"$sb/bas/two.bas"
        { echo "*.BAS = $sb/bas/*.bas text"
          printf 'program = %s/drv_read.com\n' "$tmp"; } >"$tmp/wild.cfg"
        check_drive "mapping: host wildcard takes the name" "$tmp/drv_read.com" "$tmp/wild.cfg" \
            "ONE.BAS" 'PPP'
        check_drive "mapping: host wildcard, second name" "$tmp/drv_read.com" "$tmp/wild.cfg" \
            "TWO.BAS" 'QQQ'
        check_drive "mapping: host wildcard misses cleanly" "$tmp/drv_read.com" "$tmp/wild.cfg" \
            "GONE.BAS" 'NF'

        # A value that is only a mode sets the mode without claiming to be a
        # path, and must not stop the file resolving from the cwd.
        printf 'ZZZ' >"$sb/plain.bas"
        { echo 'debug = true'; echo '*.BAS = binary'
          printf 'program = %s/drv_read.com\n' "$tmp"; } >"$tmp/moderule.cfg"
        ( cd "$sb" && "$emu" "$tmp/moderule.cfg" PLAIN.BAS ) >"$tmp/mgot" 2>"$tmp/merr"
        if grep -q "mode: binary" "$tmp/merr" && [ "$(cat "$tmp/mgot")" = "ZZZ" ]; then
            printf 'PASS  mapping: mode-only rule applies and still resolves\n'
            passed=$((passed + 1))
        else
            printf 'FAIL  mapping: mode-only rule applies and still resolves\n'
            printf '        stdout: %s\n' "$(cat "$tmp/mgot")"
            grep 'BDOS Open' "$tmp/merr" | sed 's/^/        /'
            failed=$((failed + 1))
        fi

        # --- config diagnostics -------------------------------------------
        # A mistyped directive still becomes a file mapping - changing that
        # would break real mappings - but it must no longer do so in silence.
        check_cfg_warn() {
            local name=$1 line=$2 want=$3
            { echo "$line"; printf 'program = %s/drv_read.com\n' "$tmp"; } >"$tmp/warn.cfg"
            ( cd "$sb" && "$emu" "$tmp/warn.cfg" HELLO.TXT ) >/dev/null 2>"$tmp/warnerr"
            if grep -q "$want" "$tmp/warnerr"; then
                printf 'PASS  %s\n' "$name"
                passed=$((passed + 1))
            else
                printf 'FAIL  %s\n        no "%s" for: %s\n' "$name" "$want" "$line"
                grep '^Config line' "$tmp/warnerr" | sed 's/^/        /'
                failed=$((failed + 1))
            fi
        }
        check_cfg_warn "config: typo is reported"      'verbsoe = 1'  'is not a directive'
        check_cfg_warn "config: wrong case is named"   'DEBUG = true' "spelled 'debug'"

        # A real mapping must not be warned about.
        check_cfg_quiet() {
            local name=$1 line=$2
            { echo "$line"; printf 'program = %s/drv_read.com\n' "$tmp"; } >"$tmp/quiet.cfg"
            ( cd "$sb" && "$emu" "$tmp/quiet.cfg" HELLO.TXT ) >/dev/null 2>"$tmp/quieterr"
            if grep -q '^Config line' "$tmp/quieterr"; then
                printf 'FAIL  %s\n' "$name"
                grep '^Config line' "$tmp/quieterr" | sed 's/^/        /'
                failed=$((failed + 1))
            else
                printf 'PASS  %s\n' "$name"
                passed=$((passed + 1))
            fi
        }
        check_cfg_quiet "config: real mapping stays quiet" "HELLO.TXT = $sb/a/hello.txt text"
        check_cfg_quiet "config: mode rule stays quiet"    '*.TXT = text'

        # --- options after the program name --------------------------------
        # An emulator option written after the program used to be handed to
        # the guest instead, silently.  It is now honoured and kept out of the
        # command tail; anything the emulator does not define still goes to
        # the program untouched.
        printf 'program = %s/cli_tail.com\n' "$tmp" >"$tmp/tail.cfg"
        check_drive "cli: trailing option leaves the tail" "$tmp/cli_tail.com" "$tmp/tail.cfg" \
            "FOO.TXT --no-ctrl-c-exit" ' FOO.TXT'
        check_drive "cli: unknown dashed arg reaches the guest" "$tmp/cli_tail.com" "$tmp/tail.cfg" \
            "-X --bogus" ' -X --BOGUS'
        check_drive "cli: CP/M option tail is untouched" "$tmp/cli_tail.com" "$tmp/tail.cfg" \
            "TEST,TEST.COM/N/E" ' TEST,TEST.COM/N/E'

        # With no drive_X at all, resolution must be what it always was.
        printf 'program = %s/drv_read.com\n' "$tmp" >"$tmp/nodrv.cfg"
        check_drive "drive: none configured behaves as before" "$tmp/drv_read.com" "$tmp/nodrv.cfg" \
            "ZONLY.TXT" 'ZZZ'
    fi
fi

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
