# Zexdoc Flag Mask Analysis

## How the Test Works

From `/home/wohl/qkz80/zexall-src/zexdoc.z80:1102`:
```assembly
flgmsk: and a,0d7h ; mask-out irrelevant bits (self-modified code!)
```

The test modifies this instruction to use each test's specific mask. The mask is AND'ed with the flag register before calculating the CRC.

## Mask Values

### ADD HL/IX/IY: 0xC7 = 11000111
Checks bits: S(7), Z(6), P/V(2), N(1), C(0)
Ignores bits: Y(5), **H(4)**, X(3)

**Why H is ignored**: Unknown - possibly uncertainty in original documentation

### ADC/SBC HL: 0xC7 = 11000111
Same as ADD HL

### 8-bit ALU: 0xD7 = 11010111
Checks bits: S(7), Z(6), **H(4)**, P/V(2), N(1), C(0)
Ignores bits: Y(5), X(3)

## Key Insight

Even though we're ignoring H for ADD HL, **tnylpo still passes the test**. This means:
1. The other flags (S, Z, P/V, N, C) must be exactly correct
2. Our implementation is getting these wrong even after masking

## What We Need to Fix

Since masking is applied the same way in both tnylpo and our emulator, but tnylpo passes and we fail, the issue is that our **actual flag values** (before masking) are wrong.

For ADD HL with mask 0xC7:
- Our S, Z, or P/V preservation is wrong, OR
- Our N clearing is wrong, OR
- Our C calculation is wrong

Most likely: Since we're using bit-by-bit simulation copied from tnylpo, the issue is probably in **how we apply the results**, not in the simulation itself.

## Next Steps

1. Add detailed logging to print exact flag values
2. Create minimal test case (single ADD HL operation)
3. Compare our flag output vs expected
4. Check if issue is in flag application vs calculation
