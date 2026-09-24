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
#           assembler this suite still reports 0 failed and a green tick while
#           roughly two fifths of its checks never ran - which is what the
#           first CI job to run it did.  Under this flag the four skips a
#           machine can fix by installing something - no assembler, no mingw,
#           a .com that has gone missing, no python3 for the random text file
#           check - fail instead.  The two platform
#           skips do not: the pty harness cannot run on Windows and the console
#           harness cannot run anywhere else, and no install changes that.  Nor
#           do the exercisers, which are opt-in above by design.
#           CPMEMU_SKIP_OK allows named ones through: a space or comma
#           separated list of "assembler", "mingw", "missing-com", "python3",
#           "posix-console" and "windows-console".  The macOS CI job passes
#           "mingw" and nothing else.  The last two cover skips the pty and
#           console sub-harnesses print themselves, which are skips of this
#           suite too - pty_console.cc skipping for want of a pty is 42 checks
#           gone, and --require could not see it.

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
            sed -n '2,45p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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
# separate from printing because the count behind a gate is not always one: 111
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
# more opaque binaries in the tree.  um80 is the one - see the note above the
# assemble() definition - and if it is missing the whole group skips rather than
# failing.
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
# No count here: it was wrong the last two times a guest was added.  A dialect
# that produced different bytes could not pass quietly either: every check below
# compares the guest's output against an exact string, so a mis-assembled program
# fails rather than drifts.
# um80 and ul80, and nothing else.  They are this project's own assembler and
# linker, written for its Z80/8080 work, and they are what every .asm in this
# family is assembled with - romwbw_emu says so in as many words.  The sources
# below carry `.z80` for that reason: without it um80 reads LD and JR as 8080
# mnemonics and rejects them.
#
# pasmo and z80asm were both accepted here until 2026-09-10 and are not any
# more.  Nothing was wrong with their output - the changelog records
# tests/sectran.asm assembling byte-identical under each - the problem is two
# tools for one job: a suite whose result depends on which assembler a machine
# happens to have is a suite that can pass here and fail there for a reason
# nobody records.  One assembler, and it is the one this project maintains.
#
# NO `org 0100h` IN THESE SOURCES.  ul80 bases a relocatable code segment at
# 0100h by itself, so an ORG is applied on top of that base and puts the code at
# 0200h behind 256 zero bytes.  Measured on tests/drv_read.asm: 384 bytes with
# 256 leading zeros against 128 bytes of code.  It still RUNS - CP/M loads the
# whole file at 0100h and the Z80 slides through 256 NOPs into the code - which
# is exactly why it goes unnoticed, and it is the bug romwbw_emu found sitting in
# src/w8.asm for a long time.
if command -v um80 >/dev/null 2>&1 && command -v ul80 >/dev/null 2>&1; then
    assembler=um80
    assemble() {
        _rel=${2%.com}.rel
        um80 -o "$_rel" "$1" && ul80 -o "$2" "$_rel"
    }
else
    assembler=
fi

if [ -z "$assembler" ]; then
    echo
    echo "SKIP  drive mapping tests (no assembler: pip install um80)"
    # 111 checks live behind this gate, not the 6 an earlier version counted
    skipped=$((skipped + 111))
    soft_skip assembler "drive mapping tests: 111 checks, no assembler (pip install um80)"
