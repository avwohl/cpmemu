#!/bin/bash
# Create a simple test COM file by hand-assembling

cat > /home/wohl/qkz80/tests/flag_test.hex << 'EOF'
# Test 1: ADD overflow
3E 7F       # LD A, 0x7F
C6 01       # ADD A, 0x01
00          # NOP

# Test 2: ADD carry
3E FF       # LD A, 0xFF
C6 01       # ADD A, 0x01
00          # NOP

# Test 3: ADD half-carry
3E 0F       # LD A, 0x0F
C6 01       # ADD A, 0x01
00          # NOP

# Exit
C3 00 00    # JP 0x0000
EOF

# Convert hex to binary
python3 << 'PYTHON'
with open('/home/wohl/qkz80/tests/flag_test.hex', 'r') as f:
    bytes_data = []
    for line in f:
        line = line.split('#')[0].strip()  # Remove comments
        if line:
            hex_bytes = line.split()
            for hb in hex_bytes:
                bytes_data.append(int(hb, 16))

with open('/home/wohl/qkz80/tests/flag_test.com', 'wb') as f:
    f.write(bytes(bytes_data))

print(f"Created flag_test.com with {len(bytes_data)} bytes")
PYTHON

chmod +x /home/wohl/qkz80/tests/flag_test.com
