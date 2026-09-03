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
# Usage: tests/run_tests.sh [--zex] [--require] [--help]
#   --zex   also run zexdoc, zexall and 8080exm.  zexdoc and zexall take about
#           7 minutes each on the machine this was measured on - 13m46s for the
#           pair, 67 groups each - and 8080exm adds 25 more groups under --8080
#           in 3m41s, so all three are opt-in rather than part of the default
#           run.  The cap defaults to an hour apiece, which is generous
#           headroom for slower hardware; override it with CPMEMU_ZEX_TIMEOUT
#           (seconds).  The preliminary 8080 test is not among them: it runs in
#           under a tenth of a second and is in the default suite.
#   --require
#           a skip for want of a tool is a failure.  Same as
#           CPMEMU_REQUIRE_ALL=1.  A skip exits 0, so on a machine missing an
#           assembler this suite reports "60 passed, 0 failed, 46 skipped" and
#           a green tick - which is what the first CI job to run it did, having
#           executed three fifths of it.  Under this flag the three skips a
#           machine can fix by installing something - no assembler, no mingw,
#           a .com that has gone missing - fail instead.  The two platform
#           skips do not: the pty harness cannot run on Windows and the console
#           harness cannot run anywhere else, and no install changes that.  Nor
#           do the exercisers, which are opt-in above by design.
#           CPMEMU_SKIP_OK allows named ones through: a space or comma
#           separated list of "assembler", "mingw" and "missing-com".  The
#           macOS CI job passes "mingw" and nothing else.

set -u

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
root=$(dirname -- "$here")
emu=$root/src/cpmemu
run_zex=0
zex_timeout=${CPMEMU_ZEX_TIMEOUT:-3600}
require_all=${CPMEMU_REQUIRE_ALL:-0}

for arg in "$@"; do
    case $arg in
        --zex)  run_zex=1 ;;
        --require) require_all=1 ;;
        --help|-h)
            sed -n '2,41p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *)
            echo "unknown option: $arg (try --help)" >&2
            exit 2 ;;
    esac
done

passed=0
failed=0
skipped=0

# Every skip that means "this machine is missing a tool" registers itself here,
# so --require can turn the lot into one failure at the end.  Registering is
# separate from printing because the count behind a gate is not always one: 42
# checks sit behind the assembler.
# Each takes a token so a caller can allow one by name: CPMEMU_SKIP_OK is a
# space or comma separated list of tokens that --require lets through.  The
# macOS CI job uses it for "mingw", because installing a Windows cross-compiler
# on a Mac to satisfy a check the linux job already does would be silly - and
# that is the whole list, so the assembler cannot quietly go missing there the
# way it did on ubuntu.
soft_skips=0
soft_skip_list=
soft_skip() {
    case " $(printf '%s' "${CPMEMU_SKIP_OK:-}" | tr ',' ' ') " in
        *" $1 "*) return ;;
    esac
    soft_skips=$((soft_skips + 1))
    soft_skip_list="$soft_skip_list
        $2"
}

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

# Run a command under a time limit, exiting 124 if it overruns, the way
# timeout(1) does.
#
# timeout(1) is GNU coreutils and is in no BSD or macOS base system - on the Mac
# this was written on it existed only because Homebrew coreutils was installed.
# Without a bound the guard tests below stop being guards: a regression that
# reinstates a spin would hang the suite rather than fail it, and check_zex
# would have no cap at all. With a bare `timeout` and nothing providing it, all
# three instead fail with "exited 127", which reads as an emulator bug.
if command -v timeout >/dev/null 2>&1; then
    run_bounded() { timeout "$@"; }
elif command -v gtimeout >/dev/null 2>&1; then
    run_bounded() { gtimeout "$@"; }
else
    run_bounded() {
        local limit=$1 pid waited=0
        shift
        "$@" &
        pid=$!
        while kill -0 "$pid" 2>/dev/null; do
            if [ "$waited" -ge "$limit" ]; then
                kill -TERM "$pid" 2>/dev/null
                wait "$pid" 2>/dev/null
                return 124
            fi
            sleep 1
            waited=$((waited + 1))
        done
        wait "$pid"
    }
fi

