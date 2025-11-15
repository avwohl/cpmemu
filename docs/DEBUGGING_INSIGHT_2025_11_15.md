# Z80 Emulator Debugging Insight - November 15, 2025

## Key Discovery

After analyzing the zexdoc source code (`/home/wohl/zexall-src/zexdoc.z80`), I discovered that **the CRC includes much more than just flags**.

### What zexdoc CRC Actually Tests

Lines 1104-1109 of zexdoc.z80 show that the CRC is calculated over 16 bytes from `msat` containing:
- Bytes 0-1: Memory operand (memop)
- Bytes 2-3: IY register
- Bytes 4-5: IX register
- Bytes 6-7: HL register
- Bytes 8-9: DE register
- Bytes 10-11: BC register
- Bytes 12-13: AF register (with flags masked at line 1102)
- Bytes 14-15: SP (stack pointer)

### Critical Insight

Even **simple LD instructions** like `LD B,C` are failing (CRC mismatch). These instructions:
- Don't modify flags at all
- Only copy one register to another
- Should be trivial to implement correctly

This means the problem is NOT primarily in flag calculations, but in:
1. **Register value handling** - actual data in registers is wrong
2. **Byte ordering** - register pairs might have wrong endianness
3. **PUSH/POP operations** - how registers are saved to memory
4. **IX/IY handling** - index registers might have issues
5. **Some other systematic issue** affecting ALL instructions

## Why This Matters

We've spent significant effort fixing flag calculations (DAA, CB shifts/rotates, etc.) and saw CRC improvements, but **still 0/67 tests pass**. This suggests flags are ONE part of the problem, but not the PRIMARY issue.

### Test Evidence

```
ld <bcdehla>,<bcdehla>........ ERROR **** crc expected:744b0118 found:dfe39de0
```

This test exercises simple register-to-register loads. The vast CRC difference suggests systematic register handling issues.

## Recommended Next Steps

### 1. Add Debug Output to zexdoc

Modify zexdoc.z80 lines 1112-1123 (currently disabled with `if 0`) to enable printing of:
- The 16 bytes of `msat` for each test case
- This shows actual register state being fed to CRC

### 2. Compare First Test Case

Run both emulators on the first test (`<adc,sbc> hl`) and compare:
- The `msat` bytes from our emulator
- The `msat` bytes from tnylpo (which passes)
- Identify which specific bytes differ

### 3. Trace Single Instruction

Pick the simplest failing case and trace:
- Initial register state (from `msbt`)
- The instruction being executed
- Final register state (to `msat`)
- Compare byte-by-byte with expected

### 4. Check Register Pair Byte Order

Verify that when we store BC register pair to memory:
- Low address gets C (low byte)
- High address gets B (high byte)
- This is Z80 little-endian convention

Verify PUSH BC writes:
- B first (to SP-1)
- C second (to SP-2)
- Final SP = original SP - 2

## Code Locations to Investigate

### Register Access
- `/home/wohl/qkz80/src/qkz80.cc:217-240` - `get_reg8()` and `set_reg8()`
- `/home/wohl/qkz80/src/qkz80_reg_pair.h:19-24` - `get_low()` and `get_high()`

### PUSH/POP Operations
- Search for PUSH implementation in qkz80.cc
- Verify SP modification and memory write order

### IX/IY Handling
- `/home/wohl/qkz80/src/qkz80_reg_set.h:23-24` - IX, IY declarations
- Search for IX/IY usage in instruction implementations

## Reference: tnylpo Source

Available at `/home/wohl/cl/tnylpo/`:
- `cpu.c` - CPU instruction implementation (62KB)
- Known to pass all zexdoc tests
- Can compare our implementations with theirs

## CRITICAL NEW INFORMATION

### Emulator Previously Passed 8080 Tests

User reports: **"it passed the tests as an 8080 before starting to add z80"**

This is HUGE because it means:
1. Basic instruction execution WAS working correctly
2. Something broke when Z80 support was added
3. The bug is likely in Z80-mode-specific code or mode interaction

### Git History Evidence

Commit `0248662 Add CPU mode switch for 8080 vs Z80 compatibility` added the mode system.

Before that, the emulator was 8080-only and presumably passed 8080 instruction tests.

### Hypothesis: Mode-Related Bug

Since LD instructions (which don't touch flags) are failing, and the emulator worked as 8080, there must be a mode-related bug affecting NON-FLAG operations. Possibilities:

1. **Mode incorrectly affects non-flag operations**: Some LD/PUSH/POP code path checks mode when it shouldn't
2. **Register struct layout changed**: Adding Z80 registers (IX, IY, etc.) might have broken memory layout
3. **Stack operations broken**: Z80-specific stack handling might be wrong
4. **Byte ordering issue**: Something in Z80 mode causes wrong endianness

### Test Plan

1. **Bisect git history**: Find exact commit where tests started failing
2. **Compare register layouts**: Check if qkz80_reg_set struct changed between working and broken versions
3. **Test with MODE_8080**: See what happens (even though zexdoc needs Z80 features)
4. **Check IX/IY impact**: Temporarily remove IX/IY from struct and see if layout issues disappear

## Session Files

- `/home/wohl/zexall-src/zexdoc.z80` - Test source code (critical reference)
- `/tmp/zexdoc_latest.txt` - Current test results
- `/home/wohl/qkz80/docs/SESSION_2025_11_15_AFTERNOON.md` - Previous session notes
- `/home/wohl/qkz80/docs/CPU_MODE_IMPLEMENTATION.md` - Mode implementation details
- `/home/wohl/cl/tnylpo/` - Reference emulator source (passes all tests)
