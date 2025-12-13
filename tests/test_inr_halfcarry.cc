#include "src/qkz80.h"
#include "src/qkz80_cpu_flags.h"
#include <stdio.h>

void test_inr_case(qkz80& cpu, qkz80_uint8 value, const char* desc) {
    char* mem = cpu.get_mem();

    mem[0x100] = 0x3E;  // MVI A,
    mem[0x101] = value;
    mem[0x102] = 0x3C;  // INR A

    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);  // Start with all flags clear

    cpu.execute();  // MVI A
    cpu.execute();  // INR A

    qkz80_uint8 result = cpu.get_reg8(qkz80::reg_A);
    qkz80_uint8 flags = cpu.regs.get_flags();

    printf("%-30s: 0x%02X -> 0x%02X, H=%d (flags=0x%02X)\n",
           desc, value, result,
           (flags & qkz80_cpu_flags::H) != 0,
           flags);
}

void test_dcr_case(qkz80& cpu, qkz80_uint8 value, const char* desc) {
    char* mem = cpu.get_mem();

    mem[0x100] = 0x3E;  // MVI A,
    mem[0x101] = value;
    mem[0x102] = 0x3D;  // DCR A

    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);  // Start with all flags clear

    cpu.execute();  // MVI A
    cpu.execute();  // DCR A

    qkz80_uint8 result = cpu.get_reg8(qkz80::reg_A);
    qkz80_uint8 flags = cpu.regs.get_flags();

    printf("%-30s: 0x%02X -> 0x%02X, H=%d (flags=0x%02X)\n",
           desc, value, result,
           (flags & qkz80_cpu_flags::H) != 0,
           flags);
}

int main() {
    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_8080);

    printf("Testing INR half-carry in 8080 mode:\n");
    printf("(Half-carry should be set when carry from bit 3 to bit 4)\n\n");

    test_inr_case(cpu, 0x0F, "INR 0x0F (should set H)");
    test_inr_case(cpu, 0x0E, "INR 0x0E (should NOT set H)");
    test_inr_case(cpu, 0x1F, "INR 0x1F (should set H)");
    test_inr_case(cpu, 0x1E, "INR 0x1E (should NOT set H)");
    test_inr_case(cpu, 0xFF, "INR 0xFF (should set H)");
    test_inr_case(cpu, 0xFE, "INR 0xFE (should NOT set H)");
    test_inr_case(cpu, 0x00, "INR 0x00 (should NOT set H)");

    printf("\nTesting DCR half-carry in 8080 mode:\n");
    printf("(Half-carry should be set when borrow from bit 4 to bit 3)\n\n");

    test_dcr_case(cpu, 0x00, "DCR 0x00 (should set H)");
    test_dcr_case(cpu, 0x01, "DCR 0x01 (should NOT set H)");
    test_dcr_case(cpu, 0x10, "DCR 0x10 (should set H)");
    test_dcr_case(cpu, 0x11, "DCR 0x11 (should NOT set H)");
    test_dcr_case(cpu, 0x20, "DCR 0x20 (should set H)");
    test_dcr_case(cpu, 0x21, "DCR 0x21 (should NOT set H)");

    return 0;
}
