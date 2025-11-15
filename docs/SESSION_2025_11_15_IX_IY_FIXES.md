# Session: IX/IY Register Pair Bug Fixes
## Date: November 15, 2025 (Continued Session)

## Summary

Systematically found and fixed 5 categories of bugs related to DD/FD prefix handling for IX/IY register pairs.

**Result: Went from 0/67 zexdoc tests passing to 49/67 passing (73%)**

## Root Cause Pattern

All bugs shared the same root cause:

The code extracts register pair index from opcode using `(opcode >> 4) & 0x03`:
- 0 = BC (regp_BC = 0)
- 1 = DE (regp_DE = 1)
- 2 = HL (regp_HL = 2)
- 3 = SP or AF (regp_SP = 3, regp_AF = 4)

When DD/FD prefix is present, opcode 0x21 (for example) still encodes HL (value 2 in bits 4-5), but should be interpreted as IX/IY instead.

**The fix pattern:** Check `has_dd_prefix` or `has_fd_prefix`, and if the extracted register pair is `regp_HL`, use `active_hl` instead.

```cpp
// If DD/FD prefix and register pair is HL (2), use IX/IY instead
if ((has_dd_prefix || has_fd_prefix) && rpair == regp_HL) {
  rpair = active_hl;
}
```

The `active_hl` variable (computed at line 357) selects the correct register:
```cpp
qkz80_uint8 active_hl = has_dd_prefix ? regp_IX : (has_fd_prefix ? regp_IY : regp_HL);
```

## Bugs Found and Fixed

### 1. LD IX/IY,nnnn (Load Immediate)

**File:** `src/qkz80.cc`, lines 873-884
**Opcode:** 0xDD 0x21 (LD IX,nnnn), 0xFD 0x21 (LD IY,nnnn)

**Before:** Loaded values into HL instead of IX/IY
**After:** Correctly loads into IX/IY

**Impact:** IX and IY could never be initialized with immediate values!

### 2. INC IX/IY (Increment 16-bit)

**File:** `src/qkz80.cc`, lines 1111-1122
**Opcode:** 0xDD 0x23 (INC IX), 0xFD 0x23 (INC IY)

**Before:** Incremented HL instead of IX/IY
**After:** Correctly increments IX/IY

### 3. DEC IX/IY (Decrement 16-bit)

**File:** `src/qkz80.cc`, lines 1124-1135
**Opcode:** 0xDD 0x2B (DEC IX), 0xFD 0x2B (DEC IY)

**Before:** Decremented HL instead of IX/IY
**After:** Correctly decrements IX/IY

### 4. PUSH IX/IY and POP IX/IY

**File:** `src/qkz80.cc`, lines 1390-1422
**Opcodes:**
- 0xDD 0xE5 (PUSH IX), 0xFD 0xE5 (PUSH IY)
- 0xDD 0xE1 (POP IX), 0xFD 0xE1 (POP IY)

**Before:** Pushed/popped HL instead of IX/IY
**After:** Correctly pushes/pops IX/IY

**Impact:** Any code that saves/restores IX or IY (common in function calls) would fail!

### 5. ADD IX/IY,rr (16-bit Addition)

**File:** `src/qkz80.cc`, lines 1137-1156
**Opcodes:**
- 0xDD 0x09 (ADD IX,BC), 0xDD 0x19 (ADD IX,DE), etc.
- 0xDD 0x29 (ADD IX,IX) - adds IX to itself!

**Before:** When source register was HL, used HL instead of IX/IY
**After:** Correctly interprets DD 0x29 as "ADD IX,IX"

**Note:** This bug was subtle - the destination was already correct (using `active_hl`), but the source register also needed the prefix check when it was HL.

## Test Results

### Initial State (Before Fixes)
- **zexdoc tests passing:** 0/67 (0%)
- All tests involving IX/IY failed
- Many basic register operations also failed due to inability to initialize IX/IY

### After LD/INC/DEC/PUSH/POP Fixes
- **zexdoc tests passing:** 47/67 (70%)
- All basic IX/IY operations now work
- All basic LD, INC, DEC operations pass

