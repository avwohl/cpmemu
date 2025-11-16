#!/usr/bin/env python3
# Test ADD with specific overflow cases

opcodes = [
    # Test 1: 0x7F + 0x01 = 0x80 (overflow: positive + positive = negative)
    0x3E, 0x7F,     # MVI A, 0x7F
    0x06, 0x01,     # MVI B, 0x01
    0x80,           # ADD B

    # Test 2: 0x80 + 0xFF = 0x7F (overflow: negative + negative = positive)
    0x3E, 0x80,     # MVI A, 0x80
    0x06, 0xFF,     # MVI B, 0xFF
    0x80,           # ADD B

    # Test 3: 0x01 + 0x02 = 0x03 (no overflow)
    0x3E, 0x01,     # MVI A, 0x01
    0x06, 0x02,     # MVI B, 0x02
    0x80,           # ADD B

    0x76,           # HALT
]

with open('addtest.com', 'wb') as f:
    f.write(bytes(opcodes))

print(f"Created addtest.com with {len(opcodes)} bytes")
