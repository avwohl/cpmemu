# Changelog

All notable changes to **cpmemu** are documented here.

This file starts at `v4.7.0`; `v4.6.0` is the tag it measures from, not an
entry here. For most of this project's life the record of a
change has been the commit message that made it, and those messages are longer
and more specific than any changelog entry — they carry the measurements, the
counter-examples and the things that were deliberately *not* done. This file
summarises and points; `git log` is the detail. Open work is in
[`todo.txt`](todo.txt).

## [4.8.0] - 2026-09-03

`todo.txt` is empty. It had three items; two are answered here and the third —
a person pressing keys on four terminal programs — was never a `todo.txt` item
at all and is in [`MANUAL_CHECKS.md`](MANUAL_CHECKS.md), where that file's own
header always said it belonged.

The change that made the rest possible is the first one: there was no CI job
running any test, on any platform, so nothing in this tree had ever been checked
by a machine other than the one the change was written on. There is one now, and
it found two bugs within the hour — one in code nobody had ever executed and one
in code everybody had.

The process rules that used to sit in `todo.txt`'s header — what belongs there,
what belongs in `CHANGELOG.md`, what belongs in `MANUAL_CHECKS.md`, the tag
convention, the commit style, the qkz80 sibling-project trap — are now in
`CLAUDE.md`. They were rules, not work, and they were the reason the file kept
growing while the list of open items did not shrink.

### Added

- **A test workflow: `.github/workflows/ci.yml`.** `release.yml` builds and
  packages and runs no test suite, so this is the first CI that runs anything.
  `ubuntu-latest` and `macos-latest` run `tests/run_tests.sh`; `windows-latest`
  builds `cpmemu.exe` with MSVC and runs `tests\win_console.bat`. The
  exercisers are a `workflow_dispatch` job. That job had never been run at all
  until it was dispatched here: **105 passed, 0 failed, 1 skipped** — the whole
  default suite plus zexdoc, zexall and 8080exm, 159 instruction groups between
  them with no CRC mismatches. The skip is the Windows console harness, one of
  the two platform skips `--require` exempts; quoting the line without it would
  be the very thing this release is about. It took 6m23s on an ubuntu runner rather than the
  about-18-minutes measured on the machine `run_tests.sh` was written on.

- **The Windows console cases ran, for the first time anywhere: 28 passed, 0
  failed.** `tests/win_console.cc` had been written, shipped and carried through
  three releases without one of its cases reporting a verdict. Twenty-five
  of the twenty-eight existed already; three are new here. All five that
  `todo.txt` singled out pass — the four that set the console input code page
  (an `E0` character under 437, two of them in a row, a three-byte UTF-8
  character under 65001, and a surrogate pair) and the one that sends a
  `CTRL_BREAK_EVENT` and checks the console mode is put back. The seven-bit
  expectations they encode are confirmed rather than merely asserted.

  The reason they had never run is worth recording, because it is not "nobody
  had a Windows machine". `tests\win_console.bat` named one absolute path —
  `...\Visual Studio\18\Community` — and printed `SKIP` when it was not there.
  The only machine that could ever run these is a CI runner, and the
  `windows-latest` image is `windows-2025-vs2026`, which carries Visual Studio
  Enterprise 2026 at `...\Visual Studio\18\Enterprise` and no other. The same
  version directory, a different edition — which is why the fix is `vswhere.exe`
  and not a bumped version number in the path. The one machine that could do the
  job was the one guaranteed to decline it, and a skip exits 0. Both that file and
  `src/do_build.bat` now ask `vswhere.exe`, which ships with every Visual Studio
  installer since 2017.

- **`tests/run_tests.sh --require`**, and `CPMEMU_REQUIRE_ALL=1` for the same
  thing. A skip for want of a tool is a failure under it, named, with what to
  install. The two platform skips are exempt, because no install makes a pty
  harness run on Windows, and so are the opt-in exercisers.

- **`CLAUDE.md`**, and **`docs/macos-signing.md`**.

### Fixed

