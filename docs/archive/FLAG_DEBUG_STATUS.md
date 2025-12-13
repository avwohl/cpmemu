# Flag Calculation Debugging Status

## Date: 2025-11-15

## Summary

Implemented bit-by-bit flag simulation for 16-bit arithmetic operations based on tnylpo reference implementation. Tests still showing CRC mismatches, indicating further debugging needed.

## Current Test Results

Running `timeout 300 ./src/cpm_emulator tests/zexdoc.com`:

```
<adc,sbc> hl,<bc,de,hl,sp>....ERROR **** crc expected:f8b4eaa9 found:2896c86e
add hl,<bc,de,hl,sp>..........ERROR **** crc expected:89fdb635 found:7871f3da
add ix,<bc,de,ix,sp>..........ERROR **** crc expected:c133790b found:94307d3e
add iy,<bc,de,iy,sp>..........ERROR **** crc expected:e8817b9e found:353467f6
aluop a,nn....................ERROR **** crc expected:48799360 found:9e922f9e
aluop a,<b,c,d,e,h,l,(hl),a>..ERROR **** crc expected:fe43b016 found:cf762c86
aluop a,<ixh,ixl,iyh,iyl>.....ERROR **** crc expected:a4026d5a found:45120082
```

## What We've Implemented

### 1. Bit-by-bit Flag Simulation (src/qkz80_reg_set.cc:192-267)

Based on tnylpo cpu.c:768-813:

```cpp
static qkz80_uint16 add16_bitwise(...) {
  for (int i = 0; i < 16; i++) {
    result |= (s1 ^ s2 ^ cy) & ma;
    cy = ((s2 & cy) | (s1 & (s2 | cy))) & ma;

    if (i == 11) flag_h = (cy != 0) ? 1 : 0;  // Half-carry from bit 11
    if (i == 14) c14 = (cy != 0) ? 1 : 0;      // Save carry from bit 14
    if (i == 15) flag_c = (cy != 0) ? 1 : 0;   // Carry from bit 15
    ...
  }
  flag_v = flag_c ^ c14;  // Overflow = C15 XOR C14
}
```

### 2. Separate ADC/SBC Functions

- `set_flags_from_add16()` - For ADD HL/IX/IY (preserves S, Z, P/V)
- `set_flags_from_adc16()` - For ADC HL (uses addition, sets all flags)
- `set_flags_from_sbc16()` - For SBC HL (uses subtraction, sets all flags)

### 3. Undocumented Flag Support

X (bit 3) and Y (bit 5) flags set from bits 11 and 13 of 16-bit result.

Note: Zexdoc tests documented behavior only, so X/Y shouldn't affect results.

## Possible Remaining Issues

### 1. 8-Bit ALU Operations
All 8-bit ALU tests are also failing. Need to investigate:
- Overflow (P/V) flag calculation for arithmetic ops
- Half-carry calculation
- Sign/Zero/Parity flags

### 2. Flag Calculation Edge Cases
- Interaction between different flag types (P vs V)
- Proper handling of N flag in all contexts
- Fix_flags() may be masking issues

### 3. Instruction Implementation
- Verify we're calling the right flag functions from qkz80.cc
- Check if DAD (8080 ADD HL) vs ADD HL (Z80) handled correctly

## CRC Changes Timeline

- **Initial (before bit-by-bit)**: ADC/SBC = `7fe74f4b`
- **After first bit-by-bit attempt**: ADC/SBC = `2590a9a3`
- **After proper ADC/SBC split**: ADC/SBC = `2896c86e`
- **Target**: ADC/SBC = `f8b4eaa9`

The CRCs are changing with our fixes, showing we're affecting behavior, but not yet matching expected values.

## Next Steps

1. **Test with tnylpo**: Verify that tnylpo passes these tests (user suggested this)
2. **Debug 8-bit ops**: Focus on `aluop a,nn` since all ALU ops failing
3. **Simple test case**: Create minimal program that tests one operation
4. **Trace execution**: Add detailed logging to see exact flag values
5. **Compare with spec**: Re-read Z80 manual flag descriptions

## Reference Implementation Analysis

From `/home/wohl/z80/tnylpo/cpu.c`:

- **add16()** (line 768): Bit-by-bit simulation, sets S/Z/P/V/X/Y
- **sub16()** (line 794): Same approach for subtraction
- **inst_dad()** (line 892): Calls add16(), then **restores** S/Z/P
- **inst_adchl()** (line 1732): Calls add16(), keeps all flags
- **inst_sbchl()** (line 1749): Calls sub16(), keeps all flags

This matches our implementation approach.

## Key Insight from tnylpo

Flag register layout (cpu.c:1302-1309):
```
Bit 7 (0x80): S (sign)
Bit 6 (0x40): Z (zero)
Bit 5 (0x20): Y (undocumented)
Bit 4 (0x10): H (half carry)
Bit 3 (0x08): X (undocumented)
Bit 2 (0x04): P/V (parity/overflow)
Bit 1 (0x02): N (add/subtract)
Bit 0 (0x01): C (carry)
```

Matches our qkz80_cpu_flags.h definitions.