else
    echo
    asm_ok=1
    for src in drv_read drv_dir drv_make drv_sel drv_login drv_ren cli_tail cli_fcb con_eof con_spin \
               adm3a savemem bios_disk sectran fcb_io mem_top text_ops; do
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

        # The default FCBs are the CCP's: a name the command line does not
        # give is blank, not NULs.  DRI's ED refuses to start - DISK OR
        # DIRECTORY FULL, before it reads a key - unless the second name is
        # blank, and it was eleven NULs.  And a '*' fills its field with '?',
        # as the CCP's CONVERT does; it became '_'.
        printf 'program = %s/cli_fcb.com\n' "$tmp" >"$tmp/clifcb.cfg"
        check_drive "cli: a second name not given is blank (ED starts)" "$tmp/cli_fcb.com" \
            "$tmp/clifcb.cfg" "FOO.TXT" '00[FOO     TXT]00[           ]00'
        check_drive "cli: no names at all leaves both FCBs blank" "$tmp/cli_fcb.com" \
            "$tmp/clifcb.cfg" "" '00[           ]00[           ]00'
        check_drive "cli: drives and names in both FCBs" "$tmp/cli_fcb.com" "$tmp/clifcb.cfg" \
            "A:X.Y B:LONGNAMEXX.ABCD" '01[X       Y  ]02[LONGNAMEABC]00'
        # Not through check_drive, whose unquoted $arg would let the shell
        # glob the '*' against the sandbox first.
        got=$(cd "$sb" && "$emu" "$tmp/clifcb.cfg" 'FOO.*' 'A*B.T?T' 2>/dev/null)
        if [ "$got" = '00[FOO     ???]00[A???????T?T]00' ]; then
            printf 'PASS  %s\n' "cli: a '*' fills its field with '?'"
            passed=$((passed + 1))
        else
            printf 'FAIL  %s\n        expected %s\n        got      %s\n' \
                "cli: a '*' fills its field with '?'" '00[FOO     ???]00[A???????T?T]00' "$got"
            failed=$((failed + 1))
        fi

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

        # --- BDOS file calls, through tests/fcb_io.asm -----------------------
        # tests/fcb_io.asm runs a script of BDOS file calls against one FCB -
        # its header lists the letters - and prints the first byte of every
        # record it reads, and = and the status for any call that fails.
        #
        # fcb_file <host-name> <letters>: one 128-byte record per letter,
        # each filled with that letter.
        fcbdir=$tmp/fcb
        fcb_file() {
            local out=$1 letters=$2 i
            : >"$out"
            for ((i = 0; i < ${#letters}; i++)); do
                head -c 128 /dev/zero | tr '\0' "${letters:i:1}" >>"$out"
            done
        }
        # check_fcb <name> <CP/M name> <script> <expected stdout>
        #           [<host file> <file holding the bytes it must end as>]
        # Runs in $fcbdir, which the caller has populated.  With fcb_cfg set,
        # the emulator is given that config, whose program is fcb_io.com,
        # instead of the program itself.
        check_fcb() {
            local name=$1 file=$2 script=$3 want=$4 host=${5-} want_file=${6-} got rc
            got=$(cd "$fcbdir" && "$emu" "${fcb_cfg:-$tmp/fcb_io.com}" "$file" "$script" \
                  2>"$tmp/fcberr")
            rc=$?
            if [ $rc -ne 0 ]; then
                printf 'FAIL  %s\n        emulator exited %d\n' "$name" "$rc"
                sed 's/^/        /' <"$tmp/fcberr"
                failed=$((failed + 1))
            elif [ "$got" != "$want" ]; then
                printf 'FAIL  %s\n        script %s\n        expected %s\n        got      %s\n' \
                    "$name" "$script" "$want" "$got"
                failed=$((failed + 1))
            elif [ -n "$host" ] && ! cmp -s "$fcbdir/$host" "$want_file"; then
                printf 'FAIL  %s\n        %s is %s bytes, not the %s expected:\n' "$name" "$host" \
                    "$(wc -c <"$fcbdir/$host" | tr -d ' ')" "$(wc -c <"$want_file" | tr -d ' ')"
                od -An -c "$fcbdir/$host" | head -8 | sed 's/^/        /'
                failed=$((failed + 1))
            else
                printf 'PASS  %s\n' "$name"
                passed=$((passed + 1))
            fi
        }
        fcb_reset() { rm -rf "$fcbdir"; mkdir -p "$fcbdir"; }

        # Make had no mode of its own: default_mode is auto unless the config
        # says otherwise, and auto was taken for text, so a .COM a program
        # created went through the converter.  128 bytes in, 2 on disk.
        fcb_reset
        { printf 'A\r\n\032'; head -c 124 /dev/zero | tr '\0' A; } >"$tmp/want"
        check_fcb "files: make gives a .COM the binary mode open gives it" \
            MK.COM MHAJ1K2U3WC '' mk.com "$tmp/want"

        # --- where a file read or write lands --------------------------------
        # Every expectation below is what CP/M 2.2's BDOS does, not what this
        # emulator did: the record a sequential call reads or writes is the
        # one EX, S2 and CR name, and it used to be wherever the host file's
        # stdio position had got to.  DRI's GENSYS reads SYSTEM.DAT, sets CR
        # back to 0 and writes it again; the write appended, so the file grew
        # by 256 bytes every run and the next run read the stale page.  The
        # first check is that run.
        #
        # Open keeps EX and CR as the FCB has them, as CP/M's does, so a
        # script that closes and reopens its file sets CR back to 0 itself.
        fcb_reset; fcb_file "$fcbdir/system.dat" AB; fcb_file "$tmp/want" CD
        check_fcb "files: a write after CR = 0 rewrites, not appends (GENSYS)" \
            SYSTEM.DAT ORRZ0HCWHDWCOZ0RRR 'ABCD=01' system.dat "$tmp/want"
        fcb_reset; fcb_file "$fcbdir/data.dat" 0123; fcb_file "$tmp/want" 0X23
        check_fcb "files: a write in the middle lands there and truncates nothing" \
            DATA.DAT OZ1HXWCOZ0RRRRR '0X23=01' data.dat "$tmp/want"
        # After a random read or write CR names that record, so the next
        # sequential call reads it again or writes it again (CP/M 2.2
        # Interface Guide, section 1.6: "the last randomly read record will
        # be re-read").  Both used to go on from the record after it.
        fcb_reset; fcb_file "$fcbdir/data.dat" 0123
        check_fcb "files: a sequential read re-reads the random read's record" \
            DATA.DAT ON2GRRS '223(00,00,04)'
        fcb_reset; fcb_file "$fcbdir/data.dat" 0123; fcb_file "$tmp/want" 0ST3
        check_fcb "files: a sequential write rewrites the random write's record" \
            DATA.DAT ON1HWPHSWHTWCOZ0RRRRR '0ST3=01' data.dat "$tmp/want"
        # EX is a position, and so is S2.  130 records: extent 0 all 'a',
        # then b and c in extent 1.
        fcb_reset; fcb_file "$fcbdir/data.dat" "$(printf 'a%.0s' $(seq 1 128))bc"
        fcb_file "$tmp/want" "$(printf 'a%.0s' $(seq 1 128))bW"
        check_fcb "files: EX picks the extent a read and a write use" \
            DATA.DAT OX1RSHWWC 'b(01,00,01)' data.dat "$tmp/want"
        # A read of an extent's last record leaves CR = 128, and the read
        # after it carries into the next extent.  A write of it moves to the
        # next extent at once: EX + 1, CR = 0, which is 2.2's WTSEQ.
        check_fcb "files: CR 128 carries a read into the next extent" \
            DATA.DAT OZ127RSRS 'a(00,00,80)b(01,00,01)'
        fcb_reset; fcb_file "$fcbdir/data.dat" "$(printf 'a%.0s' $(seq 1 128))"
        fcb_file "$tmp/want" "$(printf 'a%.0s' $(seq 1 127))WX"
        check_fcb "files: writing an extent's last record moves to the next" \
            DATA.DAT OZ127HWWSHXWC '(01,00,00)' data.dat "$tmp/want"
        # S2 is the module above EX: 1 * 4096 + 3 * 128 + 5 = 4485 = 1185h.
        # BDOS 36 counted EX and CR only, and a CR of 128 is the next extent.
        check_fcb "files: set random record counts S2" DATA.DAT OX3Y1Z5T '#001185'
        check_fcb "files: set random record takes CR 128" DATA.DAT OZ128T '#000080'
        # A read at end of file must leave CR where it was, or an append
        # after it skips a record.  It used to step CR on the failed read.
        fcb_reset; fcb_file "$fcbdir/data.dat" 01; fcb_file "$tmp/want" 01A
        check_fcb "files: a read at end of file does not move CR" \
            DATA.DAT ORRRSHAWCOZ0RRRR '01=01(00,00,02)01A=01' data.dat "$tmp/want"
        # 2^18 records is as far as S2:EX:CR reach.  Past that CP/M 3 answers
        # 6, and so does this, with the FCB left alone.  (2.2 answers 6 from
        # 65536 on; docs/CPM_SUPPORT.md says why this does not.)
        check_fcb "files: a random record the FCB cannot hold is error 6" \
            DATA.DAT ON262144GS '=06(00,00,00)'
        # Open keeps the EX the caller asked for and clears S2 (2.2's
        # OPENFIL), rather than forcing EX to 0 and keeping S2.
        fcb_reset; fcb_file "$fcbdir/data.dat" "$(printf 'a%.0s' $(seq 1 128))bc"
        check_fcb "files: open keeps EX and clears S2" DATA.DAT X1Y5ORS 'b(01,00,01)'
        # C requires a seek between a read and a write on one stream, and
        # nothing issued one: this is a guard, and passed before on macOS.
        fcb_reset; fcb_file "$fcbdir/data.dat" 0123; fcb_file "$tmp/want" W123
        check_fcb "files: a read after a write goes on from the next record" \
            DATA.DAT OHWWRRCOZ0RRRR '12W123' data.dat "$tmp/want"

        # --- the same through the text converter ----------------------------
        # A .TXT file is converted, LF to CR LF, so a record is not 128 host
        # bytes and CR = n has to be found by converting from the top.
        fcb_reset; printf 'one\ntwo\n' >"$fcbdir/t.txt"
        check_fcb "files: CR = 0 re-reads a text file" T.TXT OLZ0L 'one<>two<>~one<>two<>~'
        fcb_reset; head -c 100 /dev/zero | tr '\0' a >"$fcbdir/t.txt"
        { printf '\n'; head -c 100 /dev/zero | tr '\0' b; printf '\nc\n'; } >>"$fcbdir/t.txt"
        check_fcb "files: CR = 1 finds record 1 of a text file" T.TXT OZ1L \
            "$(printf 'b%.0s' $(seq 1 74))<>c<>~"
        a127=$(head -c 127 /dev/zero | tr '\0' a)
        # A bare LF that converts at byte 127 of a record: the CR ended the
        # record and the LF was pushed back, so the record came back 127
        # bytes long, padded with ^Z - and a reader stops at the ^Z.
        fcb_reset; printf '%s\nb\n' "$a127" >"$fcbdir/t.txt"
        check_fcb "files: an LF converted at byte 127 does not end the text" T.TXT OL \
            "${a127}<>b<>~"
        # A CR LF already in the file, split by the record boundary: the LF
        # opening the next record was taken for a bare one and given a CR.
        fcb_reset; printf '%s\r\nb\r\n' "$a127" >"$fcbdir/t.txt"
        check_fcb "files: a CR LF across two records is not doubled" T.TXT OL \
            "${a127}<>b<>~"
        # And writing: a record ending in CR, the next opening with LF, is
        # one line end and reaches the host as one LF.
        fcb_reset
        { head -c 127 /dev/zero | tr '\0' X; printf '\n'; head -c 9 /dev/zero | tr '\0' Y; } >"$tmp/want"
        check_fcb "files: a CR LF written across two records is one LF" \
            T.TXT MHXJ127WHYK0U10WC '' t.txt "$tmp/want"
        # A text record that starts with ^Z writes no host bytes, and that is
        # not a failure: it is how a text file's last record often looks.
        fcb_reset; : >"$tmp/want"
        check_fcb "files: a text record that starts with ^Z is written" \
            T.TXT MHAU0WC '' t.txt "$tmp/want"

        # A record rewritten in place where the converter split a host LF
        # into the CR ending one record and the LF opening the next.  The
        # classic append - read to the end, back up a record, write it again
        # from its ^Z - wrote that LF a second time, a blank line; writing
        # back the record that ends in the CR put a CR over the host LF.
        fcb_reset; printf '%s\nb\n' "$a127" >"$fcbdir/t.txt"; cp "$fcbdir/t.txt" "$tmp/want"
        check_fcb "files: rewriting the record after a split line end changes nothing" \
            T.TXT ORRRZ1WC $'a\n=01' t.txt "$tmp/want"
        fcb_reset; printf '%s\nb\n' "$a127" >"$fcbdir/t.txt"; printf '%s\nb\nC\n' "$a127" >"$tmp/want"
        check_fcb "files: an append after a split line end adds no blank line" \
            T.TXT ORRRZ1V4CJ5K6U7WC $'a\n=01' t.txt "$tmp/want"
        fcb_reset; printf '%s\nb\n' "$a127" >"$fcbdir/t.txt"; cp "$fcbdir/t.txt" "$tmp/want"
        check_fcb "files: rewriting the record that ends in a split CR keeps the LF" \
            T.TXT ORZ0WLC "a>b<>~" t.txt "$tmp/want"

        # A text file is held as its CP/M image and written back as host
        # text, so a record rewritten in place gives the file a CP/M disk would
        # hold.  The converting stream wrote the new record's host text over
        # the old: the classic append - read to the end, back up a record, and
        # write it again from its ^Z - on a CR LF file wrote APPENDED LF, and
        # left the last 4 of the 25 "ab" lines after it (the reviewers' repro,
        # with append.com; this is the same calls through fcb_io).  The file
        # keeps its CR LF.
        c1() { head -c 126 /dev/zero | tr '\0' x; printf '\r\n'
               for i in $(seq 1 25); do printf 'ab\r\n'; done; }
        appendix=ORRRZ1RZ1V100AV101PV102PV103EV104NV105DV106EV107DJ108K109WC
        fcb_reset; c1 >"$fcbdir/c1.txt"; { c1; printf 'APPENDED\r\n'; } >"$tmp/want"
        check_fcb "text: the append idiom on a CR LF file leaves nothing stale" \
            C1.TXT "$appendix" 'xa=01a' c1.txt "$tmp/want"
        # The same on the LF copy of it, which ends LF.
        fcb_reset; c1 | tr -d '\r' >"$fcbdir/c1.txt"
        { c1 | tr -d '\r'; printf 'APPENDED\n'; } >"$tmp/want"
        check_fcb "text: the append idiom on an LF file" C1.TXT "$appendix" 'xa=01a' c1.txt "$tmp/want"
        # And on CP/M's own form, ^Z-padded to a record: it keeps the padding.
        fcb_reset; { c1; printf '\032'; head -c 27 /dev/zero | tr '\0' '\032'; } >"$fcbdir/c1.txt"
        { c1; printf 'APPENDED\r\n\032'; head -c 17 /dev/zero | tr '\0' '\032'; } >"$tmp/want"
        check_fcb "text: the append idiom on a ^Z-padded CR LF file" \
            C1.TXT "$appendix" 'xa=01a' c1.txt "$tmp/want"
        # A random record of a text file is the record a sequential read
        # reads, and the file size counts those records; both were the host's
        # raw bytes, which is not where the text is once LF has become CR LF.
        fcb_reset; { head -c 126 /dev/zero | tr '\0' a; printf '\nbc\n'; } >"$fcbdir/t.txt"
        check_fcb "text: a random read reads the converted record" T.TXT ON1G 'b'
        fcb_reset; for i in $(seq 1 200); do printf 'a\n'; done >"$fcbdir/t.txt"
        check_fcb "text: file size counts the converted records" T.TXT F '#000005'
        fcb_reset; { head -c 126 /dev/zero | tr '\0' a; printf '\nbc\n'; } >"$fcbdir/t.txt"
        { head -c 126 /dev/zero | tr '\0' a; printf '\nQ\n'; } >"$tmp/want"
        check_fcb "text: a random write writes the converted record" \
            T.TXT ON1HQJ1K2U3PC '' t.txt "$tmp/want"
        # A shorter record in place: the text ends where the new ^Z is, and
        # the host file is cut there.  The stream left the old text after it.
        fcb_reset; for i in $(seq 1 100); do printf 'line %d\n' $i; done >"$fcbdir/t.txt"
        printf 'X\n' >"$tmp/want"
        check_fcb "text: a record rewritten shorter cuts the text there" \
            T.TXT OHXJ1K2U3WC '' t.txt "$tmp/want"

        # Random files: every sequence of calls, against a model of the
        # image.  tests/text_image_prop.py has the definition.
        if command -v python3 >/dev/null 2>&1; then
            got=$(cd "$fcbdir" && python3 "$root/tests/text_image_prop.py" "$emu" \
                  "$tmp/text_ops.com" --cases 300 2>&1)
            if [ $? -eq 0 ]; then
                printf 'PASS  text: %s, and the host file they leave\n' \
                    "reads and writes of random text files match the image ($got)"
                passed=$((passed + 1))
            else
                printf 'FAIL  text: reads and writes of random text files match the image\n'
                printf '%s\n' "$got" | head -40 | sed 's/^/        /'
                failed=$((failed + 1))
            fi
        else
            echo "SKIP  text: the random text file check (no python3)"
            skipped=$((skipped + 1))
            soft_skip python3 "text: the random text file check, no python3"
        fi

        # PIP, ED and WordStar write NAME.$$$ and rename it at the end.  $$$
        # is on neither extension list, so make writes it as it comes, and the
        # rename is where its real name arrives: a text name turns the host
        # copy into host text, as though it had been made under that name.
        # It stayed CP/M text, CR LF and ^Z padding, which is what a text
        # copy through PIP left on the host.  Anything that is not plainly
        # text - here a record of NULs - is left exactly as written, and so is
        # a file renamed to another name the lists do not know.
        fcb_reset; printf 'A\nB\n' >"$tmp/want"
        check_fcb "files: a text file made as X.\$\$\$ and renamed X.TXT is host text" \
            'U.$$$' 'MHAJ1K2V3BJ4K5U6WC>U.TXT;' '' u.txt "$tmp/want"
        fcb_reset; head -c 128 /dev/zero >"$tmp/want"
        check_fcb "files: a binary file renamed to a text name is left as written" \
            'U.$$$' 'MWC>U.TXT;' '' u.txt "$tmp/want"
        fcb_reset; { printf 'A\r\n\032'; head -c 124 /dev/zero | tr '\0' A; } >"$tmp/want"
        check_fcb "files: a file renamed to a name auto cannot place is left as written" \
            'U.$$$' 'MHAJ1K2U3WC>U.BAK;' '' u.bak "$tmp/want"
        # A mode rule decides for the new name as it would for any other.
        { echo "program = $tmp/fcb_io.com"; echo "*.TXT = binary"; } >"$tmp/fcbbin.cfg"
        fcb_reset; { printf 'A\r\n\032'; head -c 124 /dev/zero | tr '\0' A; } >"$tmp/want"
        fcb_cfg=$tmp/fcbbin.cfg check_fcb "files: a rename under *.TXT = binary is left as written" \
            'U.$$$' 'MHAJ1K2U3WC>U.TXT;' '' u.txt "$tmp/want"
        # A renamed file, and a file named on the command line, is found
        # through a table that took the mode from the extension alone and
        # ignored the rule; T.TXT is on the command line here because it is a
        # file.  Binary, its LFs are not given CRs.
        fcb_reset; printf 'a\nb\n' >"$fcbdir/T.TXT"
        fcb_cfg=$tmp/fcbbin.cfg check_fcb "files: a mode rule applies to a file named on the command line" \
            T.TXT OL 'a>b>~'

        # A name on the text list opens as text only if it holds text, and is
        # made as it comes and turned into host text at its close if it is
        # text.  Every name on that list also names binary files.  DRI's LINK
        # read XDOS2.LIB, a REL library LIB had made, through the converter,
        # which ended it at the first ^Z in its first record: DISK READ ERROR.
        # Here Q, a ^Z, and a whole record after it: binary reads both records.
        fcb_reset; { printf 'Q\032'; head -c 126 /dev/zero | tr '\0' R
                     head -c 128 /dev/zero | tr '\0' T; } >"$fcbdir/x.lib"
        check_fcb "files: a REL library named .LIB opens binary" X.LIB ORRR 'QT=01'
        # A macro library is text: LF becomes CR LF.
        fcb_reset; printf 'a\nb\n' >"$fcbdir/m.lib"
        check_fcb "files: a macro library named .LIB opens as text" M.LIB OL 'a<>b<>~'
        # A tokenized MBASIC program: FFh, then its bytes, LF and NUL among them.
        fcb_reset; { printf '\377A\n\000B\r\n\032'; head -c 120 /dev/zero | tr '\0' C; } >"$fcbdir/t.bas"
        check_fcb "files: a tokenized .BAS opens binary" T.BAS OL '?A>?B<>~'
        # A WordStar document: 8Dh LF is a soft return, CR LF a hard one.  The
        # converter gave the soft one a CR, which WordStar reads as a hard one.
        fcb_reset; printf 'Hello\215\nworld\r\n\032' >"$fcbdir/w.doc"
        check_fcb "files: a WordStar document opens binary" W.DOC OL 'Hello?>world<>~'
        # Not UTF-8, but a host file of bare LFs with a Latin-1 character in it
        # needs the converter, and gets it.
        fcb_reset; printf 'caf\351\nx\n' >"$fcbdir/l.asm"
        check_fcb "files: an LF file with a Latin-1 byte still opens as text" L.ASM OL 'caf?<>x<>~'
        # Made: written as it comes, and host text at the close if it is text.
        # A record of binary with a CR LF in it stays 128 bytes; made as text
        # it lost the CR.  MBASIC saves a tokenized program with a random write.
        fcb_reset; { printf 'A\000\000\000\000B\000\000\000\r\n'; head -c 117 /dev/zero; } >"$tmp/want"
        check_fcb "files: a binary record made under a text name is kept" \
            T.BAS MV0AV5BJ9K10WC '' t.bas "$tmp/want"
        check_fcb "files: and so is one written at random" \
            T.BAS MV0AV5BJ9K10N0PC '' t.bas "$tmp/want"
        fcb_reset; printf 'A\n' >"$tmp/want"
        check_fcb "files: a text record made under a text name is host text at its close" \
            T.PRN MHAJ1K2U3WC '' t.prn "$tmp/want"
        fcb_reset; printf 'A\n' >"$tmp/want"
        check_fcb "files: and at the end of the run when it is never closed" \
            T.PRN MHAJ1K2U3W '' t.prn "$tmp/want"
        # Written in sequence, it is converted as the text writer always did,
        # a bare LF and all - RMAC's listings have one after the title line.
        # Written at random, it has to read back record for record, and a
        # bare LF would not: it stays as written.
        fcb_reset; printf 'A\n\n' >"$tmp/want"
        check_fcb "files: a listing with a bare LF is host text at its close" \
            T.PRN MHAJ1K2K3U4WC '' t.prn "$tmp/want"
        fcb_reset; { printf 'A\r\n\n\032'; head -c 123 /dev/zero | tr '\0' A; } >"$tmp/want"
        check_fcb "files: one written at random with a bare LF is kept as written" \
            T.PRN MHAJ1K2K3U4N0PC '' t.prn "$tmp/want"

        # Open fails for an extent the file does not have, as 2.2's does, and
        # RC is that extent's record count.  It opened any extent asked for
        # with RC = 128, so opening extents 0, 1, 2 ... until one fails - the
        # CP/M 1.4 way to find a file's end - never stopped.
        fcb_reset; fcb_file "$fcbdir/data.dat" 0
        check_fcb "files: open fails for an extent the file does not have" DATA.DAT X5O '=FF'
        fcb_reset; head -c 38400 /dev/zero | tr '\0' x >"$fcbdir/data.dat"
        check_fcb "files: open gives each extent its record count" \
            DATA.DAT O@X1O@X2O@X3O '@80@80@2C=FF'
        fcb_reset; for i in $(seq 1 100); do printf 'a\n'; done >"$fcbdir/t.txt"
        check_fcb "files: a text file's RC counts the CR the converter adds" T.TXT O@ '@03'
        fcb_reset; : >"$fcbdir/data.dat"
        check_fcb "files: an empty file opens with RC 0" DATA.DAT O@ '@00'
        # And a program that then makes the extent, as CP/M 1.4 programs
        # extend a file, gets that extent of the file it has.  Make truncated
        # whatever the FCB's EX, so the file before it became zeros.
        a128=$(printf 'a%.0s' $(seq 1 128))
        fcb_reset; fcb_file "$fcbdir/data.dat" "$a128"; fcb_file "$tmp/want" "${a128}B"
        check_fcb "files: making extent 1 of a file keeps extent 0" \
            DATA.DAT X1OMSHBWC '=FF(01,00,00)' data.dat "$tmp/want"

        # CP/M keeps an open file's state in its FCB, so a read after a close,
        # after a disk reset, or through a copy of the FCB goes on from it.
        # Only a sequential write did; the rest answered 0xFF.
        fcb_reset; fcb_file "$fcbdir/data.dat" 012
        check_fcb "files: a read after close goes on from the FCB" DATA.DAT ORCRS '01(00,00,02)'
        check_fcb "files: a read after a disk reset goes on" DATA.DAT ORBRS '01(00,00,02)'
        check_fcb "files: a copy of an open FCB reads on" DATA.DAT ORIRS '01(00,00,02)'
        check_fcb "files: a random read after close" DATA.DAT OCN2GS '2(00,00,02)'
        fcb_reset; fcb_file "$fcbdir/data.dat" 012; fcb_file "$tmp/want" 0X2
        check_fcb "files: a random write after close" DATA.DAT OCN1HXPC '' data.dat "$tmp/want"
        check_fcb "files: a read of a file that is not there still fails" NONE.DAT R '=FF'

        # Search returns one entry per extent, and the FCB's EX picks which:
        # '?' every one, a number that one.  It returned one entry per file,
        # EX = 0, RC at most 128, so a lister adding up extents saw 300
        # records as 128.  300 records: 128, 128, 44.
        fcb_reset; head -c 38400 /dev/zero | tr '\0' x >"$fcbdir/data.dat"
        check_fcb "search: EX = ? returns every extent" DATA.DAT X63DAAA '[00,00,80][01,00,80][02,00,2C]=FF'
        check_fcb "search: EX = 1 returns extent 1" DATA.DAT X1DA '[01,00,80]=FF'
        check_fcb "search: an extent the file does not have is not found" DATA.DAT X3D '=FF'
        check_fcb "search: EX = 0 is one entry per file" DATA.DAT DA '[00,00,80]=FF'
        # A drive byte of '?' makes 2.2 compare nothing, EX included, so a
        # lister that sets it gets every extent whatever EX was left holding.
        check_fcb "search: a '?' drive byte returns every extent" DATA.DAT '&63X5DAAA' \
            '[00,00,80][01,00,80][02,00,2C]=FF'

        # A CR the text writer holds at a record's end reaches the file when
        # the run ends at the end of its input, not only when the program
        # finishes: that exit, the five-^C one and the watchdog closed nothing.
        fcb_reset; { head -c 127 /dev/zero | tr '\0' A; printf '\r'; } >"$tmp/want"
        (cd "$fcbdir" && "$emu" "$tmp/fcb_io.com" T.TXT 'MHAJ127W!' </dev/null >/dev/null 2>&1)
        if cmp -s "$fcbdir/t.txt" "$tmp/want"; then
            printf 'PASS  %s\n' "files: a held CR is written when input runs out"
            passed=$((passed + 1))
        else
            printf 'FAIL  %s\n        t.txt is %s bytes, not the 128 expected\n' \
                "files: a held CR is written when input runs out" \
                "$(wc -c <"$fcbdir/t.txt" | tr -d ' ')"
            failed=$((failed + 1))
        fi

        # An FCB and a DMA buffer at the top of memory: see tests/mem_top.asm.
        fcb_reset; { head -c 64 /dev/zero | tr '\0' A; head -c 64 /dev/zero | tr '\0' B; } >"$fcbdir/wrap.dat"
        got=$(cd "$fcbdir" && "$emu" "$tmp/mem_top.com" 2>"$tmp/fcberr")
        if [ "$got" = FF00AB ] && [ ! -e "$fcbdir/topfcb.dat" ]; then
            printf 'PASS  %s\n' "memory: an FCB past FFFFh is refused, a DMA buffer wraps"
            passed=$((passed + 1))
        else
            printf 'FAIL  %s\n        expected FF00AB and no topfcb.dat\n        got      %s\n' \
                "memory: an FCB past FFFFh is refused, a DMA buffer wraps" "$got"
            ls "$fcbdir" | sed 's/^/        /'
            failed=$((failed + 1))
        fi
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
                # A skip the sub-harness printed is a skip of this suite, and
                # --require was blind to these: pty_console.cc skips its whole
                # run when no pty can be opened, and that is 42 checks - the
                # entire reason the macOS job exists - vanishing under a green
                # tick.  Measured: forcing that branch gave "60 passed, 0
                # failed, 5 skipped" and exit 0 with --require set.
                grep '^SKIP  ' "$ptylog" >"$tmp/ptyskips" 2>/dev/null || :
                while IFS= read -r skipline; do
                    [ -n "$skipline" ] || continue
                    soft_skip posix-console "posix console: ${skipline#SKIP  }"
                done <"$tmp/ptyskips"
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
        # Hand the rule down.  win_console.bat turns its own "no compiler"
        # skip, and win_console.exe's "no emulator" and "no console to drive"
        # skips, into failures when this is set - and without it the whole
        # CPMEMU_REQUIRE_MSVC chain added for CI was unreachable from here, so
        # `run_tests.sh --require` on a Windows box with no Visual Studio
        # exited 0 having run none of the 26 console cases.
        if [ "$require_all" = 1 ]; then
            MSYS_NO_PATHCONV=1 CPMEMU_REQUIRE_MSVC=1 cmd.exe /c "$winbat" >"$winlog" 2>&1
        else
            MSYS_NO_PATHCONV=1 cmd.exe /c "$winbat" >"$winlog" 2>&1
        fi
        # Its own totals line would double count against this script's
        grep -v -e '^[0-9][0-9]* passed' -e '^$' "$winlog"
        # Counting only the verdicts it printed would let a launch that never
        # happened pass as an empty success, which is the one failure this
        # section cannot be allowed to have
        if grep -q '^\(PASS\|FAIL\|SKIP\)  ' "$winlog"; then
            passed=$((passed + $(grep -c '^PASS  ' "$winlog")))
            failed=$((failed + $(grep -c '^FAIL  ' "$winlog")))
            skipped=$((skipped + $(grep -c '^SKIP  ' "$winlog")))
            # Belt and braces: with CPMEMU_REQUIRE_MSVC set these come back as
            # FAIL rather than SKIP, so this is for the case where some future
            # skip in there does not honour the variable.
            grep '^SKIP  ' "$winlog" >"$tmp/winskips" 2>/dev/null || :
            while IFS= read -r skipline; do
                [ -n "$skipline" ] || continue
                soft_skip windows-console "windows console: ${skipline#SKIP  }"
            done <"$tmp/winskips"
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

# A skip exits 0, so a machine with no assembler runs about half of this
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
