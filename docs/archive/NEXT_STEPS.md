# Next Steps for Z80 Emulator Debugging

## Investigation Summary

After thorough analysis, I've determined:

1. **zexdoc CRC tests ALL registers, not just flags** - The test includes 16 bytes: memop(2), IY(2), IX(2), HL(2), DE(2), BC(2), AF(2), SP(2)

2. **Even simple LD instructions fail** - This proves the issue is NOT primarily in flag calculations

3. **Register pair numbering is correct** - The enum values (BC=0, DE=1, HL=2, SP=3, AF=4, IX=6, IY=7) work correctly because:
   - Values 0-3 are extracted from 2-bit opcode fields
   - IX/IY use prefix bytes (0xDD/0xFD), not opcode bits
   - The internal values 6 and 7 for IX/IY don't conflict

4. **The emulator previously passed 8080 tests** - This means basic execution WAS working

## Critical Next Step: Add Debug Output

We need to **see actual vs. expected register values**. Two approaches:

### Option 1: Modify Our Emulator (EASIEST)

Add debug output to `src/cpm_emulator.cc` or `src/qkz80.cc` to dump register state periodically.

For example, add to qkz80::step():
```cpp
static int instruction_count = 0;
instruction_count++;

if (instruction_count % 1000 == 0 && regs.PC.get_pair16() > 0x100 && regs.PC.get_pair16() < 0x2000) {
    fprintf(stderr, "PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X\n",
        regs.PC.get_pair16(),
        regs.SP.get_pair16(),
        (get_reg8(reg_A) << 8) | regs.get_flags(),
        regs.BC.get_pair16(),
        regs.DE.get_pair16(),
        regs.HL.get_pair16(),
        regs.IX.get_pair16(),
        regs.IY.get_pair16());
}
```

### Option 2: Reassemble zexdoc with Debug Enabled (BLOCKED)

I modified `/home/wohl/zexall-src/zexdoc.z80` line 1112 from `if 0` to `if 1` to enable debug output.

**Problem**: Need a Z80 assembler to rebuild. Options:
- Install `pasmo` (requires sudo: `sudo apt-get install pasmo`)
- Install `z80asm` (requires sudo: `sudo apt-get install z80asm`)
- Find pre-built assembler binary

After assembling with `pasmo zexdoc.z80 zexdoc.com`, the test will print:
- CRC value (4 bytes)
- Machine state (16 bytes: memop, IY, IX, HL, DE, BC, AF, SP)

For each test iteration before/after the instruction.

### Option 3: Create Minimal Test Case

Write a tiny program that:
```assembly
    org 0x100
    ld bc, 0x1234
    ld de, 0x5678
    push bc
    push de
    pop hl   ; HL should now be 0x5678
    pop ix   ; IX should be 0x1234 (if Z80)
    halt
```

Then manually verify all register values are correct.

## Specific Files to Modify

### For Emulator Debug Output

**File**: `src/qkz80.cc`
**Location**: In the `step()` function (around line 300-400)
**Add**: Register dump code as shown above

**File**: `src/cpm_emulator.cc`
**Alternative**: Add debug output in the main execution loop

### Building Modified zexdoc

**File**: `/home/wohl/zexall-src/zexdoc.z80`
**Change**: Line 1112: `if 0` → `if 1`
**Command**: `pasmo zexdoc.z80 zexdoc.com` (after installing pasmo)
**Result**: New zexdoc.com with debug output enabled

## What to Look For

Once we have register dumps, compare:
1. Our emulator's register values
2. tnylpo's register values (it passes all tests)
3. Look for systematic differences:
   - Are registers swapped?
   - Are values off by constant amounts?
   - Are certain registers always wrong?
   - Is byte ordering (endianness) wrong?

## Documentation Created

- `INVESTIGATION_SUMMARY.md` - Complete analysis of discoveries
- `docs/DEBUGGING_INSIGHT_2025_11_15.md` - Technical details
- This file - Concrete next steps

## Key Insight from User

The user correctly pointed out that register pair numbers must align with opcode bit patterns. Analysis confirms the implementation is correct:
- 2-bit RP field gives values 0-3 (BC, DE, HL, SP/PSW)
- IX/IY don't use RP field - they use prefix bytes
- Internal enum values 6,7 for IX/IY don't conflict

The bug must be elsewhere in instruction execution or register access.
