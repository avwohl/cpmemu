# Changelog

All notable changes to **cpmemu** are documented here.

This file starts at `v4.6.0`. For most of this project's life the record of a
change has been the commit message that made it, and those messages are longer
and more specific than any changelog entry — they carry the measurements, the
counter-examples and the things that were deliberately *not* done. This file
summarises and points; `git log` is the detail. Open work is in
[`todo.txt`](todo.txt).

## [Unreleased]

Everything below has landed since the `v4.6.0` tag and is not yet in a release.

### Added

- **The Windows console is tested on a real console.** `tests/win_console.cc`
  starts the emulator with stdin bound to the console it is attached to, writes
  the `INPUT_RECORD`s a keyboard produces with `WriteConsoleInput`, and compares
  the bytes the CP/M guest received — twenty cases over seven hand-assembled
  guest programs, each printing what it was handed as hex, so a failure names
  the wrong byte instead of describing a missing behaviour. Sixteen of the
  twenty passed against the pre-change binary, which settled the open question
  about the WordStar diamond table: the scan codes for Ctrl+Up and Ctrl+Down had
  never been observed by anyone here, and they are correct. Four failed and are
  fixed below. `tests/win_console.bat` builds and runs it; `run_tests.sh` runs
  it on Windows and skips elsewhere.
- **The POSIX terminal is tested through a real pty.** `tests/pty_console.cc` is
  the counterpart, running the same guest programs through `tests/con_guests.h`
  so a case named the same on both platforms can be read side by side. The
  terminal layer had been reachable by no test at all — `enable_raw_mode()`
  returns immediately when stdin is not a tty, so every test in the suite ran
  with the whole layer switched off and a clean compile was the only evidence
  there was. The harness was validated against four deliberately broken
  emulators, each caught by exactly the cases that should notice.
- **The Windows build is cross-compiled by the test suite.** `run_tests.sh`
  cross-compiles the Windows half into a temporary directory whenever
  `x86_64-w64-mingw32-g++` is on PATH, and fails on a warning as well as an
  error. No Linux build and no CI job touches that code, so a change breaking
  only that side used to sit undetected until someone built on Windows.
- **`drive_A` through `drive_P` back a CP/M drive letter with a host
  directory.** The FCB drive byte was read and discarded, so `B:FOO.TXT`
  resolved against the working directory and would open A's copy if one was
  there. A configured drive is now confined to its directory, so a miss fails
  rather than falling through to something unrelated — the failure that reads to
  the guest as success. An unconfigured drive resolves exactly as before,
  verified byte for byte against the previous binary. BDOS 22 Make never went
  through the resolver at all, so a guest that made `B:FOO.TXT` and then opened
  it got two different files; Search First now scans the drive's directory; BDOS
  24 reports A plus every configured or selected drive; BIOS SELDSK accepts
  every letter, so the BIOS and the BDOS no longer disagree.
- **Ctrl+Up and Ctrl+Down** join the Windows extended-key table as `^W` and `^Z`,
  the WordStar scroll commands, beside the Ctrl+Left and Ctrl+Right already
  there. They previously missed the lookup, came back as a synthesized 0 and
  were swallowed with no sign to the user.
- `QKZ80_NO_TRACE` compiles the CPU core's trace calls out. Tracing is still
  compiled in by default; embedders that never call `set_trace()` were paying a
  virtual call at every traced event. The three downstream ports picked it up on
  their next build without being told.
- `--ctrl-c-exit` / `--no-ctrl-c-exit` and a `ctrl_c_exit` config directive.
- `-Wextra` and `-Wshorten-64-to-32` are on in `src/makefile`.

### Fixed

- **The status calls lied about buffered input.** `stdin_has_data()` selected on
  the raw fd while `console_getchar()` went through `getchar()`; a single
  `read(2)` pulls a whole burst into stdio's buffer, `select()` cannot see past
  the first byte, and BDOS 6, BDOS 11 and BIOS CONST then all reported "no
  character" with the rest of the input sitting in that buffer. Reproduced on a
  pty three runs out of three. Since a POSIX arrow key is `ESC [ A`, a program
  polling BDOS 6 took the ESC and stalled with `[A` stuck in the buffer — the
  POSIX counterpart of the Windows arrow-key bug. Fixed by deleting the second
  buffer rather than keeping two in step. The same split existed on the Windows
  pipe path and is fixed the same way.
- **Five ^C killed the emulator with no way to turn it off**, and WordStar binds
  ^C to page-down, so five page-downs lost the unsaved document. The exit is
  switchable now, and the five have to land within two seconds of the first.
- **A reader looped forever on exhausted input.** BDOS 1 turned every read past
  the end of redirected input into CR, so a program that keeps reading emitted
  carriage returns until something killed it. The first read past the end still
  answers CR; later ones answer `^Z`, which is what BIOS CONIN here has always
  returned — the two calls disagreed. A guest that checks for neither is stopped
  after 1024 consecutive reads past the end.
