#!/bin/bash
# Test runner for Z80 emulator

echo "========================================"
echo "Z80 Emulator Test Suite"
echo "========================================"
echo

EMU="src/cpm_emulator"

if [ ! -f "$EMU" ]; then
    echo "Error: Emulator not found at $EMU"
    echo "Building emulator..."
    cd src
    make clean
    make
    cd ..
fi

echo "1. Simple Console Output Test (should print 'ABC')"
echo "---------------------------------------------------"
$EMU tests/simple_con.com 2>&1 | grep -v "Loaded\|Program exit"
echo

echo "2. DJNZ Test (should print '321')"
echo "---------------------------------------------------"
$EMU tests/test_djnz.com 2>&1 | grep -v "Loaded\|Program exit"
echo

echo "3. N Flag Test"
echo "---------------------------------------------------"
echo "Expected: 20 02 00"
echo "Got:      $(src/cpm_emulator tests/test_n_flag.com 2>&1 | grep -v "Loaded\|Program exit" | tr -d '\n\r')"
echo

echo "4. Flag Comparison Test (tflags)"
echo "---------------------------------------------------"
echo "Expected: 94 51 10 3E"
echo "Got:      $(src/cpm_emulator tests/tflags.com 2>&1 | grep -v "Loaded\|Program exit" | tr -d '\n\r')"
echo

echo "5. zexdoc Test Suite (documented instructions)"
echo "---------------------------------------------------"
echo "Running zexdoc (this takes ~2 minutes)..."
timeout 180 $EMU tests/zexdoc.com 2>&1 | tail -5
echo

echo "6. zexall Test Suite (all instructions)"
echo "---------------------------------------------------"
echo "Running zexall (this takes ~2 minutes)..."
timeout 180 $EMU tests/zexall.com 2>&1 | tail -5
echo

echo "========================================"
echo "Test Suite Complete"
echo "========================================"
