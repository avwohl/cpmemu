#!/bin/bash

# Script to compare flag results between our emulator and tnylpo
# For tnylpo, we examine the stack after PUSH AF

cd /home/wohl/newcpm

echo "=== Comparing Flag Results ==="
echo

for test in test1_overflow test2_carry_zero test3_halfcarry test4_sub_overflow; do
    echo "--- $test ---"

    # Run on our emulator
    echo "Our emulator:"
    /home/wohl/qkz80/src/cpm_emulator /home/wohl/qkz80/tests/$test.com 2>&1 | grep "Flags"

    # For tnylpo, we'd need to manually use ZSID
    echo "To check with tnylpo/ZSID:"
    echo "  cd /home/wohl/newcpm"
    echo "  tnylpo ZSID $test"
    echo "  Set breakpoint at 0x105 (after PUSH AF)"
    echo "  Run with G100"
    echo "  Examine stack: DFFEE (SP points to pushed flags)"
    echo
done
