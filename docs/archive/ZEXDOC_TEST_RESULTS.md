# Zexdoc Test Results

## Status: IN PROGRESS

Successfully implemented full Z80 instruction set. Tests are running but showing errors in flag calculations.

## Current Test Results

### ❌ FAILING TESTS

1. **<adc,sbc> hl,<bc,de,hl,sp>**
   - Expected CRC: f8b4eaa9
   - Found CRC: adb78dd9
   - Issue: 16-bit ADC/SBC flag calculation incorrect

2. **add hl,<bc,de,hl,sp>**
   - Expected CRC: 89fdb635
   - Found CRC: 7871f3da
   - Issue: 16-bit ADD flag calculation incorrect (carry flag)

3. **add ix,<bc,de,ix,sp>**
   - Test running (500M+ instructions executed)
   - Likely same issue as ADD HL

## Issues Identified

### 1. 16-bit Arithmetic Flag Handling

The Z80 handles flags differently than 8080 for 16-bit operations:

**ADC HL,ss / SBC HL,ss (ED prefix)**
- Should set: S, Z, H, P/V (overflow), N, C
- Currently using `set_flags_from_sum16()` which only handles carry

**ADD HL/IX/IY,ss**
- Should affect: H, N (reset), C
- Should NOT affect: S, Z, P/V
- Currently may be setting too many flags

### 2. Half-Carry (H flag) for 16-bit ops

16-bit operations need proper half-carry calculation between bits 11 and 12, not bits 3 and 4.

## Next Steps

1. Fix `set_flags_from_sum16()` to properly handle all 16-bit flags
2. Create `set_flags_from_diff16()` for SBC HL,ss
3. Add proper overflow detection for ADC/SBC HL
4. Fix ADD HL/IX/IY to only affect H, N, C flags
5. Re-run tests

## Test Progress

- ✅ Tests are executing (no crashes/hangs)
- ✅ Prefix handling working
- ✅ Instruction decoding working
- ❌ Flag calculations need fixes

## Files to Fix

- `src/qkz80_reg_set.cc` - Flag setting functions
- `src/qkz80.cc` - ADC/SBC HL and ADD HL/IX/IY implementations