# Render a file with control characters visible, so a CR/LF mismatch is
# readable in the failure output instead of invisible.
# Paths here are all script-generated under mktemp -d, so there is nothing for
# the `--` that used to be here to protect against - and BSD sed took it for a
# filename, so every FAIL printed "sed: --: No such file or directory" across
# the output that was meant to explain the failure.
show() {
    sed -n l "$1" | sed 's/^/      /'
}

# check <name> <program.com> <expected> [emulator options...]
# <expected> is passed through printf %b, so it may contain \r and \n.
check() {
    local name=$1 prog=$2 expected=$3
    shift 3
    local got=$tmp/got want=$tmp/want rc

    if [ ! -f "$root/$prog" ]; then
        printf 'SKIP  %s\n        missing: %s\n' "$name" "$prog"
        skipped=$((skipped + 1))
        soft_skip missing-com "$name (missing: $prog)"
        return
    fi

    "$emu" "$@" "$root/$prog" >"$got" 2>"$tmp/err" </dev/null
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

# check_zex <name> <program.com> [emulator options...]
# zexdoc and zexall print one line per instruction group ending in "OK", a
# line containing "ERROR" on a CRC mismatch, and "Tests complete" at the end.
# 8080exm.com is the same exerciser converted to the 8080 and prints
# "PASS! crc is:xxxxxxxx" where the other two print "OK", so both spellings
# count as a finished group here.
# The run goes straight to a file rather than through a pipe, so `timeout`'s
# own exit status is visible instead of being replaced by the last stage's.
check_zex() {
    local name=$1 prog=$2
    shift 2
    local out=$tmp/zex rc groups errors

    if [ ! -f "$root/$prog" ]; then
        printf 'SKIP  %s\n        missing: %s\n' "$name" "$prog"
        skipped=$((skipped + 1))
        soft_skip missing-com "$name (missing: $prog)"
        return
    fi

    printf '      %s: running, minutes rather than seconds, cap %ss\n' "$name" "$zex_timeout"
    run_bounded "$zex_timeout" "$emu" "$@" "$root/$prog" >"$out" 2>/dev/null </dev/null
    rc=$?
    groups=$(grep -cE 'OK$|PASS!' "$out")
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
    # other.txt before hello.txt, deliberately.  "drive: search scopes to the
    # drive" asserts they come back sorted, and if they were created in sorted
    # order that check could not tell a sort from a filesystem handing back
    # creation order - which is what it was doing before, on both counts.
    printf 'CCC' >"$sb/b/other.txt"
    printf 'BBB' >"$sb/b/hello.txt"
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

# Assembling the drive mapping sources.
#
# pasmo is what tests/README.md names, but it is packaged almost nowhere - it is
# not in Homebrew, so this whole group used to skip on any Mac.  z80asm (Bas
# Wijnen's, which is in Homebrew and in Debian) takes these sources unchanged
# and assembles every one of them, so either will do.  No count here: it was
# wrong the last two times a guest was added.  A dialect that produced different
# bytes could not pass quietly: every check below compares the guest's output
# against an exact string, so a mis-assembled program fails rather than drifts.
if command -v pasmo >/dev/null 2>&1; then
    assembler=pasmo
    assemble() { pasmo "$1" "$2"; }
elif command -v z80asm >/dev/null 2>&1; then
    assembler=z80asm
    assemble() { z80asm -o "$2" "$1"; }
else
    assembler=
fi

if [ -z "$assembler" ]; then
    echo
    echo "SKIP  drive mapping tests (no assembler: install pasmo or z80asm)"
    # 42 checks live behind this gate, not the 6 an earlier version counted
    skipped=$((skipped + 42))
    soft_skip assembler "drive mapping tests: 42 checks, no assembler (pasmo or z80asm)"
else
    echo
    asm_ok=1
    for src in drv_read drv_dir drv_make drv_sel drv_login drv_ren cli_tail con_eof con_spin adm3a \
               savemem bios_disk sectran; do
        if ! assemble "$root/tests/$src.asm" "$tmp/$src.com" >"$tmp/asm.log" 2>&1; then
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
        #
        # The order is the sort platform::list_directory promises, not the
        # order the two files were created in.  This expectation used to read
        # OTHER before HELLO, which was this machine's readdir(3) order and
        # nothing more: the first CI run of these 42 checks failed here on a
        # GitHub ubuntu runner, whose ext4 handed them back the other way
        # round.  The emulator now sorts, so both orders cannot be right and
        # this is the one that is.
        printf 'program = %s/drv_dir.com\n' "$tmp" >"$tmp/dir.cfg"
        cat "$cfg" >>"$tmp/dir.cfg"
        check_drive "drive: search scopes to the drive" "$tmp/drv_dir.com" "$tmp/dir.cfg" "B:" \
            'HELLO   TXT\r\nOTHER   TXT\r\n'

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

        # --- end of console input -------------------------------------------
        # The first read past the end still answers CR, so a part-typed line
        # submits; after that it answers ^Z and a program that checks for it
        # stops on its own.  Before this, BDOS 1 answered CR forever.
        printf 'program = %s/con_eof.com\n' "$tmp" >"$tmp/eof.cfg"
        # timeout, not because the fix needs one, but so a regression that
        # reinstates the spin fails the suite instead of hanging it.
        ( cd "$sb" && run_bounded 30 "$emu" "$tmp/eof.cfg" </dev/null ) >"$tmp/eofgot" 2>/dev/null
        if [ "$(cat "$tmp/eofgot")" = "0D 1A " ]; then
            printf 'PASS  console: EOF gives CR once, then ^Z\n'
            passed=$((passed + 1))
        else
            printf 'FAIL  console: EOF gives CR once, then ^Z\n'
            printf '    expected: 0D 1A \n    got:\n'; show "$tmp/eofgot"
            failed=$((failed + 1))
        fi

        # A program that ignores ^Z too must still be stopped rather than
        # left spinning on a stream that will never produce another byte.
        printf 'program = %s/con_spin.com\n' "$tmp" >"$tmp/spin.cfg"
        if ( cd "$sb" && run_bounded 30 "$emu" "$tmp/spin.cfg" </dev/null ) \
               >/dev/null 2>"$tmp/spinerr"; then
            if grep -q 'reads past end of input' "$tmp/spinerr"; then
                printf 'PASS  console: a reader that ignores ^Z is stopped\n'
                passed=$((passed + 1))
            else
                printf 'FAIL  console: a reader that ignores ^Z is stopped\n'
                printf '        exited without the diagnostic\n'
                failed=$((failed + 1))
            fi
        else
            printf 'FAIL  console: a reader that ignores ^Z is stopped\n'
            printf '        did not exit within 30s\n'
            failed=$((failed + 1))
        fi

        # --- the ADM-3A to ANSI output translator ---------------------------
        # Every other expected string in this suite is plain ASCII, so nothing
        # else ever puts a byte into console_output() that changes term_state:
        # the four-state escape parser, ESC = cursor addressing and the Kaypro
        # ESC G attribute byte were reachable by no test at all.  tests/adm3a.asm
        # sends one of everything and this is the exact translation.
        printf 'program = %s/adm3a.com\n' "$tmp" >"$tmp/adm3a.cfg"
        check_drive "console: ADM-3A sequences become ANSI" "$tmp/adm3a.com" "$tmp/adm3a.cfg" "" \
            'A\033[2J\033[H\033[K\033[J\033[7m\033[0m\033[7m\033[2m\033[0m\033[3;6H\033[2J\033[H\033[H\033[A\033[C\010\007\033q\r\nZ'

        # With no drive_X at all, resolution must be what it always was.
        printf 'program = %s/drv_read.com\n' "$tmp" >"$tmp/nodrv.cfg"
        check_drive "drive: none configured behaves as before" "$tmp/drv_read.com" "$tmp/nodrv.cfg" \
            "ZONLY.TXT" 'ZZZ'

        # --- --save-memory on every way a program can finish -----------------
        # BDOS 0, BIOS WBOOT and a jump to 0000h are all a CP/M program
        # finishing, and only the jump used to write the image.  BDOS 0 is the
        # one that matters: it is how most CP/M programs end, MOVCPM and SYSGEN
        # among them, so --save-memory silently produced no file for the case
        # the flag exists for.  The guest leaves A5 5A at 0200h and the marker
        # has to survive into the file, which a zero-length or truncated write
        # would not do.
        # The exit line is checked as well as the bytes, and that is not
        # belt-and-braces: 0000h holds the JP WBOOT the emulator writes there,
        # so a jump to 0000h that stopped being trapped would land in WBOOT and
        # still save.  Comparing only the marker would report PASS for a build
        # in which the path being named had been deleted outright - verified by
        # doing exactly that to each of the three in turn.
        check_savemem() {
            local name=$1 how=$2 want_exit=$3
            rm -f "$tmp/mem.bin"
            "$emu" --save-memory="$tmp/mem.bin" --save-range=0200-0201 \
                   "$tmp/savemem.com" "$how" >/dev/null 2>"$tmp/smerr"
            if ! grep -q "$want_exit" "$tmp/smerr"; then
                printf 'FAIL  %s\n        stderr never said "%s"\n' "$name" "$want_exit"
                sed 's/^/        /' <"$tmp/smerr"
                failed=$((failed + 1))
            elif [ ! -f "$tmp/mem.bin" ]; then
                printf 'FAIL  %s\n        no file written\n' "$name"
                sed 's/^/        /' <"$tmp/smerr"
                failed=$((failed + 1))
            elif [ "$(od -An -tx1 -v "$tmp/mem.bin" | tr -d ' \n')" = "a55a" ]; then
                printf 'PASS  %s\n' "$name"
                passed=$((passed + 1))
            else
                printf 'FAIL  %s\n        file holds: %s\n' "$name" \
                    "$(od -An -tx1 -v "$tmp/mem.bin" | tr -d ' \n')"
                failed=$((failed + 1))
            fi
        }
        check_savemem "save-memory: BDOS 0 System Reset writes the image" 0 'System reset'
        check_savemem "save-memory: BIOS WBOOT writes the image"          W 'BIOS WBOOT'
        check_savemem "save-memory: a jump to 0000h writes the image"     J 'JMP 0'

        # A run with no --save-memory must not announce a save.  Asserting on
        # the absence of the file alone would test nothing, since only the flag
        # can name that path; the "Saved" line is what a regression that made
        # saving unconditional would actually produce.
        rm -f "$tmp/mem.bin"
        "$emu" "$tmp/savemem.com" 0 >/dev/null 2>"$tmp/nosave"
        if [ -f "$tmp/mem.bin" ] || grep -q 'Saved .* bytes' "$tmp/nosave"; then
            printf 'FAIL  save-memory: no flag, no save\n'
            sed 's/^/        /' <"$tmp/nosave"
            failed=$((failed + 1))
        else
            printf 'PASS  save-memory: no flag, no save\n'
            passed=$((passed + 1))
        fi

        # --- CPM_BIOS_DISK tells the guest which mode it is in ---------------
        # "fail" returned A = 0, byte for byte what "ok" returns, so a guest
        # could not tell the two apart and the startup line announcing failure
        # described nothing.  A = 1 is the CP/M BIOS permanent error.
        check_bios_disk() {
            local name=$1 mode=$2 call=$3 want=$4 got
            got=$(CPM_BIOS_DISK="$mode" "$emu" "$tmp/bios_disk.com" "$call" 2>/dev/null)
            if [ "$got" = "$want" ]; then
                printf 'PASS  %s\n' "$name"
                passed=$((passed + 1))
            else
                printf 'FAIL  %s\n        expected %s, got %s\n' "$name" "$want" "$got"
                failed=$((failed + 1))
            fi
        }
        check_bios_disk "bios disk: ok returns A = 0"        ok   R 00
        check_bios_disk "bios disk: fail returns A = 1"      fail R 01
        check_bios_disk "bios disk: fail is fail for WRITE"  fail W 01
        check_bios_disk "bios disk: fail is fail for HOME"   fail H 01

        # "error" is the third mode and the only one that was ever visible.
        # The diagnostic is required as well as the status: a non-zero exit on
        # its own would also be produced by the emulator failing to start, which
        # has nothing to do with CPM_BIOS_DISK.
        if CPM_BIOS_DISK=error "$emu" "$tmp/bios_disk.com" R >/dev/null 2>"$tmp/bderr"; then
            printf 'FAIL  bios disk: error exits the emulator\n'
            failed=$((failed + 1))
        elif grep -q 'Unimplemented BIOS disk function' "$tmp/bderr"; then
            printf 'PASS  bios disk: error exits the emulator\n'
            passed=$((passed + 1))
        else
            printf 'FAIL  bios disk: error exits the emulator\n'
            printf '        exited non-zero without the diagnostic\n'
            sed 's/^/        /' <"$tmp/bderr"
            failed=$((failed + 1))
        fi

        # --- BIOS SECTRAN answers in HL --------------------------------------
        # SECTRAN takes BC = the logical sector and DE = the translate table
        # and is documented to return the physical sector in HL, but it sat in
        # the CPM_BIOS_DISK group above, which sets only A.  HL came back
        # holding whatever the guest had left in it and the table was never
        # read: Z, T and H printed 0000, 0000 and AA55 - the guest's own
        # sentinels.
        # SECTRAN is arithmetic, not I/O, so it cannot fail and does not belong
        # in that group: the answer has to be the same in all three modes, and
        # error mode must not take the emulator down over a table lookup, which
        # is what it did.  The two mode checks use the dullest lookup the guest
        # has - index 0, HL already clean - so that they move for the mode and
        # for nothing else.  The exit status is checked as well as the digits:
        # error mode exited 1 printing nothing, and an empty stdout must not be
        # read as a quiet pass.
        check_sectran() {
            local name=$1 mode=$2 call=$3 want=$4 got rc
            got=$(CPM_BIOS_DISK="$mode" "$emu" "$tmp/sectran.com" "$call" 2>/dev/null)
            rc=$?
            if [ $rc -eq 0 ] && [ "$got" = "$want" ]; then
                printf 'PASS  %s\n' "$name"
                passed=$((passed + 1))
            else
                printf 'FAIL  %s\n        expected %s exit 0, got %s exit %d\n' \
                    "$name" "$want" "$got" "$rc"
                failed=$((failed + 1))
            fi
        }
        check_sectran "sectran: no table answers HL = BC"          ok    Z 1234
        check_sectran "sectran: a table answers the byte at DE+BC" ok    T 0008
        check_sectran "sectran: a table answers with H = 0"        ok    H 0006
        check_sectran "sectran: fail mode translates anyway"       fail  M 0006
        check_sectran "sectran: error mode translates anyway"      error M 0006
        # The last two are here because a build could get all four above right
        # and still be wrong.  W carries DE+BC past FFFF, which the five above
        # never approach - their table sits low in the guest's own image - so
        # without it the cast that keeps the sum inside 64K can be deleted and
        # the suite stays green while the emulator reads off the end of its
        # memory; the table address itself is ordinary, FF8F.  A is the
        # half of the original bug no HL check can see: the stub group set the
        # accumulator, and a real SECTRAN leaves it alone.
        check_sectran "sectran: the table index wraps at FFFF"     ok    W 0009
        check_sectran "sectran: A is left alone"                   fail  A 5A
    fi
fi

# ---------------------------------------------------------------------------
# 8080 mode.
#
# --8080 is a real feature and nothing tested it. zexdoc and zexall cover the
# Z80 core, but they run the CPU as a Z80, so every rule that makes 8080 mode
# different was reachable by no test at all. tests/unit_8080.cc links the CPU
# core directly and walks the input space where it is small enough to walk:
# 3.1 million ALU cases, 65536 per 16-bit increment, checked against the
# documented 8080 rules rather than against a recording of this emulator.
#
# 8080pre.com is the preliminary test that comes with the exerciser: a short
# fixed sequence rather than an exhaustive walk, and it prints one line or
# stops. It is here rather than behind --zex because it finishes in under a
# tenth of a second, where the exerciser beside it takes minutes.
# ---------------------------------------------------------------------------

echo
unitlog=$tmp/unit.log
if ! make -C "$root/src" unit_8080 >"$tmp/unitbuild.log" 2>&1; then
    printf 'FAIL  8080 unit tests (unit_8080 did not build)\n'
    sed 's/^/        /' <"$tmp/unitbuild.log" | head -15
    failed=$((failed + 1))
else
    "$root/src/unit_8080" >"$unitlog" 2>&1
    grep -v -e '^[0-9][0-9]* groups' -e '^cpmemu 8080' -e '^====' -e '^$' "$unitlog"
    if grep -q '^\(PASS\|FAIL\)  ' "$unitlog"; then
        passed=$((passed + $(grep -c '^PASS  ' "$unitlog")))
        failed=$((failed + $(grep -c '^FAIL  ' "$unitlog")))
    else
        printf 'FAIL  8080 unit tests (unit_8080 reported nothing)\n'
        tail -5 "$unitlog" | sed 's/^/        /'
        failed=$((failed + 1))
    fi
fi

check "8080 preliminary tests" tests/8080/8080pre.com \
      '8080 Preliminary tests complete' --8080

# ---------------------------------------------------------------------------
# POSIX console.
#
# The terminal layer in src/os/linux/platform.cc is unreachable through a pipe:
# enable_raw_mode() returns at once when is_terminal() is false, so a redirected
# run never touches termios. Everything above therefore runs with that whole
# layer switched off, bar stdin_has_data(), which answers for a file and a pipe
# as well as for a tty.
#
# tests/pty_console.cc gives cpmemu a real terminal instead - a pty whose master
# this script's child writes the bytes a keyboard would send into - and compares
# what the CP/M guest received. It runs the same guest programs as the Windows
# harness, from tests/con_guests.h, so a case named the same on both platforms
# can be read side by side. Its first case measures the raw mode itself and says
# what the IEXTEN clear is worth on the machine running it, so a pass is never
# just a green tick.
# ---------------------------------------------------------------------------

echo
case $(uname -s) in
    MINGW*|MSYS*|CYGWIN*)
        echo "SKIP  posix console (tests/win_console.cc covers this on Windows)"
        skipped=$((skipped + 1))
        ;;
    *)
        ptylog=$tmp/pty.log
        if ! ${CXX:-c++} -std=c++11 -Wall -Wextra -o "$tmp/pty_console" \
                 "$here/pty_console.cc" >"$tmp/ptybuild.log" 2>&1; then
            printf 'FAIL  posix console (pty_console.cc did not compile)\n'
            sed 's/^/        /' <"$tmp/ptybuild.log" | head -15
            failed=$((failed + 1))
        else
            "$tmp/pty_console" "$emu" >"$ptylog" 2>&1
            # Its own totals line would double count against this script's
            grep -v -e '^[0-9][0-9]* passed' -e '^$' "$ptylog"
            # Counting only the verdicts it printed would let a launch that
            # never happened pass as an empty success, which is the one failure
            # this section cannot be allowed to have
            if grep -q '^\(PASS\|FAIL\|SKIP\)  ' "$ptylog"; then
                passed=$((passed + $(grep -c '^PASS  ' "$ptylog")))
                failed=$((failed + $(grep -c '^FAIL  ' "$ptylog")))
                skipped=$((skipped + $(grep -c '^SKIP  ' "$ptylog")))
            else
                printf 'FAIL  posix console (pty_console reported nothing)\n'
                tail -5 "$ptylog" | sed 's/^/        /'
                failed=$((failed + 1))
            fi
        fi
        ;;
