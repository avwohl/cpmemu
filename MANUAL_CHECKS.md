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
two different things. The automated cases now run on every push: the `windows`
job in `.github/workflows/ci.yml` builds `cpmemu.exe` with MSVC and runs all 28
of them on a `windows-latest` runner, and they pass. What that job cannot do is
press a key on a keyboard, which is what the pass above is for and why it is
still open.

---

## Turn on macOS signing and notarization

**Why this cannot be automated.** It needs a person on a Mac signed in to the
Apple Developer account, and one of the steps is restricted to a role that only
a person holds. Everything that *can* be automated already is: the signing and
notarizing steps are written in `.github/workflows/release.yml` and gated on
secrets that do not exist yet, so they skip and the job is green.

This is not a purchase. The project ships an app through the App Store and uses
TestFlight, neither of which is possible without a paid Apple Developer Program
membership — so the membership has been there all along. What is missing is a
**Developer ID Application** certificate, which is a different certificate from
the App Store ones and is covered by that same membership at no extra cost.

[`docs/macos-signing.md`](docs/macos-signing.md) has the detail and the traps.
The short form:

1. As the **Account Holder** (Admin is not enough for this one), create a
   Developer ID Application certificate — developer.apple.com → Certificates,
   Identifiers & Profiles → Certificates → + . Five per team is the budget.
2. Export it from Keychain Access as a `.p12`, on the machine that made the
   request, or it will have no private key.
3. Mint an App Store Connect API key — a **Team** key, not an Individual one;
   Apple does not let Individual keys use `notarytool` at all.
4. Set the five repository secrets `docs/macos-signing.md` lists.
5. Cut a release and read the log. None of the signing path has ever run, so
   this step is a test, not a formality: the keychain import,
   `security set-key-partition-list`, the per-architecture `CDHash` check after
   `cpack`, and `notarytool --wait` have never been observed working here.
   Expect `"status":"Accepted"`.

What right looks like afterwards, from a Mac that did not build it:

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu-macos-universal.tar.gz
tar xzf cpmemu-macos-universal.tar.gz
spctl --assess --type execute --verbose=4 cpmemu-*-Darwin-arm64-x86_64/bin/cpmemu
```

`accepted` and `source=Notarized Developer ID`. If it says `rejected`, the
ticket is not being found and the `xattr` line in `README.md` has to stay.

Note what this does **not** buy, so the README is not over-corrected
afterwards: a notarization ticket cannot be stapled to a bare Mach-O
executable, so a browser download on a machine that is offline still needs the
`xattr` line. Only shipping a `.pkg` or `.dmg` would fix that, and that changes
what the download is.
