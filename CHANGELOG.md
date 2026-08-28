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

- **A macOS job in `.github/workflows/release.yml`, which has never run on a
  GitHub runner.** `release.yml` built on `ubuntu-latest` and
  `ubuntu-24.04-arm` only, so there was no macOS download and nothing in CI
  ever compiled the platform layer's other half. `build-macos` is a job of its
  own rather than a third row in the matrix: every step in that matrix is
  Debian and RPM packaging — `apt-get`, `gem install fpm`, `fpm -t deb`, `fpm
  -t rpm` — and none of it has a macOS counterpart, so a matrix row would mean
  an `if:` on six steps plus a parallel set beside them. It builds the
  makefile targets first, because those are what someone building from source
  runs and their shared-library rules exist only on this platform, then one
  universal binary through cpack — `cpmemu-<version>-Darwin-arm64-x86_64.tar.gz`
  plus a versionless `cpmemu-macos-universal.tar.gz` so
  `/releases/latest/download/` URLs work, matching what the Linux job does for
  the `.deb` and the `.rpm`. The archive carries the binary, `libqkz80.a` and
  the headers and deliberately no dylib: a dylib's install name is an absolute
  path, so one shipped in a tarball the user unpacks wherever they like is a
  library dyld cannot find, and an `@rpath` layout is a bigger change than a
  release job. **Said plainly: every shell command in the job was extracted
  from the YAML and executed by hand on macOS 27 arm64, and each produced what
  the job expects — `lipo -info` reporting `x86_64 arm64`, both slices running,
  the archive holding `bin/cpmemu`, seven headers and `libqkz80.a`. The job
  itself has never run. `macos-latest` is a different machine with a different
  Xcode.** That is in `todo.txt`.

- **Five Windows console cases, and the two things the harness needed to reach
  them — none of which has ever been executed.** `tests/win_console.cc` could
  not name a character above `U+00FF`: `inject_spec()`'s single-character branch
  went through `VkKeyScanA`, which takes a byte, and cast one byte to `WCHAR`.
  It can now, as `U+XXXX`, and `U+XXXX+YYYY` sends several back to back with
  nothing between them — which is the shape the `0xE0` bug below needs, since
  the harness otherwise sleeps between keys the way a person types. A case can
  also set the console input code page, which nothing in `tests/` had ever done
  even though the code page is what decides the bytes a character becomes, and
  can send a console control event, which is the only way to reach ctrl+break at
  all; the emulator is started in a process group of its own for that, or the
  event would come back and kill the harness. **Said plainly: there is no
  Windows machine and no wine here. All of this cross-compiles under
  `x86_64-w64-mingw32-g++` with no warnings, under `-Wextra` as well as the
  `-Wall` `run_tests.sh` requires, and not one line of it has run.** The five
  cases have never reported anything. That is in `todo.txt`.
- **`tests/8080/8080pre.com` runs in the default suite.** The preliminary test
  that ships with the exerciser: a fixed sequence rather than an exhaustive
  walk, printing `8080 Preliminary tests complete` and nothing else when it
  passes. It takes 0.09s against the exerciser's 3m21s beside it, so there was
  nothing to weigh up about `--zex`. `run_tests.sh`'s `check` takes emulator
  options now, which is what `--8080` needed.
- **8080 mode is tested, and `tests/8080/8080exm.com` is wired in.** `--8080` is a
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
  `tests/8080/8080exm.com` — Ian Bartholomew's 8080 conversion of the same
  exerciser, in the tree and referenced by nothing since the first commit — now
  runs under `--zex` with `--8080`. It was right and the emulator was wrong: 19 of its 25
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
  guest programs when it was written, each printing what it was handed as hex,
  so a failure names the wrong byte instead of describing a missing behaviour.
  Twenty-five now: the five in the bullet above were added later in this same
  cycle and have never run anywhere. Sixteen of the original
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

