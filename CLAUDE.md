# cpmemu

A CP/M 2.2 emulator: it runs CP/M `.com` files on Linux, macOS and Windows by
emulating the 8080/Z80 and answering BDOS and BIOS calls on the host.
`README.md` is the reference for what it does and how it is used. This file is
only the things a session working on it has to know that the code does not say.

## Build and test

    make -C src                 # cpmemu
    tests/run_tests.sh          # the suite that can fail: 102 cases, about 30s
    tests/run_tests.sh --zex    # and zexdoc, zexall, 8080exm: about 18 minutes
    make -C src unit            # the 8080 unit tests on their own

`make -C src test` runs three guests as an eyeball check and asserts nothing.
`tests/run_tests.sh` is the one that exits non-zero, and it also cross-compiles
`src/os/windows/platform.cc` with mingw and fails on any warning.

Windows is `src/do_build.bat` (MSVC x64) to build and `tests\win_console.bat` to
test; both ask `vswhere.exe` where Visual Studio is. `.github/workflows/ci.yml`
runs all three platforms on push, and sets `CPMEMU_REQUIRE_MSVC=1` on the
Windows job so a skip fails instead of exiting 0.

Do not pass `STATIC=1` on macOS. The SDK ships no `crt0.o` and no static libc or
libc++, and `src/makefile` refuses the flag rather than letting it become a link
error.

## The assembler is um80, and there is no second one

`um80` and `ul80` - this project's own assembler and linker, from
[avwohl/um80_and_friends](https://github.com/avwohl/um80_and_friends), installed
with `pip install um80` - assemble every Z80 and 8080 source in this repository
and in the sibling projects. **Do not install pasmo. Do not install z80asm. Do
not add a fallback to either, or to z88dk, and do not add a branch that tries
one and then the other.** If `um80` is missing, the answer is to install it.

The suite accepted `pasmo` if it was on `PATH` and `z80asm` otherwise until
2026-09-10. Nothing was wrong with either one's output - the changelog records
`tests/sectran.asm` assembling byte-identical under each. The problem is that
two tools for one job means the program that assembled the guests depended on
the machine, and a third outcome, *neither installed*, skipped 42 checks and
exited 0. A fallback is not robustness here; it is three behaviours where one
belongs.

Two things about the dialect, both of which have already cost time:

- **`.z80` on the first line, always.** Without it `um80` is an 8080 assembler
  and the first `LD` fails the file - `Unknown instruction or directive: LD`.
  All 13 sources the suite assembles needed this line added when they were
  converted; every one of them failed on `LD` until it was there.
- **Never write `org 0100h` in a `.COM` source.** `ul80` bases a relocatable
  code segment at 0100h by itself, so an ORG is applied *on top of* that base
  and puts the code at 0200h. Measured on `tests/drv_read.asm`: with the ORG it
  links to 384 bytes, of which the first 256 are zero and only 128 are code.
  It still runs, because CP/M loads the whole file at 0100h and the Z80 slides
  through 256 NOPs into the code, which is exactly why the bug survives review.
  The correct build is two commands with no origin flag:

      um80 -o drv_read.rel drv_read.asm && ul80 -o drv_read.com drv_read.rel

  (`-p` does not help: it names where the image is *loaded*, not where the
  segment starts. `-p 0000` is for a ROM image, which does start at zero.)

## The rule that is easy to miss

`src/qkz80*.{cc,h}` is the CPU core, and three sibling projects — ioscpm,
z80cpmw and romwbw_emu — compile those files directly out of a neighbouring
working tree rather than depending on a cpmemu release. None of the three has a
version gate: romwbw_emu had the only one and removed it on 2026-09-01, so all
three now build whatever this repository's default branch holds and an edit to
`qkz80.cc` lands in all of them on their next build with no notification. Read
[Who else compiles qkz80](README.md#who-else-compiles-qkz80) before changing
qkz80's public surface: it names the compiler, language standard and warning set
each one uses, and they are not this repo's.

## Where work is recorded

There is no issue tracker beyond these files and GitHub issues on
`avwohl/cpmemu`.

**`CHANGELOG.md`** — every substantive change gets an entry under the release it
ships in. "No release this time" does not mean "no changelog". The entries
summarise and point; the commit message that made the change is the detail, and
is expected to be the longer of the two and to carry the measurements.

**`todo.txt`** — open items only, each tagged with what a machine has to be to
pick it up (`[MAC]`, `[WINDOWS]`, `[RELEASE]`), so a session on another OS can
see at a glance what it can take. Finished work leaves the file: it lives in
`CHANGELOG.md` and in the commit that made the change.

An item is a bug or a feature for a coming release. It is not a rule — rules go
in this file. It is not a note to self, an observation, or a restatement of
something `README.md` or `MANUAL_CHECKS.md` already says. It is not a task that
needs a person at a keyboard, which is what `MANUAL_CHECKS.md` is for.

This file is meant to shrink. A session that ends with `todo.txt` longer than it
started has moved work rather than done it; write the fix, not the ticket. If an
item cannot be finished, say in the item what specifically is missing — a
certificate, a machine, a decision only the owner can make — rather than
restating the problem.

**`MANUAL_CHECKS.md`** — checks that need a person at a keyboard, a real
terminal program, or a machine that is not this one, with what right looks like
written down so the person only has to compare. Delete an item once someone runs
it: if it found something, the fix goes in `CHANGELOG.md` and whatever is still
open goes in `todo.txt`; if it found nothing, the deletion and the commit
message are the record. It is not a log of checks that were done.

## Conventions

No pull requests. Push branches directly.

A commit subject is a sentence in the imperative saying what changed and what it
fixes — "console: stop the status calls lying about buffered input", not "fix
console bug". The body carries the evidence: what was measured before, what is
measured now, and what was deliberately not done. Those messages are the
project's real history and are written to be read years later.

State what was measured rather than what ought to be true. "Never executed
anywhere" and "measured on macOS 27 arm64" are the register this tree is written
in; a claim that a thing works is expected to name the run that showed it.
