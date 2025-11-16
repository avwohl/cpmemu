#!/usr/bin/env python3
# Create a minimal test .COM file with just a few ALU instructions

opcodes = [
    # Test 1: Simple ADD
    0x3E, 0x0E,     # MVI A, 0x0E
    0x06, 0x02,     # MVI B, 0x02
    0x80,           # ADD B

    # Test 2: AND
    0x3E, 0xFF,     # MVI A, 0xFF
    0x06, 0xAA,     # MVI B, 0xAA
    0xA0,           # ANA B

    # Test 3: OR
    0x3E, 0x0F,     # MVI A, 0x0F
    0x06, 0xF0,     # MVI B, 0xF0
    0xB0,           # ORA B

    # Test 4: XOR
    0x3E, 0x55,     # MVI A, 0x55
    0x06, 0xAA,     # MVI B, 0xAA
    0xA8,           # XRA B

    # Test 5: INR
    0x3E, 0xFF,     # MVI A, 0xFF
    0x3C,           # INR A

    # Exit
    0xC3, 0x00, 0x00  # JMP 0
]

with open('test_alu_minimal.com', 'wb') as f:
    f.write(bytes(opcodes))

print(f"Created test_alu_minimal.com with {len(opcodes)} bytes")
