# CP/M 2.2 Web Emulator

WebAssembly-based CP/M 2.2 emulator that runs in a browser.

## Overview

This is a web-based version of the CP/M emulator using:
- **qkz80** - 8080/Z80 CPU emulator core
- **Emscripten** - Compiles C++ to WebAssembly
- **xterm.js** - Terminal emulation in the browser

## Disk Format

Standard IBM 8" single-sided single-density (SSSD) format:
- 77 tracks
- 26 sectors per track
- 128 bytes per sector
- 256,256 bytes total (250KB)
- **No sector skew** (disk images stored in sequential order)

## Building

### Prerequisites

- Emscripten SDK (emcc)
- um80/ul80 assembler (for BIOS)
- GNU Make

### Build Commands

```bash
# Build everything (web + CLI)
make

# Build only web version
make cpm.js

# Build only CLI test version
make cpm_cli

# Clean build artifacts
make clean

# Start local test server
make serve
```

### Build Output

- `cpm.js` - Emscripten JavaScript loader
- `cpm.wasm` - WebAssembly binary
- `cpm.data` - Bundled files (bios.sys, cpm22.sys, drivea.img)
- `cpm_cli` - Command-line version for testing

## Deployment

Copy these files to any web server:

```
index.html   - Web interface
cpm.js       - JavaScript loader
cpm.wasm     - WebAssembly binary
cpm.data     - Bundled disk images and system files
```

## Web Interface Features

- **Auto-start** - CP/M boots automatically with bundled disk
- **Drive A:** - Load custom disk images (boot drive)
- **Drive B:** - Load additional disk images
- **Download** - Save modified disk images
- **Terminal** - Full xterm.js terminal with keyboard input

## CLI Version (cpm_cli)

Command-line version for testing without a browser:

```bash
./cpm_cli                    # Run with default files
./cpm_cli -d                 # Enable disk debug output
./cpm_cli -a disk.img        # Use custom disk image
./cpm_cli -s system.sys      # Use custom CP/M system
./cpm_cli -b bios.sys        # Use custom BIOS
```

## Files

| File | Description |
|------|-------------|
| `cpm_web.cc` | WebAssembly emulator source |
| `cpm_cli.cc` | CLI emulator source |
| `bios.asm` | BIOS assembly source |
| `bios.sys` | Compiled BIOS binary |
| `index.html` | Web interface |
| `Makefile` | Build configuration |

## BIOS Implementation

The BIOS is a minimal stub that provides:
- Jump table at F600h (17 entry points)
- Disk Parameter Headers (DPH) for 4 drives
- Disk Parameter Block (DPB) for 8" SSSD format
- Work areas (DIRBUF, CSV, ALV)

Actual I/O is trapped by the emulator at the BIOS entry points
and handled in C++/JavaScript.

## Architecture

```
+------------------+
|   Browser/CLI    |
+------------------+
        |
+------------------+
|    cpm_web.cc    |  BIOS trap handler, disk I/O
+------------------+
        |
+------------------+
|     qkz80        |  8080 CPU emulator
+------------------+
        |
+------------------+
|   CP/M 2.2       |  CCP + BDOS (cpm22.sys)
+------------------+
        |
+------------------+
|   bios.sys       |  Jump table + disk tables
+------------------+
```
