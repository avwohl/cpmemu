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
# Static build (for maximum portability)
make STATIC=1

# Build only the qkz80 library
make lib

# Build shared library
make shared

# Run quick tests
make test

# Clean build artifacts
make clean
```

### Install Locations

- Binary: `/usr/local/bin/cpmemu`
- Library: `/usr/local/lib/libqkz80.a`
- Headers: `/usr/local/include/qkz80/`

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

### Using CMake

```bash
cd src
cmake -B build
cmake --build build
```

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
cd src
timeout 180 ./cpmemu ../tests/zexdoc.com    # Documented instructions
timeout 180 ./cpmemu ../tests/zexall.com    # All instructions (slower)
```

### 8080 Tests

```bash
cd src
./cpmemu ../tests/8080/TEST.COM
./cpmemu ../tests/8080/CPUTEST.COM
```

## Troubleshooting

### Linux: "permission denied"

```bash
chmod +x src/cpmemu
```

### Windows: "not recognized as internal or external command"

Make sure you're running from a Developer Command Prompt, or that the Visual Studio environment is set up:
```cmd
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
```

### Windows MSIX: "Publisher mismatch"

The Publisher in `AppxManifest.xml` must exactly match the Subject of your signing certificate.

### macOS: Terminal not in raw mode

Ensure your terminal supports the required termios settings. Most modern terminals (Terminal.app, iTerm2) work correctly.