- **A polled guest hung at the end of redirected input on Windows.** The POSIX
  side had fixed this and written down why; the Windows side still had it, in
  all three redirected shapes, and none of the 25 cases could see it.
  `stdin_has_data()` answered "a byte will come back" rather than "a read will
  not block": a regular file at EOF returned `pos < len`, and a pipe whose
  writer has closed and `NUL` both fail `PeekNamedPipe`, which was read as "no
  input". BDOS 6, BDOS 11 and BIOS CONST all gate their read on that call, so
  the read was never attempted, `note_console_eof()` never counted, and the
  1024-read give-up could never fire. The guest polled until something killed
  it.

  Measured rather than argued: with the old function put back on a branch of its
  own, the new case times out on the runner — `the emulator had to be killed: it
  never finished`, with the other 25 still passing — and with the fix it passes.

  A regular file is now readable whether or not anything is left, an open empty
  pipe is not, and a `PeekNamedPipe` failure of `ERROR_BROKEN_PIPE` is, which is
  what POSIX `select()` says of a pipe with no writer. A character device is not
  one answer but two: `NUL` reads 0 at once and is readable, while a serial line
  with the default `COMMTIMEOUTS` — every field zero, meaning wait forever —
  would block, and blocking is what BDOS 6's polled form and BIOS CONST promise
  never to do. `GetCommState` tells them apart and `ClearCommError` answers for
  the comm port without reading from it, so `cpmemu prog.com < COM1` does not
  trade one hang for another. A handle `GetFileType` cannot classify at all,
  which is what a service or a `DETACHED_PROCESS` is handed, is readable too: a
  read on it fails at once, and that is end of input.

  `tests/win_console.cc` can now give a guest a pipe — the write end is filled
  and closed before the emulator starts — because the `ERROR_BROKEN_PIPE` branch
  had no test on any platform. "polled input arrives from a pipe too" and "a
  polled reader whose pipe is closed is stopped" are the two that reach it.

- **A CP/M `DIR` depended on the host filesystem.** `platform::list_directory()`
  returned `readdir(3)` order on POSIX and `FindFirstFile` order on Windows.
  Neither is promised: ext4 hands back hash order, and NTFS sorts, so the same
  drive directory listed differently on Linux and Windows and differently on two
  Linux machines. Both platforms now sort by name in byte order, and
  `os/platform.h` states it as a contract.

  Found by CI on its first run of the 42 assembler-gated checks: "drive: search
  scopes to the drive" passed on the machine it was written on and failed on a
  GitHub ubuntu runner, because the test was asserting an order nothing
  guaranteed. Its two files are now created in reverse-alphabetical order, so
  the check can tell a sort from a filesystem returning creation order — it
  could not before.

- **Two Windows raw-mode guards the POSIX side has and this one did not.**
  `enable_raw_mode()` ignored what `GetConsoleMode()` returned and claimed a
  mode to restore anyway, so a failure would have written mode `0x0080` to the
  console on exit — no line input, no echo, and a shell to reset by hand.
  `disable_raw_mode()` cleared its "there is something to put back" flag after
  restoring, which on Windows is raced by design: `atexit()` runs it on the main
  thread and the console control handler runs it on a thread the OS makes.
  Neither is reachable by any case, which is why the first run passed without
  finding them.

### Documentation

- **CI ran 60 of 102 checks and reported success.** The first version of
  `ci.yml` did not install an assembler, so the 42 checks behind that gate
  skipped and the job was green. That is the failure this workflow exists to
  prevent, so it is fixed twice: the tools are installed *and* `--require` says
  a missing one is a failure, because an install that quietly stops working
  would otherwise be absorbed by a skip all over again.

  `--require` then had to be taught about three more places it could not see.
  `CPMEMU_SKIP_OK` allows a named skip through, which is how the macOS job
  requires everything except the mingw cross-compile — installing a Windows
  cross-compiler on a Mac to repeat a check the ubuntu job already does buys
  nothing, and running that job without `--require` at all would have let a
  broken `brew install` hide 42 checks. Worse, `--require` was blind to the two
  sub-harnesses: `pty_console.cc` skips its whole run when no pty can be opened,
  and folding its counts in without registering the skip meant 42 checks — the
  entire reason the macOS job exists — could vanish under a green tick.
  Measured by forcing that branch: "60 passed, 0 failed, 5 skipped", exit 0.
  And `--require` never handed the rule down to `tests\win_console.bat`, so the
  whole `CPMEMU_REQUIRE_MSVC` chain was reachable only from `ci.yml`; from
  `run_tests.sh` on a Windows box with no Visual Studio it skipped and exited 0
  having run none of the console cases.

