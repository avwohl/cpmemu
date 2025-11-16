#include "src/qkz80.h"
#include "src/qkz80_cpu_flags.h"
#include <stdio.h>

void print_flags(qkz80_uint8 flags) {
    printf("Flags: 0x%02X = ", flags);
    printf("S=%d Z=%d H=%d P=%d N=%d C=%d X=%d Y=%d\n",
           (flags & qkz80_cpu_flags::S) != 0,
           (flags & qkz80_cpu_flags::Z) != 0,
           (flags & qkz80_cpu_flags::H) != 0,
           (flags & qkz80_cpu_flags::P) != 0,
           (flags & qkz80_cpu_flags::N) != 0,
           (flags & qkz80_cpu_flags::CY) != 0,
           (flags & qkz80_cpu_flags::X) != 0,
           (flags & qkz80_cpu_flags::Y) != 0);
}

int main() {
    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_8080);

    // Test INR A: 0xFF -> 0x00
    // Expected 8080 flags: Z=1, S=0, P=1 (parity), AC=1, bit1=1, bit3=0, bit5=0
    char* mem = cpu.get_mem();

    // Program: MVI A,FFH / INR A / HLT
    mem[0x100] = 0x3E;  // MVI A,
    mem[0x101] = 0xFF;  // 0xFF
    mem[0x102] = 0x3C;  // INR A
    mem[0x103] = 0x76;  // HLT

    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.set_reg8(0, qkz80::reg_A);
    cpu.regs.set_flags(0);

    printf("Before MVI A,0xFF:\n");
    printf("  A=0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());

    cpu.execute();  // MVI A,0xFF
    printf("\nAfter MVI A,0xFF:\n");
    printf("  A=0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());

    cpu.execute();  // INR A
    printf("\nAfter INR A (0xFF -> 0x00):\n");
    printf("  A=0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());
    printf("\n8080 Expected: Z=1, S=0, H=1, P=1, N=1 (bit1), C=0, X=0, Y=0\n");
    printf("               Binary: SZ0H0P1C = 01010110 = 0x56\n");

    return 0;
}
