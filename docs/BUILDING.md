# Building cpmemu

This document describes how to build cpmemu from source on various platforms.

## Requirements

- C++11 compatible compiler
- No external dependencies (standard library only)

## Linux

### Using Make (Recommended)

```bash
cd src
make
```

This produces `cpmemu` in the `src/` directory.

**Optional:** Install system-wide:
```bash
sudo make install
```

### Build Options

```bash
# Static build (for maximum portability).  Refused on macOS, which has no
# static libc to link against.
make STATIC=1

# qkz80: static only, shared only, or both
make lib
make shared
make libs

# Install and remove the library, headers and qkz80.pc (see below)
sudo make install-lib
sudo make uninstall-lib

# Tests
make unit          # 8080-mode CPU unit tests, under a second
make test          # three quick tests, eyeball only, never fails

# Clean build artifacts
make clean
```

### Install Locations

`make install` installs the two programs:

- `/usr/local/bin/cpmemu`
- `/usr/local/bin/cpm_disk` — the CP/M disk-image tool, installed from
  `util/cpm_disk.py`

The library is a **separate target**, `make install-lib`:

- `/usr/local/lib/libqkz80.a`, `libqkz80.so`
- `/usr/local/include/qkz80/` — seven headers
- `/usr/local/lib/pkgconfig/qkz80.pc`

`PREFIX` (default `/usr/local`), `BINDIR`, `LIBDIR`, `INCLUDEDIR`,
`PKGCONFIGDIR` and `DESTDIR` are honoured, and `qkz80.pc` is generated from
whichever of them the install used. `make uninstall` and `make uninstall-lib`
remove them again.

The `.deb` and the `.rpm` carry the emulator and the library together, and no
`cpm_disk`; there is no separate `-dev` package.

## Windows

### Using Visual Studio (MSVC)

**Prerequisites:**
- Visual Studio 2019 or later with C++ Desktop Development workload

**Build:**
```cmd
cd src
do_build.bat
```

This produces `cpmemu.exe` in the `src/` directory.

### Using MinGW

**Prerequisites:**
- MinGW-w64 with g++

**Build:**
```cmd
cd src
mingw32-make -f Makefile.win
```

### Cross-compiling for Windows from Linux

Useful for checking that a change has not broken the Windows-only half of
`src/os/windows/platform.cc`, which no Linux build touches:

```bash
sudo apt install g++-mingw-w64-x86-64
cd src
make -f Makefile.win CXX=x86_64-w64-mingw32-g++ AR=x86_64-w64-mingw32-ar
```

`tests/run_tests.sh` does this automatically into a temporary directory when
the cross-compiler is on `PATH`, and skips the check when it is not. It proves
the Windows code compiles, and nothing more: `_getch`/`_kbhit` only behave on a
real console, which is what `tests/win_console.bat` is for.

### Testing the console on Windows

```cmd
cd src
do_build.bat

cd ..\tests
win_console.bat
```

`win_console.bat` builds `win_console.cc` with the same Visual Studio as
`src\do_build.bat`, then drives a real console: it starts cpmemu with stdin
bound to that console, writes the `INPUT_RECORD`s a keyboard would produce, and
compares the bytes the CP/M guest received. It is the only part of the suite
that cannot run on Linux, because the extended key path sits behind
`is_terminal()` and no pipe reaches it.

To check the keys by hand instead - which is the only way to find out whether
the terminal program itself swallows one before the console ever sees it:

```cmd
win_console.exe --manual ..\src\cpmemu.exe
```

### Using CMake (Cross-Platform)

**Prerequisites:**
- CMake 3.10 or later
- A C++ compiler (MSVC, MinGW, or GCC)

**Build:**
```bash
cd src
cmake -B build
cmake --build build --config Release
```

