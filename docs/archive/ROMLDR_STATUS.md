# RomWBW Boot Loader (romldr) Integration Status

## Current State (December 2025)

The romldr boot loader is now partially functional in the emulator.

### What Works

- **Boot loader startup**: romldr loads and displays the boot menu
- **Keyboard input**: Commands can be typed and are recognized
- **Help command** (`h`): Displays available commands
- **List ROM apps** (`l`): Shows M (Monitor) and Z (Z-System)
- **Basic command parsing**: Input is properly received and processed

### What Doesn't Work Yet

- **Device inventory** (`d`): Shows no devices - disk enumeration not implemented
- **Disk booting**: Cannot boot from disk images yet
- **Only 2 ROM apps visible**: Should show more applications from the ROM

### Known Issues

1. **LF to CR translation**: Unix terminals send LF (0x0A) for Enter, but CP/M expects CR (0x0D). The emulator now translates LF→CR in CIOIN.

### Implementation Details

#### HBIOS Functions Implemented

- `CIOIN` (0x00): Console input with LF→CR translation
- `CIOOUT` (0x01): Console output
- `CIOIST` (0x02): Console input status with flush workaround
- `CIOOST` (0x03): Console output status
- `SYSVER` (0xF1): Returns version 3.6.0
- `SYSGET` (0xF8): Various system info queries
- `SYSSET` (0xF9): System configuration
- `SYSPEEK` (0xFA): Read byte from banked memory
- `SYSPOKE` (0xFB): Write byte to banked memory
- `SYSSETBNK` (0xFC): Bank switching

#### Type-ahead Preservation (flush Workaround)

RomWBW's romldr calls a `flush` routine before displaying the boot prompt to
clear any stale data from console input buffers. While this is correct behavior
for real hardware, it causes type-ahead to be lost in the emulator.

The workaround in CIOIST:
1. Detects calls from flush by checking if the caller return address is 0x8256
2. Returns 0 (no char available) to hide type-ahead from flush
3. Preserves keystrokes for the actual command line handler (rdln)

Note: The conpoll routine in romldr is NOT buggy - it correctly saves curcon
in register E, loads ciocnt from memory, and explicitly skips the active console
with `CP E / JR Z`. Earlier documentation incorrectly claimed conpoll was buggy.

### Files

- `emu_romwbw.rom`: 512KB ROM image with emu_hbios in bank 0
- `romldr.bin`: 32KB boot loader extracted from SBC_simh_std ROM
- `src/altair_emu.cc`: Main emulator with HBIOS implementation

### Running

```bash
./altair_emu --romwbw emu_romwbw.rom --romldr=romldr.bin --hbdisk0=<disk.img>
```

### Next Steps

1. Implement disk device enumeration for `d` command
2. Implement disk boot sector loading
3. Debug why only 2 ROM applications are visible
4. Test actual CP/M boot from disk image
