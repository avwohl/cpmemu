# Debug Research Notes

Research into extending debugging capabilities for the CP/M emulator.

## SID/ZSID Features (Original CP/M Debuggers)

### Core Commands

| Command | Function |
|---------|----------|
| **A** (Assemble) | Insert 8080/Z80 machine code using mnemonics |
| **C** (Call) | Execute subroutine without disturbing program state; can preset BC/DE registers |
| **D** (Display) | Show memory in hex/ASCII, byte or word format |
| **F** (Fill) | Fill memory range with constant value |
| **G** (Go) | Execute with up to 2 breakpoints |
| **H** (Hex) | Hex arithmetic, value conversion, list all symbols |
| **I** (Input) | Simulate CCP command line for test programs |
| **L** (List) | Disassemble with symbolic labels |
| **M** (Move) | Copy memory blocks |
| **P** (Pass) | Set pass points with counters (like watchpoints) |
| **R** (Read) | Load programs and symbol tables |
| **S** (Set) | Modify memory locations |
| **T** (Trace) | Single-step with full instruction monitoring |
| **U** (Untrace) | Monitor execution without symbols |
| **X** (Examine) | Display CPU registers |

### Key Capabilities
- Symbol table support via `.sym` files from linker
- Breakpoints (up to 2 simultaneous)
- Pass counters for monitoring specific addresses
- Inline assembly/disassembly

### Limitations
1. Debugger shared address space with target - couldn't debug large programs
2. No per-source-line debug info
3. No multi-level mapping (e.g., Basic -> C -> ASM)

---

## GDB for Z80/8080

### Current State
- **Z80 support merged into mainline GDB** (as of ~2020-2025)
- Target: `z80-unknown-elf`
- Build command:
  ```bash
  git clone https://github.com/bminor/binutils-gdb
  cd binutils-gdb
  mkdir build
  ./configure --target=z80-unknown-elf --prefix=$(pwd)/build
  make && make install
  ```

### GDB Remote Serial Protocol

For emulator integration, implement a stub with these minimal commands:

| Packet | Function |
|--------|----------|
| `?` | Report halt reason |
| `g`/`G` | Read/write registers |
| `m`/`M` | Read/write memory |
| `c`/`s` | Continue/step |

Packet format: `$packet-data#checksum`

Checksum: sum of ASCII values of packet data, lowest 8 bits

Register order for Z80: AF, BC, DE, HL, SP, PC, IX, IY, plus alternates

### Limitations
- GDB for Z80 is **assembler-level only**
- No built-in support for reading Z80 compiler symbol formats
- No stack frame interpretation
- No 8080-specific target (would need custom implementation)

---

## z88dk Tools

### z88dk-gdb

**NOT actual GNU GDB** - it's a custom GDB client that:
- Implements GDB remote protocol (client side)
- Connects to emulators (MAME, Fuse, ticks) or hardware with GDB server
- Reads z88dk's debug symbol formats

### ticks

z88dk's command-line CPU emulator/debugger:
- Supports z80, 8080, 8085, gbz80, z180, z80n, R800, KC160, Rabbit
- Includes debugger and disassembler
- Can expose GDB server interface
- Reports accurate timing for each target

### Debug Formats from z88dk

| Format | Description |
|--------|-------------|
| `.map` | Symbol-to-address table (from `-m` flag) |
| `.sym` | Additional symbols |
| `.lis` | Listing with C code in comments (with `--c-code-in-asm --list`) |
| `.adb` | Debug symbols (work in progress) |
| `.cdb` | SDCC C debug files (incomplete for Z80) |

Compile with `-debug` flag for debuggable output.

---

## DeZog (VS Code Extension)

A mature Z80 debugger alternative:
- Source-level debugging via `.list` and `.sld` files
- Reverse debugging (step backwards)
- Multiple emulator backends (ZEsarUX, CSpect, MAME, internal simulator)
- Conditional breakpoints
- Memory/register inspection with label resolution
- Code coverage visualization

---

## Tool Comparison

| Tool | What It Is |
|------|------------|
| **GDB (z80 target)** | Full GNU GDB compiled for Z80; connects to any GDB stub; limited symbol format support |
| **z88dk-gdb** | Custom lightweight GDB client for z88dk toolchain; reads z88dk symbol formats |
| **ticks** | z88dk's emulator with built-in debugger; can expose GDB server interface |
| **DeZog** | VS Code extension; connects to various emulators; good symbol support |

---

## Implementation Options for cpmemu

### Option 1: GDB Remote Stub
- Implement GDB remote protocol in emulator
- Works with both real GDB and z88dk-gdb
- Industry standard, good tooling support
- Limitation: GDB has weak support for Z80/8080 symbol formats

### Option 2: Custom Debug Protocol (SID-style shim)
- Create `usid.com` or `uzsid.com` shim program
- Shim traps out to emulator for symbol info
- Emulator loads symbols separately, not in CP/M address space
- Allows debugging large programs

### Option 3: DeZog Integration
- Implement DeZog's remote protocol
- Good source-level debugging support
- VS Code integration out of the box

---

## References

- [SID/ZSID Manual PDF](http://www.cpm.z80.de/randyfiles/DRI/SID_ZSID.pdf)
- [SID User's Guide - Internet Archive](https://archive.org/details/CPM_SID_Symbolic_Instruction_Debugger_Users_Guide)
- [GDB Z80 Implementation Blog](https://www.chciken.com/tlmboy/2022/04/03/gdb-z80.html)
- [DeZog GitHub](https://github.com/maziac/DeZog)
- [gdb-z80 fork](https://github.com/legumbre/gdb-z80)
- [z88dk Debugging Wiki](https://www.z88dk.org/wiki/doku.php?id=debugging)
- [z88dk-gdb Wiki](https://github.com/z88dk/z88dk/wiki/Tool-z88dk-gdb)
- [ticks Wiki](https://github.com/z88dk/z88dk/wiki/Tool---ticks)
- [z88dk Debug Symbol Issue](https://github.com/z88dk/z88dk/issues/1390)
- [z88dk Source Level Debugging Discussion](https://www.z88dk.org/forum/viewtopic.php?t=9644)