- **macOS notarization is written, gated, and has never run.** `release.yml`
  imports a Developer ID certificate, signs with `--options runtime
  --timestamp`, and submits to `notarytool --wait`, all behind two job-level
  gates that are false today because the secrets do not exist — so the job runs
  exactly as before. `docs/macos-signing.md` lists the five secrets and how to
  produce each.

  What remains is not a purchase. This entry first said it was, which was
  wrong: the project ships an app through the App Store and uses TestFlight,
  neither of which is possible without a paid Apple Developer Program
  membership, so the membership has been there all along. What is genuinely
  missing is a **Developer ID Application** certificate, which is a different
  certificate from the Apple Distribution one that signs store and TestFlight
  builds — the store signs its own copy and runs equivalent checks of its own,
  and none of that carries over to a tarball distributed outside it. The same membership covers it at no
  extra cost; it has to be created in the account, exported with its private
  key, and then the signing path has to be run for the first time.
  `MANUAL_CHECKS.md` carries it, because it needs a person on a Mac signed in
  to that account — and one step needs a role only a person holds. Three traps
  are written down there and in `docs/macos-signing.md` rather than left to be
  hit: only the **Account Holder** can create a Developer ID certificate, which
  Admin cannot and which Apple's own overview page misstates; there is a budget
  of five per team with no self-service way past it; and the App Store Connect
  API key must be a **Team** key, because Apple does not permit Individual keys
  to use `notarytool` at all.

  A step after packaging checks the archived binary's `CDHash` against the
  signed one, because `cpack` re-runs the CMake install rule rather than copying
  the file, and a rewrite would invalidate the signature and orphan the ticket
  silently.

  The honest limit is written down rather than discovered later: a notarization
  ticket **cannot be stapled to a bare Mach-O executable** — `stapler` takes
  disk images, flat packages and bundles — so this release would be notarized
  and unstapled, and Gatekeeper would look the ticket up online. That covers
  every user except one downloading through a browser while offline. Covering
  that one means shipping a `.pkg` or `.dmg`, which is a decision about what the
  download is.

- **macOS `tar` does propagate `com.apple.quarantine`, and Apple's
  documentation says it does not.** `README.md` has claimed the propagation for
  some time and a user's install instructions rest on it, while Apple's
  Developer Technical Support states, in the pinned forum answer on trusted
  execution, that "Unix-y unarchiving tools, like `tar` and `unzip`, don't
  propagate quarantine to the unarchived files". Measured on macOS 26.5.2, on a
  runner, `tar` does propagate it: an attribute written onto a `.tar.gz` after
  the archive was built lands on the file extracted from it. The README is
  right; the note now says so and says what was measured.

  A second case was measured in the same step — an attribute present on a file
  when it is archived survives into the extracted copy — and it is *not* a
  second contradiction of Apple. That one is libarchive storing and restoring an
  extended attribute across a round trip, which is a different mechanism from
  the propagation Apple's sentence denies. The workflow comment anticipated as
  much before it ran. Two measurements, one of which bears on the claim.

## [4.7.2] - 2026-09-01

Two open items from [`todo.txt`](todo.txt), which is down from five items to
three. Two of the `todo.txt` pointers below are answered here rather than in
`todo.txt`, where the items no longer are — the SECTRAN bug under v4.7.1 and the
eight-bit decision under v4.7.0. The other pointers in this file are still open
work. `src/cpmemu.cc` moves by 37 lines added and 1 removed, of which 12 are the
fix, 24 are the comment explaining it and one is blank; the removed line is the
`case BIOS_SECTRAN:` label leaving the stub group, not deleted logic. The tests
are the bulk of the change.

### Fixed

- **BIOS SECTRAN did not set HL, and clobbered A.** It is documented to take BC
  as the logical sector and DE as the translate table and to return the physical
  sector in HL, but it shared the `CPM_BIOS_DISK` stub group with
  HOME/SETTRK/SETSEC/SETDMA/READ/WRITE, which sets only A — so it had both
  halves of its contract backwards. Measured before the fix, with a guest that
  loads HL with `DEAD`, sets BC = 0005 and DE = 0 and calls `0FE30h`: HL came
  back `DEAD` where a real BIOS answers `0005`, and A came back `00` under `ok`
  and `01` under `fail`, overwriting a `5A` the guest had put there. It now
  answers HL = BC when DE is 0, else the byte at DE+BC with H = 0, and does not
  touch A. Nothing called it, which is why it went unnoticed since before the
  changelog; `tests/sectran.asm` is the first caller in the tree's history.

  It also leaves the stub group, and that is a behaviour change worth reading
  twice: SECTRAN is arithmetic over guest memory and reaches no media, so there
  is no failure for `CPM_BIOS_DISK=fail` to report and nothing unimplemented for
  `=error` to refuse. All three modes now give the same answer, and `=error` no
  longer ends the run on a table lookup — measured, it used to exit 1 printing
  `FATAL: Unimplemented BIOS disk function at offset 48` before the guest got
  HL back at all. Anyone using `=error` as a blanket tripwire for "the guest
  touched the BIOS disk entries" loses SECTRAN from that net. The group still
  stops a guest at the first call that would actually touch a disk.

