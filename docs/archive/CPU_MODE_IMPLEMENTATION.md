# CPU Mode Implementation - 8080 vs Z80

## Overview
Added CPU mode switching to support both Intel 8080 and Zilog Z80 flag behavior.

## Key Differences Between 8080 and Z80

### Flag Calculations

#### 8080 Mode
- **AC (Auxiliary Carry)**: Has bugs in implementation
- **P**: Always parity
- **16-bit ADD (DAD)**: Only affects Carry flag
- **No N flag**: Bit 1 is unused

#### Z80 Mode
- **H (Half Carry)**: Correct implementation
- **P/V**: Parity for logical ops, Overflow for arithmetic
- **N**: Set for subtract operations, cleared for add
- **16-bit ADD**: Affects H, N (cleared), C
- **16-bit ADC/SBC**: Affects S, Z, H, P/V, N, C
- **Undocumented X/Y flags**: Bits 3 and 5

## Implementation

### 1. CPU Mode Enum
Added to both `qkz80` and `qkz80_reg_set`:
```cpp
enum CPUMode {
  MODE_8080,  // Intel 8080 compatibility
  MODE_Z80    // Zilog Z80 mode (default)
};
```

### 2. Mode-Aware Flag Functions

#### 8-bit Operations
- `set_flags_from_sum8()`: Clears N for Z80, sets P/V as overflow
- `set_flags_from_diff8()`: Sets N for Z80 subtract operations

#### 16-bit Operations (Z80 only)
- `set_flags_from_add16()`: For ADD HL/IX/IY,ss
  - Only affects: H, N (cleared), C
  - Preserves: S, Z, P/V

- `set_flags_from_diff16()`: For ADC/SBC HL,ss
  - Affects all flags: S, Z, H, P/V, N, C
  - Proper overflow detection

### 3. Backward Compatibility

8080 mode preserves original behavior:
- DAD only sets carry
- No N flag manipulation
- AC behaves as before (bugs and all)

## Files Modified

- `src/qkz80.h` - Added CPUMode enum and accessors
- `src/qkz80.cc` - Initialize to Z80 mode, mode-aware 16-bit ops
- `src/qkz80_reg_set.h` - Added cpu_mode field and new functions
- `src/qkz80_reg_set.cc` - Implemented Z80-specific flag calculations

## Testing

Default mode is Z80. To use 8080 mode:
```cpp
cpu.set_cpu_mode(qkz80::MODE_8080);
```

Register set mode is synchronized automatically in constructor.

## Next Steps

1. Test with Zexdoc to verify Z80 flag behavior
2. Ensure 8080 programs still work correctly
3. May need fine-tuning of overflow detection
4. Consider adding mode switch via command-line flag
