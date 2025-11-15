# zexdoc Test Progress Tracking

## Current Status

**Date:** November 15, 2025
**Tests Passing:** 0 / 67
**Tests Completing:** 67 / 67 ✅
**Status:** All tests complete without hanging, but all show CRC errors

## Test Completion History

| Milestone | Tests Completing | Notes |
|-----------|------------------|-------|
| Before stack fix | 0 | Crashed immediately |
| After stack fix (473ea14) | 1 | simple_con worked, zexdoc crashed |
| After relative jumps (d36a5c7) | ~45 | Hung on indexed LD test |
| After indexed LD (b2d5362) | 67 | All tests complete ✅ |
| Current | 67 | All tests complete ✅ |

## CRC Evolution - Key Tests

### INC/DEC Tests
Shows evolution as we fixed N, X, Y flags:

| Test | Original | After INC/DEC Fix | After All Fixes | Expected |
|------|----------|-------------------|-----------------|----------|
| `<inc,dec> a` | 0c9a85d6 | 4816749f | 4816749f | d18815a4 |
| `<inc,dec> e` | 443334f0 | 51036cbb | 33fd6c81 | e175afcc |
| `<inc,dec> h` | b0dcdb7b | - | c712830a | 1ced847d |

### Rotate Tests
Shows impact of rotate flag fixes:

| Test | Original | After Rotate Fix | Expected |
|------|----------|------------------|----------|
| `<rlca,rrca,rla,rra>` | 4bae6af9 | f47b53e2 | 251330ae |

### Arithmetic Tests
These should be relatively stable (bit-simulation already correct):

| Test | Current CRC | Expected |
|------|-------------|----------|
| `aluop a,nn` | 32db663a | 48799360 |
| `aluop a,<b,c,d,e,h,l,(hl),a>` | 1a9aca4c | fe43b016 |
| `<adc,sbc> hl,<bc,de,hl,sp>` | 987ecc68 | f8b4eaa9 |

## Remaining Issues by Category

### 1. CB-Prefixed Instructions
**Tests affected:**
- `shf/rot (<ix,iy>+1)` - Expected: 713acd81, Found: 9cdcd5d8
- `shf/rot <b,c,d,e,h,l,(hl),a>` - Expected: eb604d58, Found: 3979c43d
- `bit n,(<ix,iy>+1)` - Expected: a8ee0867, Found: 87faef41
- `bit n,<b,c,d,e,h,l,(hl),a>` - Expected: 7b55e6c8, Found: c800ed09
- `<set,res> n,<bcdehl(hl)a>` - Expected: 8b57f008, Found: 13659c05
- `<set,res> n,(<ix,iy>+1)` - Expected: cc63f98a, Found: f0ea5379

**Issue:** CB-prefixed rotate, shift, bit, set, and reset instructions need X/Y flag handling

### 2. Block Instructions
**Tests affected:**
- `ldd<r> (1)` - Expected: 94f42769, Found: 1894f31b
- `ldd<r> (2)` - Expected: 5a907ed4, Found: 8480099f
- `ldi<r> (1)` - Expected: 9abdf6b5, Found: ff235294
- `ldi<r> (2)` - Expected: eb59891b, Found: 95d62a7b
- `cpd<r>` - Expected: a87e6cfa, Found: fbde4441
- `cpi<r>` - Expected: 06deb356, Found: 3b66f45b

**Issue:** LDI/LDIR/LDD/LDDR/CPI/CPIR/CPD/CPDR have complex flag behavior

### 3. Special Instructions
**Tests affected:**
- `neg` - Expected: 6a3c3bbd, Found: 0321c130
- `<rrd,rld>` - Expected: 955ba326, Found: 3517ff89
- `<daa,cpl,scf,ccf>` - Expected: 9b4ba675, Found: 0af5da9a

**Issues:**
- NEG: Should set N=1 and other flags correctly
- RLD/RRD: Rotate digit instructions with special flag behavior
- DAA: Decimal adjust (complex)
- CPL: Complement accumulator
- SCF/CCF: Set/complement carry flag

### 4. Load Instructions
**Tests affected:** Multiple LD variants

**Likely issues:**
- Some LD instructions may affect flags unexpectedly
- Indexed addressing modes may have flag issues
- Most LD instructions should NOT affect flags (except special cases)

### 5. 16-bit Arithmetic
**Tests affected:**
- `add hl,<bc,de,hl,sp>` - Expected: 89fdb635, Found: 2805a593
- `add ix,<bc,de,ix,sp>` - Expected: c133790b, Found: c4442b77
- `add iy,<bc,de,iy,sp>` - Expected: e8817b9e, Found: 654031bf
- `<adc,sbc> hl,<bc,de,hl,sp>` - Expected: f8b4eaa9, Found: 987ecc68

**Issue:** 16-bit operations may have subtle flag calculation bugs

## Progress Metrics

### By Fix Type

| Fix Applied | Tests Affected | CRC Changed | Status |
|-------------|----------------|-------------|--------|
| INC/DEC N/X/Y flags | 13 tests | Yes | ✅ Applied |
| Logical N/X/Y flags | ~3 tests | Yes | ✅ Applied |
| Rotate acc N/X/Y flags | 1 test | Yes | ✅ Applied |
| CB rotate/shift | ~6 tests | Not yet | ⬜ Pending |
| Block instructions | ~6 tests | Not yet | ⬜ Pending |
| NEG | 1 test | Not yet | ⬜ Pending |
| RLD/RRD | 1 test | Not yet | ⬜ Pending |
| DAA/CPL/SCF/CCF | 1 test | Not yet | ⬜ Pending |

### Overall Progress

- **Instruction implementation:** ~85% (most opcodes implemented)
- **Flag calculation:** ~40% (N/X/Y flags partially done)
- **Test completion:** 100% (all tests run without hanging)
- **Test accuracy:** 0% (all tests show CRC errors)

## Next Priority Tasks

1. **CB-prefixed instructions** - High impact (affects 6 test groups)
2. **Block instructions** - Medium impact (affects 6 test groups)
3. **NEG instruction** - Low impact (affects 1 test group)
4. **RLD/RRD instructions** - Low impact (affects 1 test group)
5. **DAA instruction** - Medium complexity

## Verification Strategy

1. Run simple targeted tests against tnylpo
2. Verify CRC changes in zexdoc
3. Create focused tests for each instruction type
4. Document expected vs actual behavior
5. Fix implementation
6. Re-test with zexdoc

## References

- zexdoc source: `/home/wohl/qkz80/tests/zexdoc.com`
- tnylpo comparison: `/tmp/tnylpo_zexdoc.txt`
- Current results: `/tmp/zexdoc_all_flags.txt`
- Simple tests: `/home/wohl/qkz80/tests/*.asm`

## Notes

- Simple flag tests (tflags, test_n_flag, test_sequence) all pass ✅
- Console I/O tests (simple_con, test_djnz) all pass ✅
- zexdoc completes in ~2 minutes (normal speed)
- No hangs or crashes during testing ✅
- CRC values changing with each fix (progress indicator) ✅
