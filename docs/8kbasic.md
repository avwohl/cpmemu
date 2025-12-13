# Altair 8K BASIC on altair_emu

This document describes running MITS Altair 8K BASIC (Rev 4.0) on the `altair_emu` hardware-level emulator.

## Quick Start

```bash
# Interactive session
./altair_emu ~/mbasic2025/4k8k/8kbas.bin

# Answer startup questions:
#   MEMORY SIZE? 32000
#   TERMINAL WIDTH? 72
#   WANT SIN-COS-TAN-ATN? N
```

## Startup Questions

When 8K BASIC starts, it asks several configuration questions:

| Question | Description | Typical Answer |
|----------|-------------|----------------|
| MEMORY SIZE? | RAM available (bytes). Press Enter for auto-detect, or enter a value like 32000 | `32000` or Enter |
| TERMINAL WIDTH? | Characters per line for output formatting | `72` or `80` |
| WANT SIN-COS-TAN-ATN? | Include trig functions (uses more memory) | `Y` or `N` |

After answering, BASIC displays:
```
25670 BYTES FREE
ALTAIR BASIC REV. 4.0
[EIGHT-K VERSION]
COPYRIGHT 1976 BY MITS INC.
OK
```

## 4K BASIC Differences

4K BASIC asks additional questions to minimize memory usage:

| Question | Description |
|----------|-------------|
| SIN? | Include SIN function |
| RND? | Include RND (random) function |
| SQR? | Include SQR (square root) function |

Answer `N` to exclude functions and save memory, or `Y` to include them.

## Cassette Tape Operations

### Saving Programs (CSAVE)

```bash
./altair_emu --tape-out=myprogram.cas ~/mbasic2025/4k8k/8kbas.bin
```

In BASIC:
```basic
10 PRINT "HELLO WORLD"
20 GOTO 10
CSAVE "HELLO"
```

The program is saved to `myprogram.cas` in BASIC's internal tokenized format.

### Loading Programs (CLOAD)

```bash
./altair_emu --tape-in=myprogram.cas ~/mbasic2025/4k8k/8kbas.bin
```

In BASIC:
```basic
CLOAD "HELLO"
LIST
RUN
```

### Cassette File Format

The cassette format is BASIC's internal binary tokenized format:
- **Leader**: Three 0xD3 bytes
- **Filename**: ASCII name (up to 6 characters)
- **Program data**: Tokenized BASIC (one byte per keyword)
- **Terminator**: 0x00 0x00 0x00

Token examples:
- 0x96 = PRINT
- 0x89 = GOTO
- 0x88 = GOSUB

## I/O Port Map

The emulator implements these Altair I/O ports:

| Port | Function | Description |
|------|----------|-------------|
| 0x10 | Console Status | 88-2SIO serial status register |
| 0x11 | Console Data | 88-2SIO serial data register |
| 0x06 | Cassette Status | 88-ACR cassette status register |
| 0x07 | Cassette Data | 88-ACR cassette data register |
| 0xFF | Sense Switches | Front panel sense switch register |

### 88-SIO Status Register Bits

| Bit | Name | Meaning (Read) |
|-----|------|----------------|
| 0 | TXE | Transmit buffer empty (1 = ready to send) |
| 1 | RXF | Receive buffer full (1 = data available) |
| 2 | PE | Parity error |
| 3 | FE | Framing error |
| 4 | OE | Overrun error |
| 5-7 | - | Unused |

## Command Line Options

```
Usage: altair_emu [options] <binary.bin>

Options:
  --debug          Enable debug output (shows all I/O)
  --mem=N          Set memory size in KB (default: 64)
  --sense=0xNN     Set sense switch value
  --load=0xNNNN    Load address (default: 0x0000)
  --start=0xNNNN   Start address (default: load address)
  --tape-in=FILE   Cassette input file (for CLOAD)
  --tape-out=FILE  Cassette output file (for CSAVE)
```

## Memory Map

| Address | Contents |
|---------|----------|
| 0x0000-0x1FFF | 8K BASIC interpreter |
| 0x2000-0xFEFF | User RAM (BASIC programs, variables) |
| 0xFF00-0xFFFF | Protected (appears as ROM for memory detection) |

BASIC detects available memory by writing to byte 0 of each 256-byte page and checking if the write succeeded. The emulator protects page 0xFF00 to simulate ~64K RAM.

## Memory Size Auto-Detection

When you press Enter at "MEMORY SIZE?", BASIC probes memory:
1. Writes a test byte to address 0xNN00 (page boundary)
2. Reads it back
3. If different (ROM), that's the top of RAM
4. Continues down until it finds writable RAM

The emulator makes 0xFF00+ act like ROM, so BASIC finds ~64K usable.

## Teletype Compatibility

BASIC was designed for ASR-33 Teletypes running at 110 baud. It outputs NULL characters (0x00) after carriage returns to give the print head time to return. The emulator filters these NULLs from output.

## Example Session

```bash
$ echo -e "32000\n72\nN\n10 FOR I=1 TO 5\n20 PRINT I\n30 NEXT I\nRUN\n" | \
  ./altair_emu ~/mbasic2025/4k8k/8kbas.bin 2>&1

MEMORY SIZE? 32000
TERMINAL WIDTH? 72
WANT SIN-COS-TAN-ATN? N

25670 BYTES FREE
ALTAIR BASIC REV. 4.0
[EIGHT-K VERSION]
COPYRIGHT 1976 BY MITS INC.
OK
10 FOR I=1 TO 5
20 PRINT I
30 NEXT I
RUN
 1
 2
 3
 4
 5
OK
```

## References

- [Altair 8800 BASIC Reference Manual (July 1977)](https://altairclone.com/downloads/manuals/)
- [emuStudio 88-SIO Documentation](https://www.emustudio.net/documentation/user/altair8800/88-sio)
- [SIMH Altair Simulator](https://github.com/simh/simh/blob/master/ALTAIR/altair.txt)
- [Altair Cassette Interface Manual](https://altairclone.com/downloads/manuals/Cassette%20Interface%20Manual.pdf)