- **A guest polling BDOS 6 against redirected input hung forever on POSIX.**
  `stdin_has_data()` in `src/os/linux/platform.cc` returned false outright for
  anything that was not a tty, on purpose, so BDOS 6, BDOS 11 and BIOS CONST
  never saw a pipe or a file. The consequence was not "batch runs are quieter":
  the read in `cpmemu.cc` was unreachable, `note_console_eof()` never counted,
  and the 1024-read give-up that ends such a run on Windows could not fire.
  Measured with the repo's own `con6hex` guest from `tests/con_guests.h`:
  `cpmemu con6hex.com < in.txt` and `printf 'AB.' | cpmemu con6hex.com` both ran
  until they were killed and printed not one byte of their own input; the same
  input through blocking BDOS 1 printed `41 42 2E` and exited at once. POSIX
  matches Windows now — `os/windows/platform.cc` has always answered for a pipe
  through `PeekNamedPipe` and for a file from its own position — and the two
  platforms no longer disagree about whether a guest that polls as a break
  check can take a byte of its own script. Both cases now print `41 42 2E` and
  exit 0 in 0.07s, and `< /dev/null` reaches
  `[Exiting: 1024 console reads past end of input]`, which nothing on POSIX
  could reach before. `select()` rather than `poll()`, and that was measured
  rather than chosen: on macOS 27 `poll()` sets `POLLNVAL` on `/dev/null` and
  `/dev/zero`, descriptors `fstat()` is perfectly happy with, so a `poll()`
  implementation would have reported "nothing waiting, ever" and hung exactly
  where this change is meant to stop hanging. `EINTR` is retried rather than
  read as "no character", since `SIGCONT` and `SIGWINCH` arrive here in
  ordinary use. Three `tests/pty_console.cc` cases cover it: the two that
  pinned the old behaviour used a *bounded* poller and so never hung, and were
  replaced with the same guest, input and expected bytes as the Windows case
  "polled input arrives from a file, not only from a pipe" so the two suites
  can be read side by side; a third, "a polled reader that runs out of input is
  stopped", covers the give-up path.
- **`make shared` on macOS wrote a Mach-O called `libqkz80.so`, with a bare
  install name, and a consumer that linked cleanly aborted at exec.** Not a
  cosmetic mismatch: Apple's linker does search for a `.so` and prefers it over
  the `libqkz80.a` sitting beside it in the same `-L` directory — traced with
  `-Wl,-t` — and the install name inside it was the bare string `libqkz80.so`,
  which dyld resolves only from the build directory it happened to be run in.
  So it looked fine where it was built and died everywhere else:
  `dyld: Library not loaded: libqkz80.so`, exit 134. It is
  `libqkz80.4.dylib` now with an unversioned `libqkz80.dylib` symlink beside
  it, laid out as every other dylib on the system is; `-install_name` is an
  absolute path, `-compatibility_version` is the major and
  `-current_version` the full version, and `-headerpad_max_install_names`
  leaves `install_name_tool` room. `make install-lib` re-stamps the install
  name for wherever `PREFIX` actually puts it, because the link happens before
  a `PREFIX` given only on the install command line is known. Measured: a
  consumer built from a different working directory against an installed
  prefix records that prefix and runs; installing the same build tree into two
  prefixes in a row gives each its own install name and its own `libdir`;
  `codesign -v` stays happy across the re-stamp; `make uninstall-lib` removes
  the symlink too. `make clean` removes `*.dylib`, which it did not. Linux is
  untouched — `make -n UNAME_S=Linux libs` still emits `c++ -shared -o
  libqkz80.so` verbatim, which is the only check the Linux half of any of this
  has had.
- **`make STATIC=1` on macOS produced a link error rather than a static
  binary, and is now refused rather than attempted.** The SDK ships no
  `crt0.o`, no `libc.a`, no `libSystem.a` and no `libc++.a`, so `-static`
  stopped at `ld: library 'crt0.o' not found` — measured on macOS 27 arm64 with
  Apple clang 21. Quietly dropping the flag would have been worse than
  failing, because `-static-libstdc++` *is* accepted and silently ignored by
  the same compiler, so whoever asked for `STATIC=1` would have got a
  dynamically linked binary they believed was static and found out from a
  user's machine. `src/makefile` stops with a `$(error)` naming
  `MACOSX_DEPLOYMENT_TARGET` as the thing that actually controls how old a
  macOS the binary will run on — measured rather than guessed:
  `make MACOSX_DEPLOYMENT_TARGET=12.0` gives `LC_BUILD_VERSION` `minos 12.0`
  against `minos 27.0` without it. It fires at parse time, so `make clean
  STATIC=1` stops as well; `release.yml`'s Linux job is the only caller and
  does not pass it on Darwin. `-static` on Linux is unchanged.
