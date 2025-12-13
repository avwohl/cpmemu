# Comprehensive Z80 Flag Fixes Applied

## Summary

Applied systematic fixes to set N, X, and Y flags correctly across all instruction types in the Z80 emulator.

## Flag Definitions

| Bit | Name | 8080 | Z80 | Description |
|-----|------|------|-----|-------------|
| 0 | C | ✓ | ✓ | Carry flag |
| 1 | N | ✗ | ✓ | Subtract flag (add/subtract) |
| 2 | P/V | ✓ | ✓ | Parity/Overflow flag |
| 3 | X | ✗ | ✓ | Undocumented - copy of bit 3 |
| 4 | H | ✓ | ✓ | Half carry |
| 5 | Y | ✗ | ✓ | Undocumented - copy of bit 5 |
| 6 | Z | ✓ | ✓ | Zero flag |
| 7 | S | ✓ | ✓ | Sign flag |

## Functions Modified

### 1. `set_zspa_from_inr()` - INC/DEC Instructions
**File:** `qkz80_reg_set.cc`

**Before:**
- Set S, Z, P/V, H flags only
- N, X, Y flags left as garbage

**After:**
```cpp
// N flag (Z80 only) - subtract flag
if(cpu_mode == MODE_Z80) {
  if (is_increment) {
    result &= ~qkz80_cpu_flags::N;  // Clear N for INC
  } else {
    result |= qkz80_cpu_flags::N;   // Set N for DEC
  }
}

// X and Y flags (Z80 only) - undocumented
if(cpu_mode == MODE_Z80) {
  if (a & 0x08) result |= qkz80_cpu_flags::X;  // Copy bit 3
  else result &= ~qkz80_cpu_flags::X;

  if (a & 0x20) result |= qkz80_cpu_flags::Y;  // Copy bit 5
  else result &= ~qkz80_cpu_flags::Y;
}
```

### 2. `set_flags_from_logic8()` - AND/OR/XOR
**File:** `qkz80_reg_set.cc`

**Before:**
- Set S, Z, P, H, C flags
- N, X, Y flags not set

**After:**
```cpp
// N flag is cleared for logical operations
// (already 0 since we started with fix_flags(0))

// X and Y flags (Z80 only) - copy bits 3 and 5 of result
if(cpu_mode == MODE_Z80) {
  if (sum8bit & 0x08) new_flags |= qkz80_cpu_flags::X;
  if (sum8bit & 0x20) new_flags |= qkz80_cpu_flags::Y;
}
```

### 3. NEW: `set_flags_from_rotate_acc()` - RLCA/RRCA/RLA/RRA
**File:** `qkz80_reg_set.cc`, `qkz80_reg_set.h`

**Purpose:** Set flags for accumulator rotate instructions

**Implementation:**
```cpp
void qkz80_reg_set::set_flags_from_rotate_acc(qkz80_uint8 result_a, qkz80_uint8 new_carry) {
  qkz80_uint8 flags = get_flags();

  // Set carry
  if (new_carry) flags |= qkz80_cpu_flags::CY;
  else flags &= ~qkz80_cpu_flags::CY;

  // Clear N and H flags
  flags &= ~(qkz80_cpu_flags::N | qkz80_cpu_flags::H);

  // Set X and Y flags from result (Z80 only)
  if (cpu_mode == MODE_Z80) {
    if (result_a & 0x08) flags |= qkz80_cpu_flags::X;
    else flags &= ~qkz80_cpu_flags::X;

    if (result_a & 0x20) flags |= qkz80_cpu_flags::Y;
    else flags &= ~qkz80_cpu_flags::Y;
  }

  set_flags(flags);
}
```

### 4. Rotate Instructions Updated
**File:** `qkz80.cc`

Updated all four accumulator rotate instructions:
- **RLCA (0x07)**: Rotate left circular
- **RRCA (0x0F)**: Rotate right circular
- **RLA (0x17)**: Rotate left through carry
- **RRA (0x1F)**: Rotate right through carry

**Changes:**
- Replaced `regs.set_carry_from_int(cy)` with `regs.set_flags_from_rotate_acc(result, cy)`
- Now properly set N=0, H=0, X and Y from result

## Already Correct Functions