- **BDOS 6 spun at end of input** for the same reason from the other side:
  `bdos_direct_console_io` turned the -1 into 0, which is also its value for
  "nothing waiting yet", so the give-up counter neither incremented nor reset. It
  routes through the shared `note_console_eof()` now and exits in 0.00s where
  the pre-fix binary had to be killed after 15 seconds. This closes the tty and
  Windows cases only — on POSIX a redirected *file* still never reaches the
  read, which is open in `todo.txt`.
- **BDOS 10 silently dropped about twenty control characters**, TAB and ESC
  included, so ESC-to-cancel never reached the guest. It consumes only what
  CP/M 2.2 RDBUF consumes, stores the rest, and implements `^R`, `^E`, `^X` and
  `^P`.
- **POSIX raw mode clears IEXTEN**, so BSD and XNU stop eating VLNEXT (`^V`) and
  VDISCARD (`^O`) — they are gated on IEXTEN alone, outside the ICANON block, so
  the macOS build was losing both. Measured on macOS 27 arm64: with the clear,
  every byte `0x01`–`0x1F` and `0x7F` reaches the guest; without it, `^O` and
  `^V` vanish and nothing else changes. The Linux-only reader is told so by the
  test itself, which reports what the clear was worth on the machine it ran on.
- **Windows clears `ENABLE_QUICK_EDIT_MODE`**, so a stray mouse click no longer
  starts a selection that freezes guest input and shadows `^C`.
- **A file of 2^31 records or more came back empty.** `write_dir_entry` computed
  `int records = (file_size + 127) / 128` from an `int64_t`; past 274877906817
  bytes — reachable on any LP64 host, cheap with a sparse file — `records` goes
  negative, the clamp never fires, and the RC byte and the whole allocation map
  come back zero, so the guest sees a 256 GiB file as empty. `bdos_file_size`
  had the same shape and now saturates rather than wrapping to a small number
  that looks legitimate.
- **Emulator options written after the program name were handed to the guest.**
  `cpmemu prog.cfg --no-ctrl-c-exit` left the exit on and said nothing. Both
  passes now share one definition of the option chain, and recognised options
  are removed from the command tail rather than duplicated into it.
- **A mistyped config key is reported instead of absorbed.** Anything the parser
  does not recognise becomes a file mapping, so `verbsoe = 1` registered a
  mapping for a CP/M file named VERBSOE and said nothing. It still does that —
  a bare word is a legal CP/M name — but no longer in silence. Two deliberately
  narrow checks, mutually exclusive so a line warns once, and every config in
  `examples/` was checked for false positives.
- **The two dead file-mapping forms work.** `*.BAS = /dir/*.bas` used the host
  side literally and opened nothing; a `*` there now takes the text the CP/M
  pattern matched. `*.BAS = text` registered "text" as a path; a value that is
  only a mode is a mode rule now, applied to whatever the later steps resolve,
  so it composes with drive directories rather than competing with them.
- **`run_tests.sh` had never run in any form** — CRLF line endings killed it at
  parse time, and underneath that it invoked a binary no build file in the repo
  produces. Rewritten rather than repaired: eleven defects in all, the root one
  being a `2>&1` that forced a `grep -v` filter which deleted test 1's output
  outright. `make_test.sh`, `test_emulator.sh` and `test_ixh_debug.sh` had the
  same faults and are fixed the same way.

### Documentation

- **A full `zexdoc`/`zexall` run is recorded: 67 instruction groups each, zero
  CRC mismatches, 13m46s for the pair.** `tests/README.md` had said both suites
  "show CRC mismatches on all tests" and listed the instruction groups it blamed.
  Nothing could tell a finished run from a truncated one, because the old runner
  capped the suites at 180 seconds — about five groups in — and printed the
  partial output as the result. The exercisers are opt-in behind `--zex` now,
  and a run that stops early cannot read as success.
- **The example configs documented three features that do not exist**:
  `drive_A`/`drive_B`/`drive_C` were not directives and fell through to the
  generic mapping branch, a wildcard on the Unix side did nothing, and a
  mode-only mapping did nothing. Three of the seven configs were built entirely
  around the first. All seven are rewritten against what the parser actually
  does, and `examples/README.md` is rebuilt around a table of the real
  directives — nine at the time of that rewrite, ten once `drive_A`…`drive_P`
  became real (see Added, above) — with the dead forms listed explicitly so
  nobody reintroduces them.
- README corrections: Z80 is the default CPU mode, not 8080; `--save-memory`,
  `--save-range`, `--int-cycles`, `--int-rst` and `CPM_DEBUG` were undocumented;
  BDOS 10 is supported, not a stub. The MSIX download link 404s — only v4.5.0
  and v4.5.1 ever had one — and now points at the asset that exists.
- Ctrl+V on Windows Terminal is documented as a known limitation: the binding
  does not fall through and no `SetConsoleMode` call changes it.

## [4.6.0] and earlier

Not written up here. See `git log` — the commit messages are the record.