- **`qkz80.pc` named directories the install does not use, and produced a
  static link that could not work.** `libdir` and `includedir` were hardcoded
  as `${prefix}/lib` and `${prefix}/include/qkz80`, so they were wrong the
  moment `LIBDIR` or `INCLUDEDIR` moved; they come from the same variables the
  install uses now. There was no `Libs.private` at all, so `pkg-config --static
  --libs qkz80` handed a C driver a link with no C++ runtime under it —
  measured, `cc main.o shim.o libqkz80.a` failed on `vtable for
  __cxxabiv1::__class_type_info`, `operator new`/`delete` and
  `___gxx_personality_v0`. It is `-lc++` on Darwin and `-lstdc++` on Linux
  (`make -n UNAME_S=Linux`), only `--static` reads it, and the same C driver
  links and prints its answer now. The rule was also depending on
  `qkz80.pc.in` alone, so `make install-lib PREFIX=A` followed by `PREFIX=B`
  installed a stale `.pc` still pointing at A; it is `.PHONY` and regenerates.
  The version is no longer a third hand-maintained copy either — `qkz80.pc.in`
  takes `@VERSION@` from `LIB_VERSION` in the makefile. `src/CMakeLists.txt`
  still carries its own in `project(... VERSION ...)`; the makefile comment
  says so.
- **The pty suspend/resume case failed on Darwin, and it was the harness that
  was wrong.** "a suspend puts the terminal back, and a resume takes it again"
  reported "the terminal was not put back on exit" on every run — four out of
  four, three against the committed binary and one against a fresh build, so
  neither flaky nor stale. The emulator was fine: the suspend, the resume, the
  raw re-apply and the `41 42 2E` all passed and only the exit-time check
  failed. Darwin revokes a pty slave for every other descriptor the moment the
  session leader exits, and the stand-in shell these two cases build *is* the
  session leader, so by the time `run_suspend_case()` looked at the fd there
  was nothing there — instrumented, `tcgetattr` returned `-1`/`ENOTTY` and
  `isatty()` `0`, meaning the check could only ever have said "not put back"
  whatever the emulator did. The shell reads the terminal itself now, the
  moment `waitpid()` reports the emulator gone, and reports
  `PUTBACK`/`STRANDED`/`NOTTY` down the note pipe it already had. This is
  strictly tighter than what it replaced, on Linux too: it looks between the
  emulator's exit and the shell's own exit rather than after both, and nothing
  is skipped on any platform. Not vacuous — against a deliberately broken
  emulator with `atexit(disable_raw_mode)` commented out it fails, with 15 of
  33 cases failing in total. `tests/run_tests.sh` is 76 passed, 0 failed, 5
  skipped on this Mac, where it was 74 passed, 1 failed before.

- **`-Wshorten-64-to-32` stopped every GCC build**, `release.yml`'s
  `ubuntu-latest` and `ubuntu-24.04-arm` jobs included. It is a Clang-only
  option and GCC does not ignore an unknown `-W` flag, it fails the compile, so
  the build died on the first object file. `src/makefile` probes for it now and
  adds it only where it exists. Release-blocking: the flag and the breakage
  arrived in the same unreleased change, so nothing released carries it and
  nothing released would have built either.
- **A signal could leave a POSIX terminal raw for good, and nothing would put it
  back.** This is what a loaded run actually turned up while chasing the
  intermittent `tests/pty_console.cc` kill-case failures in `todo.txt` — an
  emulator bug, not a harness one, and not the mechanism that entry suspected.
  `apply_raw_mode()` sets `raw_active` *after* its `tcsetattr` returns, while
  the settings are live in the kernel from the moment the call completes there —
  and the restore was gated on that flag. A signal delivered in the gap
  found `raw_active` false, skipped the restore, and left the user's shell with
  no echo. The gap is a few instructions wide, which makes it rare rather than
  impossible: reproduced once in 120 runs of the seven kill cases in
  `tests/pty_console.cc` with 64 spinning processes on a 2-core box, on the
  `SIGQUIT` case, reported as "the terminal was not put back on exit". Widening
  the gap to 50ms on purpose makes it all seven, every time; with the restore no
  longer consulting `raw_active` it is all seven passing with the 50ms still
  there. `termios_saved` is the guard instead — it means "there is a terminal to
  put back", which is the question actually being asked, and restoring one that
  was never made raw writes back the settings already on it.
