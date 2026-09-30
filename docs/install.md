# Installing cpmemu

Packages for every platform, and the build from source.

## Debian/Ubuntu

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu_amd64.deb
sudo dpkg -i cpmemu_amd64.deb
```

Use `cpmemu_arm64.deb` on ARM64.

## RHEL/Fedora

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu.x86_64.rpm
sudo rpm -i cpmemu.x86_64.rpm
```

Use `cpmemu.aarch64.rpm` on ARM64. Both package families also install the qkz80
library and headers - see [The qkz80 library](qkz80_library.md) - and
`cpm_disk`, the disk-image tool, which is a Python script and which the
packages name `python3` a `Recommends:` for.

## macOS

macOS 12 or later.

```bash
curl -LO https://github.com/avwohl/cpmemu/releases/latest/download/cpmemu-macos-universal.tar.gz
tar xzf cpmemu-macos-universal.tar.gz
xattr -dr com.apple.quarantine cpmemu-*-Darwin-arm64-x86_64
sudo cp cpmemu-*-Darwin-arm64-x86_64/bin/* /usr/local/bin/
```

`bin/` holds two programs, `cpmemu` and `cpm_disk`. The second is a Python
script and macOS ships no Python of its own: `/usr/bin/python3` is an
`xcode-select` stub, so `cpm_disk` runs for anyone with the Command Line Tools,
Homebrew or a python.org install, and on a machine with none of the three the
stub opens the "install the command line developer tools" dialog rather than
running anything.

The release is not notarized, so the `xattr` line is what stops Gatekeeper
refusing the binary. [macos-signing.md](macos-signing.md) has the
detail.

## Windows

An MSIX package is published for some releases, built by hand rather than by CI.
It is unsigned - identity `CN=CPMEmuTest` - so it installs only with Developer
Mode on:

```powershell
# curl.exe, not curl: in Windows PowerShell `curl` is an alias for
# Invoke-WebRequest, which has no -LO
curl.exe -LO https://github.com/avwohl/cpmemu/releases/download/v4.5.1/cpmemu.msix
Add-AppPackage -Path .\cpmemu.msix -AllowUnsigned
```

The most recent one is in
[v4.5.1](https://github.com/avwohl/cpmemu/releases/tag/v4.5.1). For a current
build, build from source with `src/do_build.bat` (MSVC) or package one with
`packaging/windows/build-msix.ps1`.

## From source

```bash
make -C src
```

Needs a C++11 compiler. This leaves the binary at `src/cpmemu`; `sudo make -C
src install` puts it on `PATH` (with `cpm_disk` beside it).
[BUILDING.md](BUILDING.md) covers every platform, CMake, MinGW and
cross-compiling. `STATIC=1` is refused on macOS, which has no static libc to
link against.

Then:

```bash
cpmemu program.com          # or ./src/cpmemu program.com, uninstalled
```