### Added

- **`tests/sectran.asm` and seven checks, none of which existed.** The guest
  calls SECTRAN through the jump table and prints what it answered; the command
  tail picks the case. It is assembled at test time by both `pasmo` and
  `z80asm`, byte-identical under each, so the assembler gate goes from 35 checks
  to 42. Two of the seven are there because the other five were not enough: `W`
  carries DE+BC past `FFFF`, and `A` prints the accumulator rather than HL.
- **Nine cases in `tests/pty_console.cc` pinning seven-bit console input.**
  Five cover the mask — one per console read site, plus a pipe twin of the
  BDOS 1 case, because a pty case alone cannot tell this emulator's mask from a
  line discipline that strips the bit. Measured: an emulator built with `ISTRIP`
  set *and* the BDOS 1 mask deleted still passes the pty case and fails only the
  pipe twin. Four cover `check_ctrl_c_exit`, which is handed the raw byte on
  purpose so that a `0x83` counts as a high byte and not as a `^C`; it has four
  call sites, the same mistake could be made at any one of them, and none was
  covered.

  Before these, nothing that runs asserted any of it. The only tests pinning the
  masks were the four code-page cases in `tests/win_console.cc`, and by
  `todo.txt`'s account not one of them has ever reported anything on any
  machine.

  The suite goes from 86 checks to 102, none failing.

- **The checks were mutation-tested, and that is what shaped them.** A first
  round of five SECTRAN checks and five seven-bit cases left five real
  regressions undetected: SECTRAN clobbering A, dropping the cast that keeps
  DE+BC inside the 64K array, the BDOS 10 line editor not masking what it
  stores, and `check_ctrl_c_exit` fed the masked byte at BDOS 1 and again at
  BIOS CONIN. All five applied at once built warning-clean under
  `-Wall -Wextra` and the suite still reported 96 passed, 0 failed. The `W` and
  `A` tails, the line-editor case and three of the four `^C` cases exist because
  of that run; the same five-way mutant now fails six checks. Each check was
  then confirmed to fail for its own reason rather than for a neighbour's.

### Documentation

- **Whether a CP/M guest should see eight bits is settled: it sees seven.** The
  four `& 0x7F` console masks stay. No code changed for this; what changed is
  that the answer is written down with what it costs, and that something now
  tests it. README's Known Limitations carries the measurements: the byte that
  masks to zero is consumed rather than delivered, so `80 41` at a BDOS 6 guest
  yields `41` alone; two byte values do this, `0x00` and `0x80`, where real CP/M
  loses only the first; BIOS CONST and BDOS 6 contradict each other on a typed
  `0x80`, status saying ready and the read saying no character; and the mask is
  applied after every raw-byte test, so a raw `0x8D` is stored as CR rather than
  ending a line early and a raw `0xFF` is stored as `0x7F` rather than acting as
  rubout.

  The count in that section moves too. The item said four read sites; there are
  six — `bdos_aux_input` (BDOS 3) and `bios_reader` mask identically. Those two
  are the Reader device, they are documented, and they are still asserted by
  nothing.

- **`docs/cpm_disk_formats.md` gains a SECTRAN detail entry** in the same
  per-function list as SELDSK, the other HL-returning BIOS call, including that
  this emulator's own DPH sets XLT = 0 so a guest driving it through SELDSK
  takes the no-translation path.

- **README's BIOS section and `CPM_BIOS_DISK` row follow the code.** SECTRAN is
  out of the stub list and into a bullet of its own, "Of the seven" is now "Of
  the six", and the env-var table row names the six calls the modes still cover.
  Anyone who relies on `=error` will find the carve-out there as well as here.