- **The Windows console took a character for a special key prefix.**
  `read_one_key()` read one byte at a time through `_getch()` and treated `0x00`
  and `0xE0` as the prefix of a two-byte special key, with `!_kbhit()` as the
  tie-breaker. `0xE0` is also an ordinary character: in code page 437 it is
  alpha, and in 65001 it leads every character from `U+0800` to `U+FFFF`.
  Measured on a real console before the change and recorded in `todo.txt`, code
  page 437: alpha alone reached the guest as `60` after the usual 7-bit mask,
  but alpha immediately followed by `A` gave
  `E0 41`, the `41` was eaten as a scan code, no table entry matched it, and the
  guest got **neither** character. The input path reads `INPUT_RECORD`s with
  `ReadConsoleInputW` now, which removes the guess rather than narrowing it, and
  with it come the five changes it forces: only key presses are looked at, a
  `wRepeatCount` above 1 is replayed a press at a time rather than expanded, the
  `WCHAR` is encoded in the console input code page (so a surrogate pair, which
  arrives as two records, becomes one character), `extended_keys[]` is keyed on
  the virtual key code plus a ctrl flag instead of CRT scan codes, and the
  two-slot queue is eight. Every key the table already handled is unchanged,
  Ctrl+Left/Right and the Ctrl+Up/Ctrl+Down added at 0b8dc2b included. One thing
  the key-press filter would otherwise have lost is kept deliberately: alt plus
  a numeric keypad code point is delivered by the console on the *alt release*,
  and that release is let through when it carries a character. **Cross-compiles
  clean; never executed.**
- **ctrl+break left the Windows console in raw mode**, and there was no
  `SetConsoleCtrlHandler` anywhere in `src/`. Clearing `ENABLE_PROCESSED_INPUT`
  keeps `^C` away from the handler — it becomes an ordinary `03` for the guest,
  which is what WordStar wants — but ctrl+break is not gated on that bit, and
  neither is the console window closing, a logoff or a shutdown. The default
  handler ends the process without running `atexit`, so `disable_raw_mode()`
  never ran: measured, the child exited `0xC000013A` and the console mode stayed
  at `0x1A0` instead of returning to `0x1F7`, leaving the shell with no echo.
  There is a handler now, on the same five events, and it returns `FALSE` so the
  process still dies of what it was sent and the exit code a caller sees is
  unchanged — the same reason the POSIX handler re-raises with the default
  disposition rather than exiting tidily. **Cross-compiles clean; never
  executed.**
- **The pty console harness gave one timeout budget to two different waits.**
  `todo.txt` recorded this as the suspect behind the intermittent kill-case
  failures and flagged it as a guess rather than a measurement. It is real, and
  it is one of the two mechanisms — the other being the `raw_active` race above,
  which is the one a loaded run here reproduced. `run_case()` spun until
  `tcgetattr` showed `ICANON` clear and then waited for the child to exit, both
  against the same 10s measured from one `start`, so a slow start ate the wait
  and the case reported a hang that never happened. Confirmed by making
  the start slow on purpose rather than waiting for a loaded machine to do it:
  with a wrapper that sleeps before it execs the emulator, and the emulator
  itself untouched and healthy, six of the seven kill cases went 6 pass at 0s and
  at 5s, 3 pass 3 fail at 9s, and 2 pass 4 fail at 9.9s and 9.95s, every failure
  reading "the emulator had to be killed: it never finished". The exit wait has a
  budget of its own now, and all six pass at every one of those delays. Which six
  is not recorded: the file holds seven - one plain kill and six signals - and the
  run above reported six. How much the
  exit needs is not uniform either, which is the other half of why one budget was
  tight: four of the seven signals dump core, so the reap waits on whatever the
  host does with one — measured here, unloaded, with `core_pattern` piping to
  apport, `SIGQUIT` 445ms, `SIGSEGV` 1385ms, `SIGBUS` 1327ms and `SIGABRT`
  1392ms, against `SIGTERM` 11ms, `SIGHUP` 3ms and `SIGINT` 3ms. Separating the
  budgets uncovers a way for a signal case to pass having proved nothing — never
  reach raw mode, and the terminal it compares is the one it started with — so a
  signal case is now held to having been observed in raw mode, and says so when
  it was not.