esac

# ---------------------------------------------------------------------------
# Windows cross-compile.
#
# os/windows/platform.cc is not built by any Linux or CI job, so a change that
# breaks only the Windows half used to sit undetected until someone built on
# Windows. A cross-compile catches that in seconds. It proves the code
# compiles, nothing more - the console path still needs a real Windows console
# to exercise.
# ---------------------------------------------------------------------------

echo
if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    echo "SKIP  windows cross-compile (x86_64-w64-mingw32-g++ not on PATH)"
    skipped=$((skipped + 1))
    soft_skip mingw "windows cross-compile: x86_64-w64-mingw32-g++ not on PATH"
else
    wintmp=$tmp/win
    rm -rf "$wintmp"
    mkdir -p "$wintmp"
    cp -r "$root/src/." "$wintmp/"
    rm -f "$wintmp"/*.o "$wintmp"/*.a "$wintmp"/cpmemu "$wintmp"/cpmemu.exe
    if ( cd "$wintmp" && make -f Makefile.win \
             CXX=x86_64-w64-mingw32-g++ AR=x86_64-w64-mingw32-ar ) \
           >"$tmp/win.log" 2>&1 && [ -f "$wintmp/cpmemu.exe" ]; then
        if grep -q 'warning:' "$tmp/win.log"; then
            printf 'FAIL  windows cross-compile (warnings)\n'
            grep 'warning:' "$tmp/win.log" | sed 's/^/        /' | head -10
            failed=$((failed + 1))
        else
            printf 'PASS  windows cross-compile (clean)\n'
            passed=$((passed + 1))
        fi
    else
        printf 'FAIL  windows cross-compile\n'
        tail -15 "$tmp/win.log" | sed 's/^/        /'
        failed=$((failed + 1))
    fi
fi

# ---------------------------------------------------------------------------
# Windows console.
#
# The extended key path in os/windows/platform.cc sits behind is_terminal(),
# and ReadConsoleInputW() reads the console input buffer rather than stdin, so no
# pipe reaches any of it and the cross-compile above only proves it builds.
# tests/win_console.cc drives a real console instead, by writing the
# INPUT_RECORDs a keyboard produces into it. That needs Windows, so everywhere
# else this can only say so.
# ---------------------------------------------------------------------------

echo
case $(uname -s) in
    MINGW*|MSYS*|CYGWIN*)
        winlog=$tmp/wincon.log
        winbat=$(cygpath -w "$root/tests/win_console.bat")
        # No emulator argument: the batch file already defaults to the one this
        # tree builds, and cmd /c splits a second argument that has a space in
        # it however it is quoted, which a checkout under "My Documents" would
        # hit.  One argument survives, because cmd strips the single pair.
        MSYS_NO_PATHCONV=1 cmd.exe /c "$winbat" >"$winlog" 2>&1
        # Its own totals line would double count against this script's
        grep -v -e '^[0-9][0-9]* passed' -e '^$' "$winlog"
        # Counting only the verdicts it printed would let a launch that never
        # happened pass as an empty success, which is the one failure this
        # section cannot be allowed to have
        if grep -q '^\(PASS\|FAIL\|SKIP\)  ' "$winlog"; then
            passed=$((passed + $(grep -c '^PASS  ' "$winlog")))
            failed=$((failed + $(grep -c '^FAIL  ' "$winlog")))
            skipped=$((skipped + $(grep -c '^SKIP  ' "$winlog")))
        else
            printf 'FAIL  windows console (win_console.bat reported nothing)\n'
            tail -5 "$winlog" | sed 's/^/        /'
            failed=$((failed + 1))
        fi
        ;;
    *)
        echo "SKIP  windows console (needs a real Windows console)"
        skipped=$((skipped + 1))
        ;;
esac

# 8080exm.com is the 8080 counterpart of zexdoc: same machinery, 25 instruction
# groups, converted to the 8080 by Ian Bartholomew and CRCs taken from real
# hardware.  It is run under --8080 because in Z80 mode it is measuring the
# wrong processor and every group mismatches.  It sat in the tree referenced by
# nothing until it was wired in here, and the two 8080-mode bugs it then found
# are in the changelog.  The path is tests/8080/, beside the .mac it was
# assembled from; there used to be a second, byte-identical copy at
# tests/8080EXM.COM and it is the one that has gone.
if [ $run_zex -eq 1 ]; then
    echo
    check_zex "zexdoc (documented instructions)" tests/zexdoc.com
    check_zex "zexall (all instructions)"        tests/zexall.com
    check_zex "8080exm (8080 mode)"              tests/8080/8080exm.com --8080
else
    echo
    echo "SKIP  zexdoc, zexall and 8080exm (pass --zex to run them)"
    skipped=$((skipped + 3))
fi

# A skip exits 0, so a machine with no assembler runs three fifths of this
# suite and reports a green tick.  Under --require that is a failure, named,
# with what to install.
if [ "$require_all" = 1 ] && [ $soft_skips -gt 0 ]; then
    echo
    printf 'FAIL  %d skip(s) are failures because --require is set:%s\n' \
           "$soft_skips" "$soft_skip_list"
    failed=$((failed + 1))
fi

echo
echo "================="
printf '%d passed, %d failed, %d skipped\n' "$passed" "$failed" "$skipped"
[ $failed -eq 0 ] || exit 1
