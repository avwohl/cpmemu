# BUG FIX: PUSH/POP IX/IY
## Date: November 15, 2025

## Summary

Fixed PUSH IX, PUSH IY, POP IX, and POP IY instructions that were operating on HL instead of IX/IY registers.

This is part of a systematic fix for all 16-bit register pair instructions that use DD/FD prefix.

## Bug Description

**PUSH IX** and **POP IX** (and their IY equivalents) were operating on HL instead of IX/IY.

### Symptoms

When executing:
```assembly
LD IX,0x1111
PUSH IX        ; Should push 0x1111 to stack
```

**Expected:** Stack contains 0x1111, IX unchanged
**Actual:** Stack contains HL value, IX unchanged

### Discovery

Found through pattern analysis after fixing LD, INC, and DEC instructions. All instructions that extract register pair from opcode `(opcode >> 4) & 0x03` had the same bug.

## Root Cause

**File:** `src/qkz80.cc`
**Location:** Lines 1390-1422 (POP and PUSH instruction handlers)

### Problem Code:

```cpp
if ((opcode & 0xcf) == 0xc1) { // POP
  qkz80_uint8 rpair((opcode >> 4) & 0x3);
  if(rpair==regp_SP) {
    rpair=regp_AF;
  }
  // Missing: Check for DD/FD prefix!
  qkz80_uint16 pair_val(pop_word());
  set_reg16(pair_val,rpair);
  ...
}
```

### Analysis:

PUSH IX is encoded as:
- Prefix: 0xDD
- Opcode: 0xE5 (same as PUSH HL)

The code extracted bits 4-5 from opcode 0xE5:
- `(0xE5 >> 4) & 0x03 = 0x02 = regp_HL`

So it always used HL, ignoring the DD/FD prefix!

### Fix:

```cpp
if ((opcode & 0xcf) == 0xc1) { // POP
  qkz80_uint8 rpair((opcode >> 4) & 0x3);
  if(rpair==regp_SP) {
    rpair=regp_AF;
  }
  // If DD/FD prefix and register pair is HL (2), use IX/IY instead
  if ((has_dd_prefix || has_fd_prefix) && rpair == regp_HL) {
    rpair = active_hl;
  }
  qkz80_uint16 pair_val(pop_word());
  set_reg16(pair_val,rpair);
  ...
}

if ((opcode & 0xcf) == 0xc5) { // PUSH
  qkz80_uint8 rpair((opcode >> 4) & 0x3);
  if(rpair==regp_SP) {
    rpair=regp_AF;
  }
  // If DD/FD prefix and register pair is HL (2), use IX/IY instead
  if ((has_dd_prefix || has_fd_prefix) && rpair == regp_HL) {
    rpair = active_hl;
  }
  qkz80_uint16 val(get_reg16(rpair));
  push_word(val);
  ...
}
```

The `active_hl` variable is already computed at line 357:
```cpp
qkz80_uint8 active_hl = has_dd_prefix ? regp_IX : (has_fd_prefix ? regp_IY : regp_HL);
```

## Impact

This bug affected:
- **PUSH IX** - Push IX to stack
- **PUSH IY** - Push IY to stack
- **POP IX** - Pop from stack to IX
- **POP IY** - Pop from stack to IY
- Any code that saves/restores IX or IY (common in function calls)

## Verification

### Test Program

Created `/tmp/testpush.com` to test PUSH/POP for IX and IY:

```assembly
LD BC,0x1234
LD DE,0x5678
LD HL,0x9ABC
LD IX,0x1111
LD IY,0x2222
LD SP,0xDEF0

PUSH IX      ; Push 0x1111
PUSH IY      ; Push 0x2222
POP BC       ; BC = 0x2222
POP DE       ; DE = 0x1111
PUSH IX      ; Push 0x1111 again
POP HL       ; HL = 0x1111
HALT
```

### Test Results

