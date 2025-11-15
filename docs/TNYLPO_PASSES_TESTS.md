# Tnylpo Test Results

## Date: 2025-11-15

## Confirmed: Tnylpo passes zexdoc

```
wohl@awohl4:~/z80/z80pack/cpmtools$ tnylpo exz80doc.com
Z80 instruction exerciser
Undefined status bits NOT taken into account
<adc,sbc> hl,<bc,de,hl,sp>....( 71,680) cycles   OK
add hl,<bc,de,hl,sp>..........( 35,840) cycles   OK
add ix,<bc,de,ix,sp>..........( 35,840) cycles   OK
add iy,<bc,de,iy,sp>..........( 35,840) cycles   OK
aluop a,nn....................( 45,056) cycles   OK
```

## Our Results (FAILING)

```
<adc,sbc> hl,<bc,de,hl,sp>....ERROR **** crc expected:f8b4eaa9 found:2896c86e
add hl,<bc,de,hl,sp>..........ERROR **** crc expected:89fdb635 found:7871f3da
add ix,<bc,de,ix,sp>..........ERROR **** crc expected:c133790b found:94307d3e
add iy,<bc,de,iy,sp>..........ERROR **** crc expected:e8817b9e found:353467f6
aluop a,nn....................ERROR **** crc expected:48799360 found:9e922f9e
```

## Key Insight

"Undefined status bits NOT taken into account" - This means zexdoc ignores X and Y flags. So our X/Y implementation doesn't affect test results.

## Implications

Since we based our implementation on tnylpo's code, but tnylpo passes and we fail, there must be a difference in:

1. **How we're using the bitwise simulation results**
2. **Some other flag calculation we haven't considered**
3. **Instruction implementation calling wrong flag functions**
4. **Edge cases in the bitwise simulation itself**

## Action Items

1. Line-by-line comparison of our flag functions vs tnylpo
2. Check if our instruction implementations call the right flag functions
3. Verify edge cases (zero values, overflow corner cases)
4. Check 8-bit ALU operations since those are also failing
