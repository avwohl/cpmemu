#include "src/qkz80.h"
#include "src/qkz80_cpu_flags.h"
#include <stdio.h>

void print_flags(qkz80_uint8 flags) {
    printf("0x%02X [S=%d Z=%d H=%d P=%d N=%d C=%d X=%d Y=%d]",
           flags,
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

    char* mem = cpu.get_mem();

    // Test 1: ADD with carry out
    // MVI A,FFH / MVI B,01H / ADD B
    mem[0x100] = 0x3E; mem[0x101] = 0xFF;  // MVI A,0xFF
    mem[0x102] = 0x06; mem[0x103] = 0x01;  // MVI B,0x01
    mem[0x104] = 0x80;                      // ADD B

    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.regs.set_flags(0);

    cpu.execute();  // MVI A,0xFF
    cpu.execute();  // MVI B,0x01
    cpu.execute();  // ADD B

    printf("Test 1: ADD 0xFF + 0x01 = 0x00\n");
    printf("  A=0x%02X, Flags=", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());
    printf("\n  Expected: Z=1, C=1, H=1, P=1, S=0, N=1 (bit1)\n");
    printf("            0x%02X\n", (0x40 | 0x10 | 0x04 | 0x02 | 0x01));

    // Test 2: SUB with borrow
    mem[0x110] = 0x3E; mem[0x111] = 0x00;  // MVI A,0x00
    mem[0x112] = 0x06; mem[0x113] = 0x01;  // MVI B,0x01
    mem[0x114] = 0x90;                      // SUB B

    cpu.set_reg16(0x110, qkz80::regp_PC);
    cpu.regs.set_flags(0);

    cpu.execute();  // MVI A,0x00
    cpu.execute();  // MVI B,0x01
    cpu.execute();  // SUB B

    printf("\nTest 2: SUB 0x00 - 0x01 = 0xFF\n");
    printf("  A=0x%02X, Flags=", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());
    printf("\n  Expected: Z=0, C=1, H=1, P=1, S=1, N=1 (bit1)\n");
    printf("            0x%02X\n", (0x80 | 0x10 | 0x04 | 0x02 | 0x01));

    return 0;
}