- **`tests/README.md` describes the new cases** and gains a `sectran.asm` entry.
  The gate count moved from 35 to 42 in all five places it is written — the
  `run_tests.sh` comment and its `skipped=$((skipped + 42))`, `README.md`, and
  `tests/README.md` twice — of which only the second is the source. Keeping
  those in step is what the last bullet of the v4.7.1 entry below is about.

- **Stale `cpmemu.cc:NNNN` citations are now function names.** Five references
  naming four distinct lines — four in `src/os/linux/platform.cc` and one in
  `tests/win_console.cc` — had drifted onto a `get_reg16` call, a bare brace, a
  blank line and an unrelated `fprintf`. Function names cannot rot the same way,
  which is the point.

## [4.7.1] - 2026-09-01

Two bugs the v4.7.0 README audit turned up. Both were found by reading the
source in order to document it, neither had a test, and both predate the
changelog. `src/cpmemu.cc` moves by 27 lines added and 9 removed, most of that
a comment and a five-line helper; the tests are the point.

### Fixed

- **`--save-memory` wrote nothing when the program exited through BDOS 0,
  which is how most CP/M programs end.** Three things are a CP/M program
  finishing — BDOS 0 System Reset, BIOS WBOOT, and a jump to `0x0000` — and
  each called `exit(0)` from wherever it was noticed. Only the jump called
  `do_save_memory()` first. So the flag worked for a guest that ended with
  `jp 0` and silently produced no file for one that ended with `ld c,0` /
  `call 5`, with nothing on stderr either way. That is the case the flag exists
  for: MOVCPM and SYSGEN are the two programs named in its own help text and
  both exit through the BDOS. Measured before the fix, with a guest that writes
  `A5 5A` at `0200h`: `--save-memory` plus `jp 0` wrote the image, the same
  guest plus BDOS 0 wrote no file at all, and BIOS WBOOT wrote none either. The
  three paths now go through one `program_exit()` that prints, saves and exits.
  `--save-range` works from all three, which it could not before because two of
  them never reached the writer. A fourth exit turned up while this was being
  checked and is fixed with it: the nine-billion-instruction watchdog at the
  bottom of `main()` returned 0 without writing an image or saying it had not,
  which is the one place a post-mortem is worth most. The suite cannot reach
  that one - the limit is about eight minutes of running - so it is the only
  part of this release checked by reading rather than by running.
- **`CPM_BIOS_DISK=fail` was byte for byte identical to `CPM_BIOS_DISK=ok`.**
  Both branches ran `set_reg8(0x00, reg_A)`; only the debug line and the
  startup message differed, so the emulator announced "BIOS disk functions will
  return failure" and then handed the guest the success code. A guest could not
  tell the two modes apart, which made one of the three documented settings
  unreachable in practice. `fail` returns A = 1 now, the CP/M BIOS permanent
  error. Of the seven calls in that group only READ and WRITE return a status
  in A — HOME, SETTRK, SETSEC and SETDMA return nothing — so the byte is simply
  unread for the others, which is why the whole group can share one value.
  SECTRAN is the exception worth naming: it is documented to return the
  physical sector in HL, and this emulator sets only A and leaves HL as the
  guest left it. Measured with a guest that puts `DEAD` in HL and calls SECTRAN
  with BC = 0005 and DE = 0: HL comes back `DEAD` under both `ok` and `fail`,
  where a real BIOS answers 0005. (Under `error` the emulator exits inside the
  call, so the guest never gets HL back at all.) That is untouched here and is
  now an open item in
  `todo.txt` — it is a separate bug from either of these two, and A = 1 neither
  helps nor hurts it. `error` is unchanged and was always the only visible mode.

### Added

- **Nine checks, behind the existing assembler gate, and two guests to reach
  them.** `tests/savemem.asm` writes a marker and then finishes the way its
  command tail asks — `0`, `W` or `J` — so one guest covers all three exits;
  the suite runs it under `--save-memory --save-range=0200-0201` and compares
  the two bytes that come back, which a zero-length or truncated write would
  not survive. `tests/bios_disk.asm` calls HOME, READ or WRITE through the jump
  table and prints the status byte as hex, covering `ok`, `fail` for all three
  calls, and that `error` exits non-zero *with its diagnostic* — a bare
  non-zero exit is also what a failure to start looks like.

  Each check was then made to fail on purpose, which is the part worth
  recording, because the first draft of three of them could not. `savemem.asm`
  reached BDOS 0 with a `call`, so a build in which BDOS 0 no longer terminated
  the guest simply fell through into the WBOOT branch below it, saved the image
  there, and the check named for BDOS 0 printed PASS. The jump case had the
  same hole from the other side: `0000h` holds the `JP WBOOT` the emulator
  writes there, so an untrapped jump lands in WBOOT and saves anyway — and that
  one cannot be closed from inside the guest at all. The `call` is a `jp` now
  and every case greps stderr for the exit it is named after. Re-checked by
  building three mutants, each with one of the three paths removed: each fails
  exactly its own check and nothing else, where before two of them passed
  against a build with the path deleted outright. Reverting `fail` to A = 0
  fails its three checks and no others. The gate's skip count moves from 26
  to 35.