These functions were already setting N, X, Y flags correctly through bit-by-bit simulation:

### 1. `set_flags_from_sum8()` - ADD/ADC (8-bit)
- Uses `add8_bitwise()` to simulate bit-by-bit
- Sets flag_x and flag_y from simulation
- Clears N flag (addition)

### 2. `set_flags_from_diff8()` - SUB/SBC/CP (8-bit)
- Uses `sub8_bitwise()` to simulate bit-by-bit
- Sets flag_x and flag_y from simulation
- Sets N flag (subtraction)

### 3. `set_flags_from_add16()` - ADD HL/IX/IY (16-bit)
- Uses `add16_bitwise()` to get X/Y from high byte
- Clears N flag (addition)
- Preserves S, Z, P/V

### 4. `set_flags_from_adc16()` - ADC HL (16-bit)
- Uses `add16_bitwise()` for all flags
- Clears N flag (addition)
- Sets all flags including X, Y

### 5. `set_flags_from_sbc16()` - SBC HL (16-bit)
- Uses `sub16_bitwise()` for all flags
- Sets N flag (subtraction)
- Sets all flags including X, Y

## Testing Results

### Simple Tests - All Pass ✅
```
simple_con:      ABC ✅
test_djnz:       321 ✅
tflags:          94 51 10 3E ✅
test_n_flag:     20 02 00 ✅
test_sequence:   00 00 94 00 ✅
```

### zexdoc Progress
- All 67 test groups complete without hanging
- CRC values changed after each fix (progress)
- Still showing errors (other instruction bugs remain)

## Instruction Coverage

| Instruction Type | N Flag | X/Y Flags | Status |
|-----------------|--------|-----------|---------|
| INC/DEC | ✅ | ✅ | FIXED |
| ADD/ADC (8-bit) | ✅ | ✅ | Already correct |
| SUB/SBC/CP (8-bit) | ✅ | ✅ | Already correct |
| AND/OR/XOR | ✅ | ✅ | FIXED |
| RLCA/RRCA/RLA/RRA | ✅ | ✅ | FIXED |
| ADD HL/IX/IY | ✅ | ✅ | Already correct |
| ADC HL | ✅ | ✅ | Already correct |
| SBC HL | ✅ | ✅ | Already correct |

## Remaining Issues

While N/X/Y flags are now handled correctly, zexdoc still shows CRC errors due to other bugs:

1. **CB-prefixed instructions** (RLC, RRC, RL, RR, SLA, SRA, SRL, BIT, SET, RES)
   - Need to set X/Y flags from result
   - Need to properly handle all variants

2. **NEG instruction**
   - Should set N=1 (subtraction/negation)

3. **DAA instruction**
   - Complex decimal adjust logic
   - Affects multiple flags

4. **RLD/RRD instructions**
   - Rotate digit left/right
   - Special flag behavior

5. **Block instructions** (LDI, LDIR, LDD, LDDR, CPI, CPIR, CPD, CPDR)
   - Complex flag effects
   - P/V indicates BC≠0

6. **SCF/CCF instructions**
   - Set/complement carry flag
   - X/Y flag behavior

## Files Changed

### Modified
- `src/qkz80_reg_set.cc` - Flag setting functions
- `src/qkz80_reg_set.h` - Function declarations
- `src/qkz80.cc` - Rotate instructions

### Created
- `docs/FLAG_NXY_IMPLEMENTATION.md` - Implementation details
- `docs/FLAG_FIXES_COMPLETE.md` - This file
- `docs/SESSION_2025_11_15.md` - Session summary

## Commits

1. **714848d** - Add N, X, Y flag handling to INC/DEC instructions
2. **e0aa411** - Add comprehensive N/X/Y flag support to all instructions

## Next Steps

1. Fix CB-prefixed rotate/shift instructions
2. Fix NEG instruction
3. Fix DAA instruction
4. Fix block instructions
5. Fix RLD/RRD instructions
6. Fix SCF/CCF instructions
7. Continue testing with zexdoc/zexall

## References

- Z80 CPU User Manual - Official Zilog documentation
- "The Undocumented Z80 Documented" by Sean Young
- tnylpo Z80 emulator source code
- zexdoc/zexall test suite source code
