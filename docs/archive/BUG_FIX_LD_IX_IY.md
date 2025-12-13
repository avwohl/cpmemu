# CRITICAL BUG FIX: LD IX,nnnn and LD IY,nnnn
## Date: November 15, 2025

## Bug Description

**LD IX,nnnn** and **LD IY,nnnn** instructions were loading values into HL instead of IX/IY registers.

### Symptoms

When executing:
```assembly
LD IX,0x1111  ; Should load 0x1111 into IX
```

**Expected:** IX=0x1111, HL unchanged
**Actual:** IX=0x0000, HL=0x1111

Same problem occurred with LD IY,nnnn.

### Discovery

Found using debug tracing comparison between qkz80 and tnylpo:

**qkz80 (WRONG):**
```
AFTER [4]: PC=0110 AF=0000 BC=1234 DE=5678 HL=1111 SP=DEF0 IX=0000 IY=0000 [--------]
AFTER [5]: PC=0114 AF=0000 BC=1234 DE=5678 HL=2222 SP=DEF0 IX=0000 IY=0000 [--------]
```

**tnylpo (CORRECT):**
```
AFTER [4]: PC=0110 AF=0000 BC=1234 DE=5678 HL=9ABC SP=DEF0 IX=1111 IY=0000 [--------]
AFTER [5]: PC=0114 AF=0000 BC=1234 DE=5678 HL=9ABC SP=DEF0 IX=1111 IY=2222 [--------]
```

## Root Cause

**File:** `src/qkz80.cc`
**Location:** Lines 873-880 (LXI instruction handler)

### Problem Code:

```cpp
if ((opcode & 0xcf) == 0x01) { // LXI
  qkz80_uint16 addr(pull_word_from_opcode_stream());
  qkz80_uint8 rpair = ((opcode >> 4) & 0x03);  // Extract bits 4-5
  set_reg16(addr,rpair);
  ...
}
```

### Analysis:

The instruction `LD IX,nnnn` is encoded as:
- Prefix: 0xDD
- Opcode: 0x21 (same as LD HL,nnnn)
- Data: nnnn (16-bit immediate value)

The code extracted bits 4-5 from opcode 0x21:
- `(0x21 >> 4) & 0x03 = 0x02 = regp_HL`

So it always used HL, ignoring the DD/FD prefix!

### Fix:

```cpp
if ((opcode & 0xcf) == 0x01) { // LXI
  qkz80_uint16 addr(pull_word_from_opcode_stream());
  qkz80_uint8 rpair = ((opcode >> 4) & 0x03);
  // If DD/FD prefix and register pair is HL (2), use IX/IY instead
  if ((has_dd_prefix || has_fd_prefix) && rpair == regp_HL) {
    rpair = active_hl;
  }
  set_reg16(addr,rpair);
  ...
}
```

The `active_hl` variable is already computed at line 357:
```cpp
qkz80_uint8 active_hl = has_dd_prefix ? regp_IX : (has_fd_prefix ? regp_IY : regp_HL);
```

## Impact

This bug would affect:
- **LD IX,nnnn** - Load immediate into IX
- **LD IY,nnnn** - Load immediate into IY
- Any code that initializes IX or IY registers
- All zexdoc tests that use IX or IY

Essentially, **IX and IY could never be properly initialized** with immediate values!

## Verification

### Test Program

Created `/tmp/testld16.com` to test LD instructions for all register pairs.

### Before Fix:
```
AFTER [4]: PC=0110 ... HL=1111 ... IX=0000 IY=0000  # IX not loaded!
AFTER [5]: PC=0114 ... HL=2222 ... IX=0000 IY=0000  # IY not loaded!
```

### After Fix:
```
AFTER [4]: PC=0110 ... HL=9ABC ... IX=1111 IY=0000  # IX loaded correctly!
AFTER [5]: PC=0114 ... HL=9ABC ... IX=1111 IY=2222  # IY loaded correctly!
```

### Matches tnylpo: ✓ PERFECT!

## Files Modified

- `src/qkz80.cc` (lines 873-884): Added prefix check for LD rr,nnnn

## Related Bugs

This same pattern might affect other instructions that use DD/FD prefix with HL-based opcodes:
- ADD HL,rr → might need similar fix for ADD IX,rr and ADD IY,rr
- INC HL / DEC HL → might need similar fix for INC IX/IY and DEC IX/IY
- Other 16-bit operations on HL that should work with IX/IY

Need to audit all uses of register pair extraction from opcodes to ensure DD/FD prefixes are handled correctly!

## Commit Message

```
Fix LD IX,nnnn and LD IY,nnnn loading into HL instead of IX/IY

LD IX,nnnn (0xDD 0x21) and LD IY,nnnn (0xFD 0x21) were incorrectly
loading values into HL because the code only looked at opcode 0x21
(which encodes HL in bits 4-5) without checking the DD/FD prefix.

Fixed by checking has_dd_prefix/has_fd_prefix and using active_hl
when the extracted register pair is HL.

This is a critical bug that prevented IX and IY from being properly
initialized in any Z80 program.
```

## Testing Status

- ✓ Simple LD test passes
- ✓ Output matches tnylpo exactly
- ✗ zexdoc tests: Still 0/67 passing (other bugs remain)

The fix is correct, but more bugs need to be found and fixed.
