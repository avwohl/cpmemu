# Flag Debugging Progress

## Latest Status (2025-11-15)

### Fixes Applied

1. **fix_flags() made CPU mode aware** (commit 8e9de71)
   - In Z80 mode: Returns flags unchanged (N, X, Y are meaningful)
   - In 8080 mode: Forces bit 1=1, clears bits 3,5 per 8080 spec
   - CRITICAL FIX: Was forcing 8080 behavior in all modes

2. **Removed redundant fix_flags() calls**
   - set_flags_from_adc16() and set_flags_from_sbc16() were calling fix_flags() twice
   - set_flags() already calls fix_flags(), no need to call manually

3. **Fixed get_flags() to not call fix_flags()** (commit 0770143)
   - get_flags() should return RAW flags from AF register
   - Only set_flags() should apply fix_flags() when storing
   - Prevents double-application of flag modifications

### Current Test Results

Running zexdoc with 5B instruction limit:

```
<adc,sbc> hl,<bc,de,hl,sp>....
  ERROR **** crc expected:f8b4eaa9 found:987ecc68

add hl,<bc,de,hl,sp>..........
  ERROR **** crc expected:89fdb635 found:2805a593

add ix,<bc,de,ix,sp>..........
  ERROR **** crc expected:c133790b found:c4442b77

add iy,<bc,de,iy,sp>..........
  ERROR **** crc expected:e8817b9e found:654031bf

aluop a,nn....................
  ERROR **** crc expected:48799360 found:f96ba9ad

aluop a,<b,c,d,e,h,l,(hl),a>..
  ERROR **** crc expected:fe43b016 found:92f008a8

aluop a,<ixh,ixl,iyh,iyl>.....
  ERROR **** crc expected:a4026d5a found:48544e0c
```

### CRC Evolution

Tracking how CRCs changed with each fix:

**ADC/SBC HL:**
- Initial: 7fe74f4b
- After bit-by-bit simulation: 2590a9a3
- After proper ADC/SBC split: 2896c86e
- After fix_flags mode fix: 987ecc68 ← current

**ADD HL:**
- After bit-by-bit simulation: 7871f3da
- After fix_flags mode fix: 2805a593 ← current

Each fix changed the CRCs, confirming impact, but not matching expected values.

## Implementation Details

### Bit-by-bit Flag Simulation (from tnylpo)

Based on tnylpo cpu.c lines 768-786:

```cpp
for (int i = 0; i < 16; i++) {
    result |= (s1 ^ s2 ^ cy) & ma;
    cy = ((s2 & cy) | (s1 & (s2 | cy))) & ma;

    if (i == 11) flag_h = (cy != 0);  // Half-carry from bit 11
    if (i == 14) c14 = (cy != 0);      // Save C14 for overflow
    if (i == 15) flag_c = (cy != 0);   // Carry from bit 15

    cy <<= 1;
    ma <<= 1;
}

flag_v = flag_c ^ c14;              // Overflow = C15 XOR C14
flag_x = (result & 0x0800) != 0;   // Bit 11 (undocumented)
flag_y = (result & 0x2000) != 0;   // Bit 13 (undocumented)
flag_z = (result == 0);
flag_s = (result & 0x8000) != 0;
```

### ADD HL Flag Handling

Per Z80 spec and tnylpo implementation:

1. **Preserve S, Z, P/V** - Save before operation, restore after
2. **Clear N** - This is addition, not subtraction
3. **Set C, H from bit-simulation** - Carry from bit 15, half-carry from bit 11
4. **Set X, Y from result** - Undocumented flags from bits 11, 13

Our implementation in set_flags_from_add16():
```cpp
// Save S, Z, P/V
qkz80_uint8 preserve_mask = S | Z | P;
qkz80_uint8 preserved = flags & preserve_mask;

// Run bit-simulation
add16_bitwise(val1, val2, 0, flag_h, flag_c, ...);

// Build new flags
flags &= ~N;  // Clear N
// Set C, H, X, Y from simulation
...

// Restore S, Z, P/V
flags = (flags & ~preserve_mask) | preserved;
```

This matches tnylpo's approach exactly.

## Comparison with tnylpo

### What tnylpo does (inst_dad, lines 892-927):

```c
int old_s = flag_s, old_z = flag_z, old_p = flag_p;
...
set_hl(add16(internal, s, 0));  // add16() sets ALL flags
flag_s = old_s;  // Restore
flag_z = old_z;
flag_p = old_p;
```

### What we do:

```cpp
qkz80_uint8 preserved = flags & (S|Z|P);  // Save
add16_bitwise(...);  // Get flag values
// Build flags with N cleared, C/H/X/Y set
flags = (flags & ~(S|Z|P)) | preserved;  // Restore
```

**Logically equivalent** - both preserve S,Z,P/V and set other flags from simulation.

## Remaining Issues

### Known Differences from tnylpo:
1. **Flag storage**: tnylpo uses separate int variables, we pack into byte
   - Shouldn't matter if pack/unpack is correct
2. **Result calculation**: tnylpo calculates result in add16(), we calculate separately
   - Shouldn't matter if both use same bit-simulation

### Possible Issues:
1. **Flag bit positions** - Are our flag constants correct?
2. **Mask application** - Is preserve/restore logic correct?
3. **8-bit ALU ops** - ALL aluop tests failing, might have separate issue
4. **Instruction decoding** - Could be calling wrong flag functions?

## Next Steps

1. **Verify flag bit positions** - Double-check qkz80_cpu_flags.h constants
2. **Test with simple program** - Create minimal ADD HL test, compare with tnylpo
3. **Add detailed logging** - Print exact flag values for first few operations
4. **Check 8-bit ALU** - The aluop failures suggest broader issue
5. **Review instruction dispatch** - Verify we're calling set_flags_from_add16 correctly

## Test Environment

- Emulator: /home/wohl/qkz80/src/cpm_emulator
- Test: /home/wohl/qkz80/tests/zexdoc.com
- Reference: /home/wohl/z80/tnylpo (passes all tests)
- Instruction limit: 5,000,000,000 (5B)
- Progress reporting: Every 100M instructions

## Flag Mask Reference (from ZEXDOC_MASK_ANALYSIS.md)

- **ADD HL/IX/IY**: mask 0xC7 = checks S,Z,P/V,N,C (ignores H,X,Y)
- **ADC/SBC HL**: mask 0xC7 = same as ADD HL
- **8-bit ALU**: mask 0xD7 = checks S,Z,H,P/V,N,C (ignores X,Y)

Even though H is ignored for ADD HL in zexdoc, tnylpo still passes. This means
the OTHER flags (S,Z,P/V,N,C) must be exactly correct. Our failures indicate
these core flags are wrong, not just the undocumented ones.
