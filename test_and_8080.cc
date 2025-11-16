#include "src/qkz80.h"
#include "src/qkz80_cpu_flags.h"
#include <stdio.h>

void print_flags(const char* label, qkz80_uint8 flags) {
    printf("%s: 0x%02X [S=%d Z=%d H=%d P=%d N=%d C=%d X=%d Y=%d]\n",
           label, flags,
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

    // Test AND instruction
    // In 8080: AND should set H=1 (AC), C=0, calculate S,Z,P from result
    // Bit 1 should always be 1

    printf("Testing AND instruction in 8080 mode:\n\n");

    // Test 1: AND with result having even parity
    mem[0x100] = 0x3E; mem[0x101] = 0xFF;  // MVI A,0xFF
    mem[0x102] = 0x06; mem[0x103] = 0x0F;  // MVI B,0x0F
    mem[0x104] = 0xA0;                      // AND B

    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.regs.set_flags(0xFF);  // Start with all flags set

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    printf("Before AND: A=0x%02X, B=0x%02X\n",
           cpu.get_reg8(qkz80::reg_A), cpu.get_reg8(qkz80::reg_B));
    print_flags("Flags", cpu.regs.get_flags());

    cpu.execute();  // AND B
    printf("\nAfter AND:  A=0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    print_flags("Flags", cpu.regs.get_flags());
    printf("Expected: 0x0F, H=1, C=0, S=0, Z=0, P=1 (even parity), N=1 (bit1)\n");
    printf("          0x0F has 4 bits set = even parity\n");

    // Test 2: AND resulting in zero
    mem[0x110] = 0x3E; mem[0x111] = 0xF0;  // MVI A,0xF0
    mem[0x112] = 0x06; mem[0x113] = 0x0F;  // MVI B,0x0F
    mem[0x114] = 0xA0;                      // AND B

    cpu.set_reg16(0x110, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    printf("\n\nBefore AND: A=0x%02X, B=0x%02X\n",
           cpu.get_reg8(qkz80::reg_A), cpu.get_reg8(qkz80::reg_B));

    cpu.execute();  // AND B
    printf("After AND:  A=0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    print_flags("Flags", cpu.regs.get_flags());
    printf("Expected: 0x00, H=1, C=0, S=0, Z=1, P=1 (even parity), N=1 (bit1)\n");

    // Test 3: AND with negative result
    mem[0x120] = 0x3E; mem[0x121] = 0xFF;  // MVI A,0xFF
    mem[0x122] = 0x06; mem[0x123] = 0x80;  // MVI B,0x80
    mem[0x124] = 0xA0;                      // AND B

    cpu.set_reg16(0x120, qkz80::regp_PC);
    cpu.regs.set_flags(0x01);  // Set carry to test it gets cleared

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    cpu.execute();  // AND B
    printf("\n\nAfter AND 0xFF & 0x80:\n");
    printf("Result: A=0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    print_flags("Flags", cpu.regs.get_flags());
    printf("Expected: 0x80, H=1, C=0 (cleared!), S=1, Z=0, P=0 (odd), N=1 (bit1)\n");
    printf("          0x80 has 1 bit set = odd parity\n");

    return 0;
}
