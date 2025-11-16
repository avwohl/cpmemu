#!/usr/bin/env python3
# Create a simple INR test

opcodes = [
    0x3E, 0xFF,     # MVI A, 0xFF
    0x3C,           # INR A (0xFF -> 0x00, should set Z flag)
    0x76,           # HALT
]

with open('inr_test.com', 'wb') as f:
    f.write(bytes(opcodes))

print(f"Created inr_test.com with {len(opcodes)} bytes")
