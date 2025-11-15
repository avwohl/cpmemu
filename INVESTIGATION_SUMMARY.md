# Z80 Emulator Investigation Summary
## November 15, 2025 - Evening Session

### Problem Statement
All 67 zexdoc tests fail despite extensive flag calculation fixes. The emulator previously passed 8080 tests before Z80 support was added.

### Key Discoveries

#### 1. zexdoc Tests More Than Just Flags
Analysis of `/home/wohl/zexall-src/zexdoc.z80` reveals the CRC includes 16 bytes:
- Memory operand (2 bytes)
- IY register (2 bytes)
- IX register (2 bytes)
- HL register (2 bytes)
- DE register (2 bytes)
- BC register (2 bytes)
- AF register (2 bytes, flags masked)
- SP stack pointer (2 bytes)

**Implication**: Flag fixes alone cannot make tests pass. Register values must also be correct.

#### 2. Simple LD Instructions Fail
Test `ld <bcdehla>,<bcdehla>` fails with CRC mismatch.

LD instructions like `LD B,C`:
- Don't modify flags at all
- Just copy one register to another
- Should be trivial

**Implication**: The problem is NOT primarily in flag calculations, but in register handling or some other systematic issue.

#### 3. Previously Passed 8080 Tests
User confirms emulator passed tests as 8080 before Z80 support was added (before commit 24a8b7c).

**Implication**: Basic instruction execution and register handling WAS working. Something broke when Z80 support was added OR zexdoc has never fully worked.

#### 4. Flag Fixes Show CRC Progress But No Passes
Recent commits improved 7 test CRCs:
- `<daa,cpl,scf,ccf>`: e9e0d068 → 74db8180
- `cpd<r>`, `cpi<r>`: CRCs changed
- `ldd<r>`, `ldi<r>`: CRCs changed

But still **0/67 tests pass**.

**Implication**: Flags are PART of the problem, but not the PRIMARY issue preventing test passes.

### Hypotheses

#### Most Likely: Instruction Execution Bug
Even though LD should be simple, there might be:
- Wrong opcode decoding
- Wrong register mapping
- Incorrect memory access
- Stack operation issues

#### Less Likely: Test Setup Issue
- Wrong initial register state
- zexdoc test harness incompatibility
- Memory layout problems

#### Least Likely: Struct Layout
Checked git history - qkz80_reg_set struct hasn't changed since Z80 registers were added in commit 24a8b7c.

### Recommended Next Steps

#### 1. Enable zexdoc Debug Output [HIGHEST PRIORITY]
Modify `/home/wohl/zexall-src/zexdoc.z80` lines 1112-1123 (currently `if 0`) to enable debug output:
- Shows the 16 bytes of `msat` for each test iteration
- Allows manual comparison of register state
- Can identify which specific registers are wrong

#### 2. Compare First Test Case With tnylpo
Run simplest test (`<adc,sbc> hl` or `<inc,dec> a`) on both:
- Our emulator with debug output
- tnylpo with same debug output
- Compare byte-by-byte to find exact discrepancy

#### 3. Create Minimal Reproduction
Write tiny test program that:
- Sets registers to known values
- Executes single LD instruction
- Prints resulting register values
- Manually verify correctness

Example:
```assembly
    org 0x100
    ld b, 0x12
    ld c, 0x34
    ld b, c      ; Should copy 0x34 to B
    halt
```

#### 4. Check Basic Operations
Verify in isolation:
- LD reg,immediate works
- LD reg,reg works
- PUSH/POP work correctly
- Memory addressing works

### Files for Reference

#### Critical Source Files
- `/home/wohl/zexall-src/zexdoc.z80` - Test implementation (shows CRC calculation)
- `/home/wohl/cl/tnylpo/cpu.c` - Reference emulator (passes all tests)

#### Our Implementation
- `/home/wohl/qkz80/src/qkz80.cc` - Main CPU implementation
- `/home/wohl/qkz80/src/qkz80_reg_set.cc` - Register and flag handling
- `/home/wohl/qkz80/src/qkz80_reg_set.h` - Register struct definition

#### Documentation
- `/home/wohl/qkz80/docs/DEBUGGING_INSIGHT_2025_11_15.md` - Today's insights
- `/home/wohl/qkz80/docs/SESSION_2025_11_15_AFTERNOON.md` - Afternoon session
- `/home/wohl/qkz80/docs/CPU_MODE_IMPLEMENTATION.md` - Mode system details

### Current Status

- **Tests passing**: 0/67
- **CRC improvements**: 7 tests showing measurable progress
- **Flag fixes applied**: ~10 commits worth
- **Root cause**: Still unknown, but likely NOT primarily flags

### Why Not Bisect Git History?

While normally bisecting would help find when tests broke, it may not work here because:
1. zexdoc is a Z80 test suite - wouldn't work on pure 8080 emulator
2. The "passing 8080 tests" likely refers to different test suite
3. Z80 support may never have fully passed zexdoc

### Conclusion

The systematic failure of ALL tests, including trivial LD instructions, suggests a fundamental issue beyond flag calculations. The next step MUST be to get visibility into what register values are actually being produced vs. expected. This requires enabling debug output in zexdoc or creating minimal test cases.

The good news: CRCs are improving with each fix, proving we're making progress. The bad news: we're fixing symptoms without understanding the root cause.

**Action Required**: Add debug output to see actual vs. expected register values.
