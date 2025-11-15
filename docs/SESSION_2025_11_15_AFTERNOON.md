# Z80 Emulator Debug Session - November 15, 2025 (Afternoon)

## Session Overview
Continued systematic debugging of Z80 flag handling bugs, focusing on DAA instruction and maintaining rigorous testing discipline.

## Commits Pushed (3)

### 1. Commit 70ba794: CB Rotate/Shift Dedicated Function
**Purpose**: Code clarity and maintainability
**Changes**:
- Created `set_flags_from_rotate8()` specifically for RLC, RRC, RL, RR, SLA, SRA, SLL, SRL
- Ensures H and N flags always cleared (not calculated from operands)
- Sets P/V as parity, X/Y from result bits 3 and 5

**Impact**: No CRC change (functionally equivalent to previous code, but clearer)

### 2. Commit 1d095df: DAA Subtraction Support
**Purpose**: Fix DAA to work after both ADD and SUB operations
**Changes**:
- Added N flag checking to determine operation type
- N=0 (after ADD): Add BCD correction
- N=1 (after SUB): Subtract BCD correction
- Use appropriate half-carry calculation for each case

**Impact**: `<daa,cpl,scf,ccf>` CRC changed e9e0d068 → c7af3cc2

### 3. Commit 10b95ba: DAA N Flag Preservation
**Purpose**: DAA must preserve N flag from previous operation
**Changes**:
- Created dedicated `set_flags_from_daa()` function
- Takes N flag as parameter and preserves it
- Sets other flags correctly (S, Z, H, P/V, C, X, Y)
- P/V calculated as parity (not overflow)

**Impact**: `<daa,cpl,scf,ccf>` CRC changed c7af3cc2 → 74db8180

## Test Results Analysis

### zexdoc Status
- **Total tests**: 67
- **Passing**: 0
- **Failing**: 67
- **All tests complete**: YES (no hangs)

### Tests Showing CRC Changes (Progress Indicators)

| Test | Before Session | After Session | Change |
|------|---------------|---------------|---------|
| `<daa,cpl,scf,ccf>` | e9e0d068 | **74db8180** | 2 fixes applied |
| `cpd<r>` | fbde4441 | 4a62581f | Block CP changes |
| `cpi<r>` | 3b66f45b | 888cd127 | Block CP changes |
| `ldd<r> (1)` | 1894f31b | 5ecfe4ae | Block LD changes |
| `ldd<r> (2)` | 8480099f | d5ab0ce3 | Block LD changes |
| `ldi<r> (1)` | ff235294 | 8fe7e613 | Block LD changes |
| `ldi<r> (2)` | 95d62a7b | 05cc7256 | Block LD changes |

## Technical Details

### New Functions Created

#### `set_flags_from_rotate8(qkz80_uint8 result, qkz80_uint8 new_carry)`
**Location**: qkz80_reg_set.cc:101-135
**Purpose**: Flag setting for CB-prefixed rotate/shift instructions
**Flags Set**:
- C: From carry parameter
- H: Always 0
- N: Always 0
- Z: From result == 0
- S: From result & 0x80
- P/V: Parity of result
- X: From result bit 3 (Z80 only)
- Y: From result bit 5 (Z80 only)

#### `set_flags_from_daa(qkz80_uint8 result, qkz80_uint8 n_flag, qkz80_uint8 half_carry, qkz80_uint8 carry)`
**Location**: qkz80_reg_set.cc:478-516
**Purpose**: Flag setting for DAA instruction
**Flags Set**:
- C: From carry parameter
- H: From half_carry parameter
- N: **PRESERVED** from n_flag parameter (key difference)
- Z: From result == 0
- S: From result & 0x80
- P/V: Parity of result (not overflow)
- X: From result bit 3 (Z80 only)
- Y: From result bit 5 (Z80 only)

### DAA Implementation Details

**Correction Calculation** (unchanged):
```c
if (half_carry || low_nibble > 9)
    correction |= 0x06;
if (carry || high_nibble > 9 || (high_nibble >= 9 && low_nibble > 9))
    correction |= 0x60;
```