### After ADD IX/IY Fix
- **zexdoc tests passing:** 49/67 (73%)
- ADD IX and ADD IY tests now pass

## Verification Method

Created test programs for each instruction category:
- `/tmp/testld16.com` - LD rr,nnnn tests
- `/tmp/testinc16.com` - INC/DEC tests
- `/tmp/testpush.com` - PUSH/POP tests

Compared execution with tnylpo (reference emulator) using debug tracing:
```bash
env QKZ80_DEBUG=1 ./cpm_emulator test.com 2>&1 > qkz80_output.txt
env DEBUG_TRACE=1 tnylpo test.com 2>&1 > tnylpo_output.txt
diff -y qkz80_output.txt tnylpo_output.txt
```

All test outputs matched tnylpo perfectly after fixes!

## Remaining Failures (18 tests)

The following test categories still fail:

1. **aluop a,nn** - ALU operations with immediate values
2. **aluop a,<b,c,d,e,h,l,(hl),a>** - ALU operations on registers
3. **aluop a,<ixh,ixl,iyh,iyl>** - ALU operations on IX/IY halves
4. **aluop a,(<ix,iy>+1)** - ALU operations on indexed memory
5. **<daa,cpl,scf,ccf>** - Decimal adjust and complement operations
6. **<inc,dec> (<ix,iy>+1)** - INC/DEC on indexed memory
7. **<inc,dec> ixh/ixl/iyh/iyl** - INC/DEC on IX/IY half-registers
8. **ld <b,c,d,e>,(<ix,iy>+1)** - Load from indexed memory to registers
9. **ld <h,l>,(<ix,iy>+1)** - Load from indexed memory to H/L
10. **ld a,(<ix,iy>+1)** - Load from indexed memory to A
11. **ld <ixh,ixl,iyh,iyl>,nn** - Load immediate to IX/IY halves
12. **ld <bcdexya>,<bcdexya>** - LD between registers involving IX/IY halves
13. **ld (<ix,iy>+1),<b,c,d,e>** - Store registers to indexed memory
14. **ld (<ix,iy>+1),<h,l>** - Store H/L to indexed memory
15. **ld (<ix,iy>+1),a** - Store A to indexed memory

## Pattern Analysis

Most remaining failures involve:
1. **Indexed memory operations** - `(IX+d)` and `(IY+d)` addressing
2. **Half-register operations** - IXH, IXL, IYH, IYL
3. **ALU operations** - ADD, SUB, AND, OR, XOR, CP with various operands
4. **Flag operations** - DAA, CPL, SCF, CCF

These likely require different fixes than the register pair extraction pattern.

## Files Modified

### `/home/wohl/qkz80/src/qkz80.cc`
1. **Lines 873-884:** Fixed LD IX/IY,nnnn
2. **Lines 1111-1122:** Fixed INC IX/IY
3. **Lines 1124-1135:** Fixed DEC IX/IY
4. **Lines 1137-1156:** Fixed ADD IX/IY,rr
5. **Lines 1390-1422:** Fixed PUSH/POP IX/IY

All fixes follow the same pattern - check DD/FD prefix and use `active_hl` when needed.

## Next Steps

1. **Investigate ALU operations** - The "aluop" tests suggest systematic issues with arithmetic/logic operations
2. **Check indexed memory operations** - `(IX+d)` and `(IY+d)` addressing mode
3. **Examine half-register operations** - IXH, IXL, IYH, IYL handling
4. **Review flag operations** - DAA, CPL, SCF, CCF

The systematic pattern-based approach was very successful for register pair operations. Need to find similar patterns for the remaining failures.

## Progress Timeline

| Milestone | Tests Passing | Percentage |
|-----------|---------------|------------|
| Initial state | 0/67 | 0% |
| After LD/INC/DEC fixes | 45/67 | 67% |
| After PUSH/POP fix | 47/67 | 70% |
| After ADD IX/IY fix | 49/67 | 73% |

**Total improvement: +49 tests in one session!**
