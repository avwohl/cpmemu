# Analysis of tnylpo Z80 Emulator Flag Handling

## Source: /home/wohl/z80/tnylpo/cpu.c

## Key Findings

### 1. 16-Bit Addition (add16 function, line 768)

Uses bit-by-bit simulation to get exact flag behavior:

```c
for (i = 0; i < 16; i++) {
    su |= (s1 ^ s2 ^ cy) & ma;
    cy = ((s2 & cy) | (s1 & (s2 | cy))) & ma;
    if (i == 11) flag_h = (cy != 0);      // H from bit 11
    if (i == 14) c14 = (cy != 0);          // Save carry from bit 14
    if (i == 15) flag_c = (cy != 0);      // C from bit 15
    ...
}
flag_n = 0;                                // Clear N for addition
flag_v = flag_c ^ c14;                     // V = carry_bit15 XOR carry_bit14
flag_x = ((su & 0x0800) != 0);            // X from bit 11 (undocumented)
flag_y = ((su & 0x2000) != 0);            // Y from bit 13 (undocumented)
flag_z = (su == 0);                        // Z if result is 0
flag_s = ((su & 0x8000) != 0);            // S from bit 15
```

### 2. 16-Bit Subtraction (sub16 function, line 794)

Same bit-by-bit approach:
- H flag from carry at bit 11
- V (overflow) = carry_bit15 XOR carry_bit14
- N flag set to 1 for subtraction
- X, Y from bits 11 and 13

### 3. ADD HL/IX/IY (inst_dad, line 892)

**Critical insight**: Uses add16() but then restores S, Z, P!

```c
int old_s = flag_s, old_z = flag_z, old_p = flag_p;
...
set_hl(add16(internal, s, 0));
flag_s = old_s;  // Restore S
flag_z = old_z;  // Restore Z
flag_p = old_p;  // Restore P
```

So ADD HL only affects: **H, N (cleared), C, X, Y**

### 4. ADC HL / SBC HL (lines 1732, 1749)

Use add16()/sub16() directly **without** restoring flags.

So ADC/SBC HL affect: **S, Z, H, P/V, N, C, X, Y** (all flags)

### 5. Flag Storage

Uses individual boolean variables:
```c
static int flag_s, flag_z, flag_y, flag_h, flag_x, flag_p, flag_n, flag_c;
```

Not a packed byte - makes flag manipulation simpler.

## Key Formulas

### Overflow Detection
```
V = carry_out_bit15 XOR carry_out_bit14
```

This is the standard signed overflow detection for 16-bit arithmetic.

### Half-Carry for 16-bit
```
H = carry_out_bit11
```

Captured during bit-by-bit addition at position 11.

### Undocumented Flags
```
X = bit 11 of result
Y = bit 13 of result
```

## Implications for Our Implementation

1. **Need to fix overflow calculation** - Use C15 XOR C14, not our current method
2. **ADD HL must preserve S, Z, P/V** - We're currently modifying them
3. **Half-carry is from bit 11** - Our calculation may be wrong
4. **Undocumented X, Y flags** - We're not setting these (optional for Zexdoc)

## Action Items

1. Fix `set_flags_from_add16()` to:
   - Only modify H, N, C
   - Preserve S, Z, P/V

2. Fix `set_flags_from_diff16()` to use proper overflow:
   - V = C15 XOR C14 (not current sign-based method)

3. Consider bit-by-bit simulation or lookup table for H flag

4. Add X, Y flag support (for Zexall)