### Documentation

- `README.md` had documented both bugs as behaviour, since at the time they
  were. The `--save-memory` row no longer carries the BDOS 0 caveat, the
  `CPM_BIOS_DISK` row distinguishes the modes, and the BIOS list says which of
  the seven calls actually return a status. The ctrl+break paragraph 180 lines
  further down had its own copy of the old list and is fixed too - it now
  points at the option row rather than re-enumerating it, which is what let the
  two drift apart in the first place.
- The gate's check count is stated in four places and only one of them is the
  source. `README.md`, `tests/README.md` twice and the `run_tests.sh` comment
  all said 26; they say 35. `tests/README.md` also called the gated group "more
  than half the suite", which it was not at 26 and is not at 35 - it is about
  two fifths - and its list of sources assembled at test time had never gained
  `cli_tail.asm`, let alone the two added here. The comment that counted the
  assembled guests has had its number removed rather than corrected: it was
  wrong the last two times a guest was added.

## [4.7.0] - 2026-09-01

Everything below landed after the `v4.6.0` tag, which was published five months
earlier on 2026-03-29. This is the first release to carry a macOS archive; the
`.deb` and the `.rpm` are built as they were.

### Added

- **A macOS job in `.github/workflows/release.yml`.** `release.yml` built on
  `ubuntu-latest` and
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
  release job. It was written by extracting every shell command from the YAML
  and running it by hand on macOS 27 arm64 — `lipo -info` reporting `x86_64
  arm64`, both slices running, the archive holding `bin/cpmemu`, seven headers
  and `libqkz80.a` — and then **the job ran for real on `macos-latest` in run
  33137416510 and succeeded**, alongside both existing Linux jobs. So the
  platform layer's other half is compiled in CI now, on a machine that is not
  the one this was written on.

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

- **`README.md` was audited section by section against the source and rewritten
  where the two disagreed — 485 lines to 796.** Five months of changes had gone
  in without it, and the audit found more than the missing features: the memory
  map reserved `0xFC00` for a CCP that does not exist and gave the TPA an end
  address a page below where the loader actually stops, the first line of the
  file promised "comprehensive BDOS/BIOS support" over a BIOS whose disk calls
  are stubs, the BDOS table called three acknowledged no-ops (BDOS 28, 29, 30)
  Supported and stopped at 40 when 48 is implemented, the BIOS list predated
  SELDSK answering for every drive letter, the ^C hatch claimed "there is no
  other way out" where Windows ctrl+break is one, and the sample config's
  `*.BAS = ${HOME}/basic text` line names a directory on the host side — a form
  that opens and then reads EOF forever. Newly documented rather than newly
  corrected: `drive_A`..`drive_P`, the ADM-3A to ANSI translator with its
  sequence table (it was in the emulator from the start and in the README
  never), the qkz80 library and its `pkg-config` entry, suspend and signal
  handling, that console input is masked to seven bits, and that `--save-memory`
  writes nothing when the guest exits through BDOS 0 — which is how most CP/M
  programs end. Every claim added or changed was checked against the source and,
  where it could be, run: the library consumer example compiles under
  `-Wall -Wextra` and runs, `-DQKZ80_NO_TRACE` takes `qkz80.o`'s `.text` from
  26,046 bytes to 11,390, and the seven-bit measurement `43 29 4E 31 41 2E` came
  from piping the UTF-8 for e-acute, alpha, `A` and `.` at a hex-echo guest.
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
  `notarytool`; that is open in `todo.txt`. (It is not, as of v4.8.0:
  `notarytool` is written and gated, the certificate step is in
  `MANUAL_CHECKS.md`, and `todo.txt` is empty. Left as written otherwise —
  this is what the release said at the time.)
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