- **`cpack` named every non-Windows archive with no architecture.**
  `src/CMakeLists.txt` never set `CPACK_PACKAGE_FILE_NAME`, so the default
  `name-version-system` applied and a linux amd64 build and a linux arm64 build
  were both `cpmemu-4.6.0-Linux.tar.gz`: two different binaries under one
  filename, so whichever is attached to a release second replaces the first and
  someone downloads a binary that cannot run. Verified with `cpack` here — the
  same tree produced `cpmemu-4.6.0-Linux.tar.gz` before and
  `cpmemu-4.6.0-Linux-x86_64.tar.gz` after, and a toolchain file naming
  `aarch64` gives `cpmemu-4.6.0-Linux-aarch64`, which was read out of
  `CPackConfig.cmake` rather than built. Nothing in CI publishes a cpack archive
  today, so this breaks nothing and stops being a trap for whoever adds a job
  that does. The rest of the macOS release path — `STATIC=1`, the shared
  library, the install name and a job to build any of it — is fixed above,
  later in the same cycle.
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
  the pre-fix binary had to be killed after 15 seconds. This closed the tty and
  Windows cases only; the POSIX redirected cases could not reach the read at
  all until `stdin_has_data()` was changed, above, later in the same cycle.
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

- **Two byte-identical copies of the 8080 exerciser.** It was in the tree three
  times over and only one copy was run. `tests/8080/` is the canonical one: a
  verbatim copy of superzazu's `cpu_tests`, each `.com` beside the `.mac` it was
  assembled from and a `.prn` listing, with a README naming where they came
  from. `tests/8080EXM.COM`, which `--zex` used to run, was byte for byte
  `tests/8080/8080exm.com`; `tests/8080exer.com` was byte for byte
  `tests/8080/8080exer.com` and was run by nothing. Both are gone and `--zex`
  runs the copy in `tests/8080/` — 25 groups, no CRC mismatches, run at the new
  path to check the move, in 3m21s on a machine slower than the one the 3m41s in
  `tests/README.md` came from. `tests/8080/8080exer.com` stays and is still run by nothing,
  which is a decision rather than an oversight: it is the same 25 groups as
  `8080exm` and takes the same minutes, and where `8080exm` prints the CRC it
  computed, `8080exer` prints `OK`, so a failing group tells you less and a
  passing run tells you nothing `8080exm` did not. Deleting it would leave its
  `.mac` and `.prn` describing a program that is not there. `tests/README.md`
  says all of this.
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

- **macOS is documented as a platform you can install and build on.** `README.md`
  had no macOS install section at all, and now names the universal archive, the
  `xattr -dr com.apple.quarantine` line and — this is the part worth writing
  down — what happens when you skip it. The binary is signed ad hoc and is not
  notarized, so Gatekeeper refuses it, and from a terminal that refusal has no
  visible form: measured on macOS 27, the process starts, prints nothing and
  sits in state `SN` until it is killed, where the same binary with the
  attribute removed runs immediately. `curl -L` tags the download and macOS
  `tar` copies the tag onto every file it extracts, so it lands on `bin/cpmemu`
  as well as on the archive. `docs/BUILDING.md` gains the makefile and CMake
  routes: why `STATIC=1` is refused and what to use instead, what `make shared`
  produces and why it is not a `.so`, and the universal-binary cpack
  invocation. Notarizing properly needs a Developer ID certificate and
  `notarytool`; that is open in `todo.txt`.