**With Visual Studio generator:**
```cmd
cd src
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The executable will be in `build/` or `build/Release/` depending on the generator.

## macOS

### Using Make

```bash
cd src
make
```

`STATIC=1` is refused here, and the makefile says so rather than ignoring it.
The SDK ships no `crt0.o`, no `libc.a`, no `libSystem.a` and no `libc++.a`, so
`-static` cannot link at all - it stops at `ld: library 'crt0.o' not found` -
and `-static-libstdc++` is accepted and silently ignored by Apple clang, so
dropping the flag quietly would hand you a dynamically linked binary you
believed was static. What does control portability across macOS versions is
`MACOSX_DEPLOYMENT_TARGET`:

```bash
make MACOSX_DEPLOYMENT_TARGET=12.0
```

`make shared` produces `libqkz80.<major>.dylib` plus an unversioned
`libqkz80.dylib` symlink, not a `.so`, and stamps an absolute install name so a
consumer can find it at run time; `make install-lib` re-stamps that install
name for wherever `PREFIX` puts it. A `.so` here was worse than a naming
mistake: Apple's linker prefers one over the `libqkz80.a` beside it, and the
bare install name it carried was one dyld could not resolve, so a consumer
linked cleanly and then aborted at exec.

### Using CMake

```bash
cd src
cmake -B build
cmake --build build
```

For a universal binary covering both Apple silicon and Intel, which is what the
release workflow ships:

```bash
cd src
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build build --parallel
( cd build && cpack )
```

That writes `cpmemu-<version>-Darwin-arm64-x86_64.tar.gz`, holding the binary,
`libqkz80.a` and the headers. No dylib goes into it: a dylib's install name is
an absolute path, so one shipped in a tarball the user unpacks wherever they
like would be a library dyld cannot find.

## Platform Abstraction

cpmemu uses a platform abstraction layer located in `src/os/`:

| Directory | Platform | APIs Used |
|-----------|----------|-----------|
| `os/linux/` | Linux, macOS, BSD | POSIX (termios, select, dirent) |
| `os/windows/` | Windows | Win32 (Console API, FindFirstFile) |

The abstraction provides:
- Terminal raw mode for character-by-character input
- Non-blocking stdin check
- Directory listing
- File type/size queries
- Working directory changes

## Creating Packages

### Linux (DEB/RPM)

The GitHub Actions workflow automatically builds packages on release. To build locally:

```bash
# Install fpm
gem install fpm

# Build DEB
fpm -s dir -t deb -n cpmemu -v 1.0.0 \
    --prefix /usr/local \
    src/cpmemu=bin/cpmemu

# Build RPM
fpm -s dir -t rpm -n cpmemu -v 1.0.0 \
    --prefix /usr/local \
    src/cpmemu=bin/cpmemu
```

### Windows (MSIX)

**Prerequisites:**
- Windows 10 SDK (for makeappx.exe and signtool.exe)
- Code signing certificate (for distribution)

**Build MSIX:**
```powershell
cd packaging\windows

# Generate icons (requires Python + Pillow)
python generate_icons.py

# Build and sign package
.\build-msix.ps1 -CertPath path\to\cert.pfx -CertPassword yourpassword
```

**For local testing without a purchased certificate:**
```powershell
# Create self-signed certificate
$cert = New-SelfSignedCertificate -Type Custom -Subject "CN=TestPublisher" `
    -KeyUsage DigitalSignature -FriendlyName "Test Cert" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")

# Export to PFX
$pwd = ConvertTo-SecureString -String "test123" -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath "test-cert.pfx" -Password $pwd

# Trust the certificate (run as Administrator)
Import-Certificate -FilePath "test-cert.cer" -CertStoreLocation "Cert:\LocalMachine\TrustedPeople"
```

See [packaging/windows/README.md](../packaging/windows/README.md) for detailed MSIX packaging instructions.

## Testing

### Quick Tests

```bash
cd src
make test
```

### Z80 Instruction Tests

```bash
tests/run_tests.sh --zex
```

Do not put a short cap on these by hand. Each suite takes about seven minutes,
and a 180 second limit - which an earlier version of this document recommended -
stops about five instruction groups in and leaves partial output that reads like
a finished run. That mistake is written up in `tests/README.md`. The runner caps
each at an hour and reports a truncated run as a failure rather than a pass;
override it with `CPMEMU_ZEX_TIMEOUT` if a slower machine needs longer.

### 8080 Tests

The exercisers in `tests/8080/` run under `--8080`:

```bash
cd src
./cpmemu --8080 ../tests/8080/8080pre.com    # preliminary test, seconds
./cpmemu --8080 ../tests/8080/8080exm.com    # 25 instruction groups, ~4 minutes
```

`tests/run_tests.sh` runs `8080pre.com` as part of the quick suite and
`8080exm.com` under `--zex`. `tests/8080/README.md` lists the files and where
they came from.

## Troubleshooting

### Linux: "permission denied"

```bash
chmod +x src/cpmemu
```

### Windows: "not recognized as internal or external command"

Run from a Developer Command Prompt, or let `src/do_build.bat` set the
environment up: it asks `vswhere.exe` where Visual Studio is rather than naming
a version or an edition, and only falls back to a hardcoded path if the
installer is gone. To do it by hand, call `vcvarsall.bat x64` from your own
installation.

### Windows MSIX: "Publisher mismatch"

The Publisher in `AppxManifest.xml` must exactly match the Subject of your signing certificate.

### macOS: Terminal not in raw mode

Ensure your terminal supports the required termios settings. Most modern terminals (Terminal.app, iTerm2) work correctly.
