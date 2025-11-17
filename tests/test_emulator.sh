#!/bin/bash
# Test script for CP/M emulator

cd /home/wohl/qkz80

echo "=== Testing CP/M Emulator with MBASIC ==="
echo ""

# Test 1: Run MBASIC and show prompt
echo "Test 1: MBASIC starts and shows prompt"
echo "----------------------------------------"
timeout 2 src/cpm_emulator com/mbasic.com 2>&1 | head -20
echo ""

# Test 2: With config file
echo "Test 2: Loading with config file"
echo "----------------------------------------"
if [ -f examples/simple_test.cfg ]; then
    timeout 2 src/cpm_emulator examples/simple_test.cfg 2>&1 | head -20
fi
echo ""

# Test 3: With direct file mapping
echo "Test 3: Direct file mapping from command line"
echo "----------------------------------------"
if [ -f /home/wohl/cl/mbasic/tests/printsep.bas ]; then
    timeout 2 src/cpm_emulator com/mbasic.com /home/wohl/cl/mbasic/tests/printsep.bas 2>&1 | head -20
fi
echo ""

echo "=== Tests Complete ==="
echo ""
echo "To use interactively:"
echo "  ./src/cpm_emulator com/mbasic.com"
echo "  ./src/cpm_emulator examples/simple_test.cfg"
