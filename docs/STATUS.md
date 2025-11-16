# Z80 CPU Emulator - Test Status

## zexdoc Test Results: 63/67 Passing (94%)

### Passing Tests (63) ✅

#### 16-bit Arithmetic
- ✅ `<adc,sbc> hl,<bc,de,hl,sp>`
- ✅ `add hl,<bc,de,hl,sp>`
- ✅ `add ix,<bc,de,ix,sp>`
- ✅ `add iy,<bc,de,iy,sp>`

#### Bit Operations
- ✅ `bit n,(<ix,iy>+1)`
- ✅ `bit n,<b,c,d,e,h,l,(hl),a>`

#### Block Operations
- ✅ `cpd<r>` - Compare and decrement
- ✅ `cpi<r>` - Compare and increment
- ✅ `ldd<r> (1)` - Load and decrement
- ✅ `ldd<r> (2)`
- ✅ `ldi<r> (1)` - Load and increment
- ✅ `ldi<r> (2)`

#### Special Instructions
- ✅ `<daa,cpl,scf,ccf>` - **FIXED!** Decimal adjust, complement, flag operations
- ✅ `neg` - Negate accumulator
- ✅ `<rrd,rld>` - Rotate digit operations

#### Increment/Decrement
- ✅ `<inc,dec> a`
- ✅ `<inc,dec> b`
- ✅ `<inc,dec> bc`
- ✅ `<inc,dec> c`
- ✅ `<inc,dec> d`
- ✅ `<inc,dec> de`
- ✅ `<inc,dec> e`
- ✅ `<inc,dec> h`
- ✅ `<inc,dec> hl`
- ✅ `<inc,dec> ix`
- ✅ `<inc,dec> iy`
- ✅ `<inc,dec> l`
- ✅ `<inc,dec> (hl)`
- ✅ `<inc,dec> sp`
- ✅ `<inc,dec> (<ix,iy>+1)`
- ✅ `<inc,dec> ixh`
- ✅ `<inc,dec> ixl`
- ✅ `<inc,dec> iyh`
- ✅ `<inc,dec> iyl`

#### Load Instructions
- ✅ `ld <bc,de>,(nnnn)`
- ✅ `ld hl,(nnnn)`
- ✅ `ld sp,(nnnn)`
- ✅ `ld <ix,iy>,(nnnn)`
- ✅ `ld (nnnn),<bc,de>`
- ✅ `ld (nnnn),hl`
- ✅ `ld (nnnn),sp`
- ✅ `ld (nnnn),<ix,iy>`
- ✅ `ld <bc,de,hl,sp>,nnnn`
- ✅ `ld <ix,iy>,nnnn`
- ✅ `ld a,<(bc),(de)>`
- ✅ `ld <b,c,d,e,h,l,(hl),a>,nn`
- ✅ `ld (<ix,iy>+1),nn`
- ✅ `ld <b,c,d,e>,(<ix,iy>+1)`
- ✅ `ld <h,l>,(<ix,iy>+1)`
- ✅ `ld a,(<ix,iy>+1)`
- ✅ `ld <ixh,ixl,iyh,iyl>,nn`
- ✅ `ld <bcdehla>,<bcdehla>`
- ✅ `ld <bcdexya>,<bcdexya>`
- ✅ `ld a,(nnnn) / ld (nnnn),a`
- ✅ `ld (<ix,iy>+1),<b,c,d,e>`
- ✅ `ld (<ix,iy>+1),<h,l>`
- ✅ `ld (<ix,iy>+1),a`
- ✅ `ld (<bc,de>),a`

#### Rotate/Shift Operations
- ✅ `<rlca,rrca,rla,rra>` - Accumulator rotates
- ✅ `shf/rot (<ix,iy>+1)` - Shifts and rotates with indexed addressing
- ✅ `shf/rot <b,c,d,e,h,l,(hl),a>` - Shifts and rotates

#### Set/Reset Bits
- ✅ `<set,res> n,<bcdehl(hl)a>`
- ✅ `<set,res> n,(<ix,iy>+1)`

### Failing Tests (4) ❌

All failures are ALU (arithmetic/logic) operations with different addressing modes:

1. ❌ `aluop a,nn` - ALU ops with immediate values
   - CRC expected: 48799360, found: fb9db85a

2. ❌ `aluop a,<b,c,d,e,h,l,(hl),a>` - ALU ops with registers
   - CRC expected: fe43b016, found: 334649bb

3. ❌ `aluop a,<ixh,ixl,iyh,iyl>` - ALU ops with IX/IY high/low
   - CRC expected: a4026d5a, found: 6e027aa3

4. ❌ `aluop a,(<ix,iy>+1)` - ALU ops with indexed memory
   - CRC expected: e849676e, found: 14e03297

**Note**: "aluop" includes ADD, ADC, SUB, SBC, AND, OR, XOR, CP instructions

## Recent Progress

### Session Summary
- **Starting point**: 62/67 tests passing
- **Current**: 63/67 tests passing (94% pass rate)
- **Achievement**: Fixed DAA (Decimal Adjust Accumulator) instruction

