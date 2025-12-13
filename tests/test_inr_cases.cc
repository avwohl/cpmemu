#include "src/qkz80.h"
#include "src/qkz80_cpu_flags.h"
#include <stdio.h>

void test_inr(qkz80& cpu, qkz80_uint8 value) {
    char* mem = cpu.get_mem();

    // MVI A,value / INR A
    mem[0x100] = 0x3E;  // MVI A,
    mem[0x101] = value;
    mem[0x102] = 0x3C;  // INR A

    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.regs.set_flags(0);

    cpu.execute();  // MVI A
    cpu.execute();  // INR A

    qkz80_uint8 result = cpu.get_reg8(qkz80::reg_A);
    qkz80_uint8 flags = cpu.regs.get_flags();

    printf("INR 0x%02X -> 0x%02X, Flags=0x%02X [S=%d Z=%d H=%d P=%d]\n",
           value, result, flags,
           (flags & qkz80_cpu_flags::S) != 0,
           (flags & qkz80_cpu_flags::Z) != 0,
           (flags & qkz80_cpu_flags::H) != 0,
           (flags & qkz80_cpu_flags::P) != 0);
}

int main() {
    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_8080);

    printf("Testing INR instruction with various values:\n\n");

    // Test cases that stress half-carry
    test_inr(cpu, 0x0F);  // 0x0F -> 0x10, H should be 1
    test_inr(cpu, 0x10);  // 0x10 -> 0x11, H should be 0
    test_inr(cpu, 0x1F);  // 0x1F -> 0x20, H should be 1
    test_inr(cpu, 0xFF);  // 0xFF -> 0x00, H should be 1, Z should be 1
    test_inr(cpu, 0x7F);  // 0x7F -> 0x80, H should be 1, S should be 1, P should be 1 (overflow)
    test_inr(cpu, 0x00);  // 0x00 -> 0x01, H should be 0

    return 0;
}
