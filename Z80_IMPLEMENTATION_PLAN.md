# Z80 Complete Instruction Set Implementation Plan

## Current Status
The emulator currently implements the Intel 8080 instruction set. We need to add Z80-specific instructions.

## Z80 Extensions Over 8080

### 1. New Registers (DONE)
- IX, IY (16-bit index registers)
- I (interrupt vector base)
- R (memory refresh counter)
- IFF1, IFF2 (interrupt flip-flops)
- IM (interrupt mode)
- Alternate register set (AF', BC', DE', HL')

### 2. CB Prefix Instructions (Bit Manipulation)
All CB instructions operate on 8-bit registers (B,C,D,E,H,L,(HL),A)

#### Rotates and Shifts (CB 00-3F)
- RLC r - Rotate Left Circular
- RRC r - Rotate Right Circular
- RL r - Rotate Left through Carry
- RR r - Rotate Right through Carry
- SLA r - Shift Left Arithmetic
- SRA r - Shift Right Arithmetic
- SLL r - Shift Left Logical (UNDOCUMENTED)
- SRL r - Shift Right Logical

#### Bit Operations (CB 40-FF)
- BIT b,r - Test bit (CB 40-7F)
- RES b,r - Reset bit (CB 80-BF)
- SET b,r - Set bit (CB C0-FF)

### 3. ED Prefix Instructions (Extended)

#### 16-bit Operations
- ADC HL,ss (ED 4A, 5A, 6A, 7A) - Add with carry to HL
- SBC HL,ss (ED 42, 52, 62, 72) - Subtract with carry from HL
- LD (nn),BC/DE/SP/HL (ED 43, 53, 63, 73)
- LD BC/DE/SP/HL,(nn) (ED 4B, 5B, 6B, 7B)

#### 8-bit Operations
- NEG (ED 44) - Negate accumulator
- IM 0/1/2 (ED 46, 56, 5E) - Set interrupt mode
- LD I,A / LD R,A (ED 47, 4F)
- LD A,I / LD A,R (ED 57, 5F)
- RRD (ED 67) - Rotate Right Decimal
- RLD (ED 6F) - Rotate Left Decimal

#### Block Instructions
- LDI/LDIR (ED A0, B0) - Load and increment/repeat
- LDD/LDDR (ED A8, B8) - Load and decrement/repeat
- CPI/CPIR (ED A1, B1) - Compare and increment/repeat
- CPD/CPDR (ED A9, B9) - Compare and decrement/repeat

#### Block I/O
- INI/INIR (ED A2, B2) - Input and increment/repeat
- IND/INDR (ED AA, BA) - Input and decrement/repeat
- OUTI/OTIR (ED A3, B3) - Output and increment/repeat
- OUTD/OTDR (ED AB, BB) - Output and decrement/repeat

#### Return Instructions
- RETI (ED 4D) - Return from interrupt
- RETN (ED 45) - Return from NMI

### 4. DD/FD Prefix Instructions (IX/IY Operations)
DD prefix uses IX, FD prefix uses IY

All HL instructions work with IX/IY when prefixed:
- LD IX,nn / LD IY,nn
- LD (IX+d),n / LD (IY+d),n
- LD r,(IX+d) / LD r,(IY+d)
- ADD IX,ss / ADD IY,ss
- INC/DEC IX / INC/DEC IY
- INC/DEC (IX+d) / INC/DEC (IY+d)
- All arithmetic/logical ops with (IX+d)/(IY+d)

### 5. DDCB/FDCB Prefix (Indexed Bit Operations)
Format: DD CB d opcode or FD CB d opcode
All CB operations but with (IX+d) or (IY+d) addressing

### 6. New Z80-Only Instructions
- EX AF,AF' (08) - Exchange AF with AF'
- EXX (D9) - Exchange BC,DE,HL with alternates
- LD SP,IX/IY (DD F9, FD F9)
- EX (SP),IX/IY (DD E3, FD E3)

### 7. Undocumented Instructions
- SLL (CB 30-37) - Shift left, set bit 0
- Operations on IX/IY half-registers (IXH, IXL, IYH, IYL)
- Duplicated ED instructions
- DDCB/FDCB result storage in registers

## Implementation Approach

1. ✓ Add Z80 registers to qkz80_reg_set
2. ✓ Modify execute() to handle prefixes inline using flags
3. Implement CB prefix bit operations
4. Implement ED prefix extended instructions
5. Modify existing HL instructions to respect DD/FD prefix (active_hl variable)
6. Implement DDCB/FDCB indexed bit operations
7. Add undocumented instructions
8. Test with Zexall/Zexdoc

## Testing
Download and run Zexall and Zexdoc test suites:
- https://github.com/agn453/ZEXALL/
- These test all documented and undocumented instructions
