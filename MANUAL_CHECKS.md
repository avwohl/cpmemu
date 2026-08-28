# Manual checks

Checks a person has to run, because no automated test can stand in for them.
Everything here needs a keyboard, a real terminal program or a machine that is
not the one these were written on.

Delete an item once someone runs it. If it found something, the fix goes in
`CHANGELOG.md` and whatever is still open goes in `todo.txt`; if it found
nothing, the deletion and the commit message are the record. This file is not a
log of checks that were done.

Open items with no person in them are in [`todo.txt`](todo.txt).

---

## A keyboard pass on four terminal programs

**Why this cannot be automated.** Both console harnesses step past the terminal
program. `tests/win_console.cc` writes `INPUT_RECORD`s straight into the console
input buffer with `WriteConsoleInput`, and `tests/pty_console.cc` writes bytes
into a pty master. Each is faithful from the console or the line discipline
inward and neither is faithful outward: if Windows Terminal binds ctrl+left for
its own use, or Terminal.app binds something, the tests still pass and the user
still loses the key. The two harnesses have a `--manual` mode for exactly this,
running a hex echo attached to the real terminal so a person can press a key and
read the bytes the CP/M guest received.

Nobody has done this on either platform. The machine the Windows side was
written on had no interactive terminal, only a windowless console; the macOS
side was driven through a pty by a program, which is not a person either, so
whatever Terminal.app or iTerm2 sets in the termios we inherit is unmeasured.

### macOS

```bash
c++ -std=c++11 -Wall -Wextra -o pty_console tests/pty_console.cc
./pty_console --manual src/cpmemu
```

Run it once in **Terminal.app** and once in **iTerm2**. Terminal.app ships with
the system; iTerm2 has to be downloaded.

Press `.` to finish. What right looks like:

| key            | expected |
|----------------|----------|
| `A`            | `41` |
| ctrl+V         | `16` |
| ctrl+O         | `0F` |
| an arrow key   | `1B 5B` and a letter |
| `.`            | `2E`, then the run ends |

ctrl+V and ctrl+O are the two the macOS line discipline takes for `VLNEXT` and
`VDISCARD` if `IEXTEN` is not cleared, so they are the pair most likely to be
eaten by something outside the emulator. An arrow key printing nothing, or
printing only part of `1B 5B <letter>`, means the terminal program bound it.

### Windows

```
tests\win_console.bat --manual ..\src\cpmemu.exe
```

Run it once in **Windows Terminal** and once in a **legacy conhost** window —
they are different programs with different key bindings, and only conhost is
what an old `cmd.exe` shortcut opens.

The harness prints the table it expects before it starts. Press `.` to finish.
What right looks like:

| key             | expected |
|-----------------|----------|
| arrows          | `05 18 13 04` |
| Home / End      | `11 13` / `11 04` |
| PgUp / PgDn     | `12` / `03` |
| Ins / Del       | `16` / `07` |
| ctrl+arrows     | `01 06 17 1A` |
| ctrl+R / ctrl+O | `12` / `0F` |
| any F key       | nothing at all |

A key that prints nothing when the table says it should print something is a
binding the terminal program took. Ctrl+V is already known not to fall through
on Windows Terminal and no `SetConsoleMode` call changes it — that one is in
`README.md` as a limitation, not a finding.

Note that this pass and running `tests\win_console.bat` without `--manual` are
two different things. The automated Windows cases have also never been executed
anywhere, which is an item in `todo.txt` tagged `[WINDOWS]`.
