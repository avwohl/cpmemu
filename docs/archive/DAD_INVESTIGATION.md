# DAD Instruction Bug Investigation

## Problem
8080EXM.COM test fails on DAD (16-bit add) instruction with CRC mismatch:
- Expected CRC: `14474ba6`
- Actual CRC: `0677c931`

## Investigation Tools Created

### 1. Modified superzazu i8080 Emulator
**File**: `~/superzazu/i8080_tests.c` (lines 30-33)
- Added port 0 output logging for comparison
- Used as reference "known-good" 8080 implementation

### 2. Custom Test Program
**File**: `~/superzazu/test_dad.c`
- Standalone test runner for .com files
- Outputs port 0 bytes to stderr in comparable format
- Command: `./test_dad <file.com>`

### 3. Test Files Created
- `/tmp/dad_4_opcodes.com` - Tests all 4 DAD opcodes with base register values
- `/tmp/dad_full_test.asm` - Assembly source for comprehensive DAD test
- `/tmp/dad_full_test.com` - Compiled comprehensive test
- `/tmp/8080exer_output_v2.com` - Binary-patched 8080exer to output CRC bytes via port 0

### 4. Captured Output Files
- `/tmp/dad_superzazu.txt` - 19,456 bytes from superzazu (reference)
- `/tmp/dad_8080exer_qkz80.txt` - 19,456 bytes from qkz80 (buggy)

## Key Findings

### Byte-by-Byte Comparison
Compared all 19,456 iterations (4 opcodes × 4,864 register combinations):

**Byte 13 (FLAGS register) differs**:
- superzazu (correct): `c7` = `11000111`
- qkz80 (wrong): `cd` = `11001101`

### Flag Bit Analysis
Starting flags: `0xc6` = `11000110`

After DAD B instruction:
```
Expected (c7): Only bit 0 (Carry) changed
  c6 = 1100 0110
  c7 = 1100 0111  ← Only Carry flag changed

Actual (cd): Bits 0, 2, 3 changed
  c6 = 1100 0110
  cd = 1100 1101  ← Multiple flags changed!
       ^^^^ ^^^
       |||| |||└─ Bit 0 (Carry): 0→1 ✓ CORRECT
       |||| ||└── Bit 1: 1→1 (no change)
       |||| |└─── Bit 2 (Parity): 1→0 ✗ WRONG
       ||||└──── Bit 3 (X/UNUSED2): 0→1 ✗ WRONG
       |||└───── Bit 4 (H/AC): 0→0 (no change)
       ||└────── Bit 5 (Y/UNUSED3): 0→0 (no change)
       |└─────── Bit 6 (Zero): 1→1 (no change)
       └──────── Bit 7 (Sign): 1→1 (no change)
```

### 8080 DAD Specification
According to Intel 8080 documentation:
- **DAD (Double Add)**: 16-bit add to HL
- **Flags affected**: **ONLY Carry flag**
- **Flags preserved**: S, Z, P, AC (all other flags)

### Code Analysis

**Location**: `src/qkz80.cc:1497-1500`
```cpp
} else {
  // 8080: Only sets carry flag
  regs.set_carry_from_int((sum& ~0x0ffff)!=0);
}
```

**Location**: `src/qkz80_reg_set.cc:239-246`
```cpp
void qkz80_reg_set::set_carry_from_int(qkz80_big_uint x) {
  qkz80_uint8 result(get_flags());
  result&=  ~qkz80_cpu_flags::CY;
  if(x&1) {
    result|= qkz80_cpu_flags::CY;
  }
  set_flags(result);  // ← Calls fix_flags()
}
```

**Location**: `src/qkz80_reg_set.cc:61-63`
```cpp
void qkz80_reg_set::set_flags(qkz80_uint8 new_flags) {
  return AF.set_low(fix_flags(new_flags));  // Always fix flag bits
}
```

**Location**: `src/qkz80_reg_set.cc:47-55`
```cpp
qkz80_uint8 qkz80_reg_set::fix_flags(qkz80_uint8 new_flags) const {
  if (cpu_mode == MODE_8080) {
    // 8080 mode: Clear bits 3,5 (always 0 in 8080), force bit 1 to 1
    new_flags &= ~(qkz80_cpu_flags::UNUSED2 | qkz80_cpu_flags::UNUSED3);
    new_flags |= qkz80_cpu_flags::UNUSED1;
  }
  return new_flags;
}
```

### Flag Bit Definitions
**Location**: `src/qkz80_cpu_flags.h:8-19`
```cpp
UNUSED1=2,    // Bit 1 - N flag on Z80 (always 1 in 8080)
P=4,          // Bit 2 - Parity/Overflow flag
UNUSED2=8,    // Bit 3 - X flag on Z80 (always 0 in 8080)
AC=16,        // Bit 4 - Auxiliary carry / Half carry
UNUSED3=32,   // Bit 5 - Y flag on Z80 (always 0 in 8080)
Z=64,         // Bit 6 - Zero flag
S=128,        // Bit 7 - Sign flag
```

## Root Cause Hypothesis

The bug shows **bit 3 = 1** in output when `fix_flags()` should force it to 0 in 8080 mode.

Possible causes:
1. **Data captured in Z80 mode**: The comparison data may have been captured without `--8080` flag
2. **cpu_mode not set correctly**: The `--8080` flag may not be setting `cpu_mode == MODE_8080`
3. **Timing issue**: Flags modified after `fix_flags()` is called

## Next Steps
1. Verify `--8080` flag correctly sets `cpu_mode == MODE_8080`
2. Re-run comparison with confirmed 8080 mode
3. Add debug logging to trace flag modifications during DAD execution
4. Check if `cpu_mode` is accessible from qkz80_reg_set

## Test Reproduction

### Build superzazu test runner:
```bash
cd ~/superzazu
gcc -o test_dad test_dad.c i8080.c -I.
```

### Capture superzazu reference output:
```bash
./test_dad /tmp/8080exer_output_v2.com 2>&1 | \
  grep "output to port 0" | awk '{print $NF}' | \
  head -19456 > /tmp/dad_superzazu.txt
```

### Capture qkz80 output (need to verify --8080 mode):
```bash
./src/cpm_emulator --8080 /tmp/8080exer_output_v2.com 2>&1 | \
  grep "output to port 0" | awk '{print $NF}' | \
  head -19456 > /tmp/dad_qkz80_verified.txt
```

### Compare:
```bash
diff /tmp/dad_superzazu.txt /tmp/dad_qkz80_verified.txt
```

## Files Modified
- `~/superzazu/i8080_tests.c` - Added port 0 logging (lines 30-33)
- `~/superzazu/test_dad.c` - New test runner (created)