### DAA Fix Details
The DAA instruction was completely rewritten using tnylpo's proven implementation:

**Key Changes**:
1. **Correction calculation**: Uses comprehensive lookup table based on:
   - Current carry flag state
   - Current half-carry flag state
   - High and low nibble values

2. **H flag calculation**: Based on low nibble value and N flag, NOT from the correction operation
   - After addition (N=0): `H = (low_nibble >= 0xA) ? 1 : 0`
   - After subtraction (N=1): More complex logic based on old H flag

3. **C flag calculation**: Follows Z80 specification for BCD overflow

**Location**: `src/qkz80.cc:1720-1784`

## Analysis of Remaining Failures

All 4 failing tests share the same characteristic: they test ALU operations. This suggests a systematic issue with flag setting for arithmetic/logic operations, rather than individual instruction bugs.

### Possible Issues
1. **Flag calculation**: While bit-by-bit addition/subtraction appears correct (verified with test cases), there may be edge cases not covered
2. **CPU mode handling**: X/Y flags may be set unconditionally when they should be Z80-only
3. **P/V flag**: Different behavior needed for arithmetic (overflow) vs logic (parity) operations
4. **Undocumented behavior**: zexdoc may test undocumented flag combinations

### Investigation Approach
Since fixing DAA by adopting tnylpo's exact logic was successful, the same approach should work for ALU operations:
1. Compare qkz80 flag-setting functions with tnylpo's `add8()` and `sub8()`
2. Identify any semantic differences
3. Adopt tnylpo's logic where it differs

## Implementation Status

### Core Z80 Features
- ✅ All standard 8080 instructions
- ✅ Z80 extended instructions (IX/IY indexing)
- ✅ Undocumented instructions (IXH/IXL/IYH/IYL)
- ✅ Bit-by-bit flag calculation for precise emulation
- ✅ CPU mode switching (8080 vs Z80)
- ✅ DAA instruction (BCD operations)
- ⏳ ALU flag handling (4 tests failing)

### Flag Handling
- ✅ S (Sign)
- ✅ Z (Zero)
- ✅ H (Half-carry) - Verified correct for binary addition
- ⏳ P/V (Parity/Overflow) - May have issues
- ✅ N (Add/Subtract)
- ⏳ C (Carry) - May have issues
- ✅ X, Y (Undocumented) - Set from result bits 3 and 5

## Files Modified

### This Session
- `src/qkz80.cc` - Rewrote DAA instruction (lines 1720-1784)
- `src/cpm_emulator` - Rebuilt binary

### Previous Sessions
- `src/qkz80_reg_set.cc` - Flag calculation functions, CPU mode handling
- `src/qkz80.cc` - Instruction implementations

## Next Steps

### High Priority - Fix ALU Operations
1. **Compare implementations**: Review tnylpo's add8/sub8/logic operations
2. **Identify differences**: Find where qkz80 diverges from tnylpo
3. **Targeted fixes**: Apply tnylpo's logic to failing operations
4. **Test incrementally**: Verify each fix doesn't break passing tests

### Testing Strategy
1. Create minimal test cases for each ALU operation
2. Compare qkz80 vs tnylpo flag results
3. Identify specific flag combinations that differ
4. Fix and verify

## Reference Information

### Test Mask
zexdoc uses mask **0xD7** which tests:
- Bit 7: S (Sign)
- Bit 6: Z (Zero)
- Bit 5: ~~Y (ignored)~~
- Bit 4: H (Half-carry)
- Bit 3: ~~X (ignored)~~
- Bit 2: P/V (Parity/Overflow)
- Bit 1: N (Add/Subtract)
- Bit 0: C (Carry)

### Build Instructions
```bash
cd /home/wohl/qkz80/src
make
```

### Run Tests
```bash
cd /home/wohl/qkz80/src
timeout 180 ./cpm_emulator ../tests/zexdoc.com 2>&1 | tail -80
```

## Technical Notes

### Bit-by-bit Addition Algorithm
Based on full adder simulation (matches tnylpo):
```
For each bit i (0-7):
  result[i] = s1[i] XOR s2[i] XOR carry_in
  carry_out = (s2[i] AND carry_in) OR (s1[i] AND (s2[i] OR carry_in))
  carry_in_next = carry_out << 1
```

### DAA Algorithm
Complex lookup table based on:
- Initial carry flag (C)
- Initial half-carry flag (H)
- Initial subtract flag (N)
- High nibble value (0-F)
- Low nibble value (0-F)

Output:
- Correction value (0x00, 0x06, 0x60, 0x66)
- New carry flag
- New half-carry flag

See `src/qkz80.cc:1720-1784` for full implementation.

## Achievements

- ✅ 94% test pass rate (63/67)
- ✅ All bit operations working
- ✅ All load/store operations working
- ✅ All increment/decrement operations working
- ✅ All rotate/shift operations working
- ✅ All block operations working
- ✅ IX/IY indexed addressing fully working
- ✅ Undocumented IXH/IXL/IYH/IYL instructions working
- ✅ DAA instruction correctly implemented
- ⏳ Final 4 ALU tests to fix
