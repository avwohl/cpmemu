#include "src/qkz80.h"
#include "src/qkz80_cpu_flags.h"
#include <stdio.h>

int main() {
    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_8080);

    // Test: Set some flags including X, Y, N
    qkz80_uint8 test_flags = qkz80_cpu_flags::CY | qkz80_cpu_flags::Z |
                              qkz80_cpu_flags::X | qkz80_cpu_flags::Y |
                              qkz80_cpu_flags::N;

    printf("Input flags: 0x%02X\n", test_flags);
    printf("  CY=%d Z=%d N=%d X=%d Y=%d\n",
           (test_flags & qkz80_cpu_flags::CY) != 0,
           (test_flags & qkz80_cpu_flags::Z) != 0,
           (test_flags & qkz80_cpu_flags::N) != 0,
           (test_flags & qkz80_cpu_flags::X) != 0,
           (test_flags & qkz80_cpu_flags::Y) != 0);

    cpu.regs.set_flags(test_flags);
    qkz80_uint8 result_flags = cpu.regs.get_flags();

    printf("After set_flags/get_flags: 0x%02X\n", result_flags);
    printf("  CY=%d Z=%d N=%d X=%d Y=%d\n",
           (result_flags & qkz80_cpu_flags::CY) != 0,
           (result_flags & qkz80_cpu_flags::Z) != 0,
           (result_flags & qkz80_cpu_flags::N) != 0,
           (result_flags & qkz80_cpu_flags::X) != 0,
           (result_flags & qkz80_cpu_flags::Y) != 0);

    printf("\nExpected for 8080 mode: N=1 (forced), X=0, Y=0\n");

    return 0;
}
