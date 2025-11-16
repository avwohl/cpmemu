#!/usr/bin/env python3
# Test INR with multiple values to understand the pattern

opcodes = [
    # Test 1: INR from 0x7E -> 0x7F (no overflow)
    0x3E, 0x7E,     # MVI A, 0x7E
    0x3C,           # INR A

    # Test 2: INR from 0x7F -> 0x80 (overflow: positive to negative)
    0x3E, 0x7F,     # MVI A, 0x7F
    0x3C,           # INR A

    # Test 3: INR from 0xFF -> 0x00 (no overflow, just wraparound)
    0x3E, 0xFF,     # MVI A, 0xFF
    0x3C,           # INR A

    # Test 4: INR from 0x0F -> 0x10 (half-carry)
    0x3E, 0x0F,     # MVI A, 0x0F
    0x3C,           # INR A

    0x76,           # HALT
]

with open('inr_multi.com', 'wb') as f:
    f.write(bytes(opcodes))

print(f"Created inr_multi.com with {len(opcodes)} bytes")