- **`todo.txt` is open items only again, and the checks that need a person are
  a file of their own.** It had grown into an account of work already finished
  — three of its six entries opened or closed with a paragraph explaining what
  had landed and pointing at this file — and roughly two lines in three asked
  nobody to do anything. It is 86 lines down to 46, every surviving item tagged
  with what a machine has to be to pick it up (`[MAC]`, `[WINDOWS]`,
  `[DECISION]`, `[RELEASE]`), and the four-terminal keyboard pass moved to a
  new `MANUAL_CHECKS.md` as a checklist with the expected bytes in it. Two
  numbers in it were wrong and had been copied out of source comments rather
  than measured: the byte string the guest gets after the `& 0x7F` (see below),
  and a claim that two POSIX cases and a Windows one ran "the same guest" —
  they ran different guests with different expectations, and that error was
  duplicated in `tests/pty_console.cc`, where the comment has been rewritten
  with the cases it describes.
- **The masked-byte measurement in `src/os/linux/platform.cc` was arithmetically
  impossible.** The comment recorded the guest receiving `C3 29 4E 31 42 23 62
  00 14`, but `0xC3 & 0x7F` is `0x43` and no byte at or above `0x80` survives
  the mask at all. Re-measured with `con1hex` against the UTF-8 for e-acute,
  alpha, pound and em-dash: `43 29 4E 31 42 23 62 00 14`. The `ISTRIP` entry
  further down this section quoted the same wrong string out of that comment
  and is corrected too.
- **`src/CMakeLists.txt` no longer says "there is no Mac here".** The comment
  on the cpack archive-naming branch said it was written from the documentation
  and never measured. It is measured now: a plain build names the archive
  `cpmemu-4.6.0-Darwin-arm64.tar.gz` and
  `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` names it
  `cpmemu-4.6.0-Darwin-arm64-x86_64.tar.gz`, with `lipo -info` confirming both
  slices.

- **The `ISTRIP` comment and the four `& 0x7F` read sites contradicted each
  other, and now they do not.** The comment in `src/os/linux/platform.cc` said
  the 8th bit survives and that clearing `ISTRIP` "is all we need". The first
  half is true and measured — 128 of 128 high bytes reach `read()` unchanged —
  but the second half is not: `cpmemu.cc:1591` (BDOS 1), `:1771` (BDOS 10 buffer
  store), `:2361` (BDOS 6) and `:2799` (BIOS CONIN) all mask with `& 0x7F`, so a
  high byte arrives at the guest with its top bit gone. The comment now says
  what the clear does — keeps the bit through the line discipline — names the
  four sites that then take it off, and records what the guest actually gets:
  the UTF-8 bytes for e-acute, alpha, pound and em-dash come out as
  `43 29 4E 31 42 23 62 00 14`, and on the polled path a byte that masks to
  `0x00` is dropped entirely because BDOS 6 reads 0 as "no character". **Only
  the prose changed. Whether CP/M should see eight bits is a real decision and
  nothing here makes it**; it is stated in `todo.txt` for whoever does.
- **README records that three other emulators compile `src/qkz80*` out of this
  working tree.** Related Projects listed them; what it did not say is that they
  do not depend on a cpmemu *release*, so an edit to `qkz80.cc` lands in all
  three on their next build with no notification. The mechanisms differ and the
  new section says which is which: ioscpm has 11 symlinks in `iOSCPM/Core/`
  pointing at `../cpmemu/src/qkz80*` and builds them as Objective-C++ for iOS at
  `c++17`/`gnu++20`; z80cpmw's `.vcxproj` compiles
  `$(SolutionDir)..\cpmemu\src\qkz80*` in place under MSVC at `/W3`;
  romwbw_emu's `src/makefile` falls back to `../../cpmemu/src` when pkg-config
  has no `qkz80` entry. Only romwbw_emu has a version gate and only in CI, where
  `release.yml` and `test.yml` check out a pinned `CPMEMU_REF` — `9a94e8d` as
  this is written. `todo.txt` had recorded that clone as unpinned; it is not any
  more, and the entry it came from is closed. 06262ff is the worked example of
  the mechanism running quietly in the good direction: it added
  `QKZ80_NO_TRACE` and all three picked it up without being told.
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