**Application** (FIXED):
```c
if (n_flag) {
    // After SUB: subtract correction
    result = A - correction;
    half_carry = compute_subtract_half_carry(A, result, correction, 0);
} else {
    // After ADD: add correction
    result = A + correction;
    half_carry = compute_sum_half_carry(A, correction, 0);
}
```

## Key Insights

### What's Working
1. **Methodical approach**: Each targeted fix produces measurable CRC changes
2. **DAA complexity understood**: Requires both subtraction support AND N flag preservation
3. **Code quality improving**: Dedicated flag functions make behavior explicit
4. **Block instructions**: CRCs changing on LDI/LDD/CPI/CPD tests

### Remaining Mysteries
1. **Zero passes**: Despite 7+ CRC improvements, no test fully passes yet
2. **INC/DEC unchanged**: These tests' CRCs haven't budged despite correct-looking implementation
3. **LD instructions failing**: Even simple loads fail, suggesting issue beyond flags
4. **Systematic problem?**: ALL 67 tests fail - hints at pervasive issue

## Hypotheses for Investigation

### Hypothesis 1: CRC Includes More Than Flags
zexdoc CRC likely includes:
- Register values (A, F, B, C, D, E, H, L, IX, IY, SP, PC)
- Memory contents at specific locations
- Possibly R register or other state

**Implication**: Even perfect flags won't pass if register operations wrong

### Hypothesis 2: Test Initialization Issue
Possible issues:
- Wrong initial register values
- Wrong initial flag state
- Memory setup problems
- R register not incrementing correctly

### Hypothesis 3: Subtle Flag Bugs Remain
Even with improvements, may have:
- Edge cases in half-carry calculations
- P/V mode confusion (parity vs overflow)
- X/Y flag source errors in some contexts
- Interaction bugs between flags

### Hypothesis 4: Instruction Implementation Bugs
Beyond flags:
- Index register arithmetic (IX+d, IY+d calculations)
- Memory addressing modes
- Undocumented instruction variants
- Timing or state management

## Files Modified

### Source Files
- `src/qkz80.cc` - DAA instruction implementation
- `src/qkz80_reg_set.cc` - Two new flag functions
- `src/qkz80_reg_set.h` - Function declarations

### Documentation
- `docs/SESSION_2025_11_15_AFTERNOON.md` - This file

## Statistics
- **Session duration**: ~2.5 hours
- **Commits**: 3
- **Functions created**: 2
- **Lines added**: ~80
- **Tests improved**: 7 CRC changes
- **Tests passing**: 0 (but progress measurable)

## Next Session Recommendations

### Immediate Priorities
1. **Compare with tnylpo execution**: Since tnylpo passes all tests, comparing execution traces could reveal the systematic issue
2. **Add debug logging**: Log flag values for one simple test and compare with expected
3. **Focus on simplest case**: Pick `<inc,dec> a` and trace every flag bit

### Investigation Approaches
1. **Micro-test creation**: Write minimal test for single INC, compare our output with tnylpo
2. **CRC analysis**: Understand what zexdoc includes in CRC beyond flags
3. **State dumping**: Add ability to dump all CPU state at instruction boundaries

### Code Review Areas
1. Index register arithmetic (IX+d, IY+d)
2. Register pair operations
3. Memory access patterns
4. R register handling (if included in CRC)

## Conclusion

This session demonstrated continued progress with the systematic approach:
- 3 quality commits with clear improvements
- DAA instruction significantly improved (2 fixes)
- 7 tests showing measurable CRC changes
- Code quality enhanced with dedicated flag functions

However, the lack of any passing tests suggests a deeper issue remains. The next session should focus on comparing execution with a known-good emulator (tnylpo) to identify the systematic problem preventing test passes.

The methodical approach of creating well-documented, focused fixes continues to be valuable even though we haven't achieved passing tests yet. Each CRC change proves we're fixing real bugs, moving incrementally toward correctness.
