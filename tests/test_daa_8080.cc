#include "src/qkz80.h"
#include "src/qkz80_cpu_flags.h"
#include <stdio.h>

void test_daa(qkz80& cpu, qkz80_uint8 a_val, qkz80_uint8 flags_before, const char* desc) {
    char* mem = cpu.get_mem();

    // Set up: load A and flags, then execute DAA
    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.set_reg8(a_val, qkz80::reg_A);
    cpu.regs.set_flags(flags_before);

    mem[0x100] = 0x27;  // DAA

    printf("%s:\n", desc);
    printf("  Before: A=0x%02X, Flags=0x%02X\n", a_val, flags_before);

    cpu.execute();  // DAA

    qkz80_uint8 result = cpu.get_reg8(qkz80::reg_A);
    qkz80_uint8 flags_after = cpu.regs.get_flags();

    printf("  After:  A=0x%02X, Flags=0x%02X [C=%d H=%d]\n\n",
           result, flags_after,
           (flags_after & qkz80_cpu_flags::CY) != 0,
           (flags_after & qkz80_cpu_flags::H) != 0);
}

int main() {
    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_8080);

    printf("Testing DAA instruction in 8080 mode:\n\n");

    // Test cases for addition (N=0 in Z80, but in 8080 bit 1 should be 1)
    // After ADD that resulted in 0x15 with no carries
    test_daa(cpu, 0x15, 0x02, "ADD result 0x15, no carries");

    // After ADD that resulted in 0x1A with no carries (needs adjustment)
    test_daa(cpu, 0x1A, 0x02, "ADD result 0x1A, no carries (low digit > 9)");

    // After ADD that resulted in 0x9C with no carries (needs adjustment)
    test_daa(cpu, 0x9C, 0x02, "ADD result 0x9C, no carries (high digit > 9)");

    // After ADD with half-carry set
    test_daa(cpu, 0x1A, 0x12, "ADD result 0x1A with H=1");

    // After ADD with carry set
    test_daa(cpu, 0x9A, 0x03, "ADD result 0x9A with C=1");

    // After ADD with both carries set
    test_daa(cpu, 0x9F, 0x13, "ADD result 0x9F with C=1, H=1");

    return 0;
}