**qkz80 (after fix):**
```
AFTER [6]: PC=0116 ... BC=1234 ... HL=9ABC SP=DEEE IX=1111 IY=2222  # PUSH IX
AFTER [7]: PC=0118 ... BC=1234 ... HL=9ABC SP=DEEC IX=1111 IY=2222  # PUSH IY
AFTER [8]: PC=0119 ... BC=2222 ... HL=9ABC SP=DEEE IX=1111 IY=2222  # POP BC (got IY)
AFTER [9]: PC=011A ... BC=2222 DE=1111 HL=9ABC SP=DEF0 IX=1111 IY=2222  # POP DE (got IX)
AFTER [10]: PC=011C ... BC=2222 DE=1111 HL=9ABC SP=DEEE IX=1111 IY=2222  # PUSH IX
AFTER [11]: PC=011D ... BC=2222 DE=1111 HL=1111 SP=DEF0 IX=1111 IY=2222  # POP HL (got IX)
```

**tnylpo (reference):**
```
AFTER [6]: PC=0116 ... BC=1234 ... HL=9ABC SP=DEEE IX=1111 IY=2222  # PUSH IX
AFTER [7]: PC=0118 ... BC=1234 ... HL=9ABC SP=DEEC IX=1111 IY=2222  # PUSH IY
AFTER [8]: PC=0119 ... BC=2222 ... HL=9ABC SP=DEEE IX=1111 IY=2222  # POP BC (got IY)
AFTER [9]: PC=011A ... BC=2222 DE=1111 HL=9ABC SP=DEF0 IX=1111 IY=2222  # POP DE (got IX)
AFTER [10]: PC=011C ... BC=2222 DE=1111 HL=9ABC SP=DEEE IX=1111 IY=2222  # PUSH IX
AFTER [11]: PC=011D ... BC=2222 DE=1111 HL=1111 SP=DEF0 IX=1111 IY=2222  # POP HL (got IX)
```

### Matches tnylpo: ✓ PERFECT!

## Files Modified

- `src/qkz80.cc` (lines 1390-1422): Added prefix check for PUSH/POP

## Related Fixes

This is the fourth category of bugs in the same pattern:
1. **LD IX/IY,nnnn** - Fixed in previous session (lines 873-884)
2. **INC IX/IY** - Fixed in previous session (lines 1111-1122)
3. **DEC IX/IY** - Fixed in previous session (lines 1124-1135)
4. **PUSH/POP IX/IY** - Fixed in this session (lines 1390-1422)

All bugs shared the same root cause: extracting register pair from opcode without checking DD/FD prefix.

## zexdoc Test Results

### Before All Fixes: 0/67 passing (0%)

### After All Fixes: 47/67 passing (70%)

**Newly passing test categories:**
- `<inc,dec> ix` - OK
- `<inc,dec> iy` - OK
- `ld <ix,iy>,nnnn` - OK
- `ld <ix,iy>,(nnnn)` - OK
- `ld (nnnn),<ix,iy>` - OK
- And many more!

**Still failing (20 tests):**
- `add ix,<bc,de,ix,sp>` - Need to check ADD instruction
- `add iy,<bc,de,iy,sp>` - Need to check ADD instruction
- `aluop a,nn` - ALU operations on immediate values
- `aluop a,<b,c,d,e,h,l,(hl),a>` - ALU operations on registers
- `aluop a,<ixh,ixl,iyh,iyl>` - ALU operations on IX/IY halves
- `aluop a,(<ix,iy>+1)` - ALU operations on indexed memory
- `<daa,cpl,scf,ccf>` - Decimal adjust and flag operations
- `<inc,dec> (<ix,iy>+1)` - INC/DEC on indexed memory
- `<inc,dec> ixh/ixl/iyh/iyl` - INC/DEC on IX/IY halves
- Various LD operations involving IX/IY halves and indexed memory

## Next Steps

1. Investigate ADD IX/IY instructions
2. Look into ALU operations (aluop tests)
3. Check indexed memory operations
4. Examine IX/IY half-register operations

The systematic pattern-based bug hunting approach has been very successful!
