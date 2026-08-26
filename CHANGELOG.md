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

- **8080 mode is tested, and `tests/8080EXM.COM` is wired in.** `--8080` is a
  real feature and nothing tested it: zexdoc and zexall are thorough but they
  run the CPU as a Z80, so every rule that makes 8080 mode different — parity
  instead of overflow, the auxiliary carry, the fixed flag bits, no N flag —
  was reachable by no test at all. Two things close that. `tests/unit_8080.cc`
  links the CPU core directly and walks the input space where it is small
  enough to walk: 3.1 million ALU cases across the register, immediate and
  memory forms, 65536 per 16-bit increment, every value of the flag byte,
  checked against the documented 8080 rules written out in the test rather than
  against a recording of this emulator. It runs in under a second and is part
  of the default `run_tests.sh`; `make -C src unit` runs it alone. And
  `tests/8080EXM.COM` — Ian Bartholomew's 8080 conversion of the same exerciser,
  in the tree and referenced by nothing since the first commit — now runs under
  `--zex` with `--8080`. It was right and the emulator was wrong: 19 of its 25
  groups mismatched, and both causes are fixed below. All 25 pass now.
- **The ADM-3A to ANSI output translator is tested.** Every other expected
  string in the suite is plain ASCII, so nothing anywhere put a byte into
  `console_output()` that changed `term_state`: the four-state escape parser,
  `ESC =` cursor addressing and the Kaypro `ESC G` attribute byte were covered
  by nothing. `tests/adm3a.asm` sends one of each, plus the control codes and
  an unknown escape, and the expectation is the exact ANSI byte string.
- **A suspend and a resume are tested, and so is a background start.** The two
  new cases at the end of `tests/pty_console.cc` build a session and a stand-in
  shell of their own, because a shell is what makes the bug below visible and
  the rest of that harness is deliberately not one. Worth knowing if you extend
  them: a child in an orphaned process group has `SIGTSTP` discarded rather than
  delivered, so the simpler setup cannot stage a stop at all — an earlier draft
  reported "SIGTSTP did not stop the emulator" and was measuring its own
  scaffolding.
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
- `-Wextra` is on in `src/makefile`, and `-Wshorten-64-to-32` where the
  compiler has it (see Fixed, below).

### Fixed

- **`-Wshorten-64-to-32` stopped every GCC build**, `release.yml`'s
  `ubuntu-latest` and `ubuntu-24.04-arm` jobs included. It is a Clang-only
  option and GCC does not ignore an unknown `-W` flag, it fails the compile, so
  the build died on the first object file. `src/makefile` probes for it now and
  adds it only where it exists. Release-blocking: the flag and the breakage
  arrived in the same unreleased change, so nothing released carries it and
  nothing released would have built either.
- **`DAA` in 8080 mode always subtracted.** It read bit 1 of the flag register
  as the Z80's N flag, and 8080 mode forces that bit to 1 because that is what
  an 8080's flag register reads back — so every `DAA` under `--8080` took the
  subtract path. `DAA` on `1Ah` gave `14h` where an 8080 gives `20h`; on `9Ch`
  it gave `36h` where an 8080 gives `02h` with carry. That is every BCD program
  under `--8080`, not an edge case, and it is in `v4.6.0` and every tag before
  it. Z80
  mode is untouched by this and by the fix below — both are gated on the mode,
  no public signature changed, and zexdoc and zexall still report 67 groups each
  with no mismatches, which matters because three other emulators compile
  `src/qkz80*` directly out of this tree.
- **`CMA`, `STC` and `CMC` wrote the Z80's half carry in 8080 mode.** On the
  8080 `CMA` affects no condition bit and `STC` and `CMC` affect only the carry;
  the bit the Z80 rules were writing there is the 8080's auxiliary carry, which
  a following `DAA` reads. This is why 19 of `8080EXM`'s 25 groups mismatched
  rather than the two that name `DAA` and the rotates: the exerciser's own
  harness runs an `STC` after every test instruction, to put back the carry it
  clobbered reading the stack pointer, and that `STC` was clearing the auxiliary
  carry the instruction under test had just produced. A wrong `STC` corrupted
  the recorded state of almost every group, `MOV` and `MVI` included, which set
  no flags at all.
- **Suspend and resume left the emulator on a cooked terminal for the rest of
  its life.** `enable_raw_mode()` ran once, at startup, and nothing re-applied
  it, while bash and zsh write their own termios back when they take a job into
  the foreground. Measured against a real interactive bash: `kill -TSTP`, then
  `fg`, and the guest received none of what was typed at it — the line
  discipline echoed `B.` back to the shell instead, and the emulator had to be
  killed. There is a `SIGTSTP` handler now that puts the terminal back before
  the process stops, and a `SIGCONT` handler that takes it again; after the fix
  the same script gets `42 2E` and a clean exit. Of the two, the resume is the
  one a bash user sees — bash and zsh write their own termios over the stopped
  job's terminal anyway, so they mask the first half — but a terminal handed
  back raw is still the emulator's own bug wherever the parent is not a
  job-control shell, and `tests/pty_console.cc` asserts it directly rather than
  through what a shell happens to paper over. The unblock in the `SIGTSTP`
  handler is not decoration: `signal()` gives BSD semantics, which block the
  signal for the duration of its own handler, so a bare `raise()` there only
  marks it pending and the emulator keeps running. `SIGCONT` is handled
  separately rather than folded in, because `SIGSTOP` cannot be caught at all
  and `SIGTTIN`/`SIGTTOU` stop the process without going near `SIGTSTP` — that
  last one is `cpmemu prog.com &` followed by `fg`, where the `tcsetattr` in
  `enable_raw_mode()` runs in a background process group. On Linux that
  interrupted `ioctl` is restarted after the `fg` and completes with no handler
  involved, so the handler is what a platform returning `EINTR` needs. One thing
  it cannot fix, said plainly: a shell that writes the terminal *after* sending
  `SIGCONT` is racing the handler, and nothing here can win that race. bash and
  zsh hand the terminal over first.
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

### Removed

- **Fifteen compiled test binaries, and fifteen dead sources — not quite the
  same set.** The binaries were build output committed into `tests/`, two of
  them (`test_8080_baseline`, `test_commit_4c7bd4d`) with no source in the tree
  at all, and two of the sources had no binary. Nothing built or ran any of it:
  no make target, no `run_tests.sh` line. The sources had rotted past compiling
  — `qkz80` has taken a memory object in its constructor for a long time and
  `get_mem()` returns `qkz80_uint8*`, so all fifteen fail on their first two
  lines — and every one of them printed its result and returned 0 whatever the
  CPU did, so wiring them in as they stood would have added fifteen tests that
  could not fail. `tests/unit_8080.cc` covers what they were reaching for, with
  assertions and an exit status.
  `.gitignore` now names the binaries the current tests build — `unit_8080`,
  `pty_console`, `win_console` — so those cannot be committed by accident.

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
