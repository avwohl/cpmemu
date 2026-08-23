#!/bin/bash
# Regenerate flag_test.hex and flag_test.com by hand-assembling.
#
# There is no assembler in this repo - tests/README.md records that the .com
# files here were built elsewhere with z88dk - so this is the one place a test
# binary is produced from source that lives in the tree.  It writes next to
# itself rather than to an absolute path, so it works in any checkout.

set -u

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd) || exit 1
hex=$here/flag_test.hex
com=$here/flag_test.com

cat > "$hex" << 'EOF'
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

python3 - "$hex" "$com" << 'PYTHON'
import sys

src, dst = sys.argv[1], sys.argv[2]
data = bytearray()
with open(src) as f:
    for line in f:
        line = line.split('#')[0].strip()   # strip comments
        for byte in line.split():
            data.append(int(byte, 16))
with open(dst, 'wb') as f:
    f.write(data)
print("wrote %s, %d bytes" % (dst, len(data)))
PYTHON
