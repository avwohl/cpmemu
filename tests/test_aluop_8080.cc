#include "src/qkz80.h"
#include "src/qkz80_cpu_flags.h"
#include <stdio.h>

void print_flags(qkz80_uint8 flags) {
    printf("Flags=0x%02X [S=%d Z=%d H=%d P=%d N=%d C=%d]",
           flags,
           (flags & qkz80_cpu_flags::S) != 0,
           (flags & qkz80_cpu_flags::Z) != 0,
           (flags & qkz80_cpu_flags::H) != 0,
           (flags & qkz80_cpu_flags::P) != 0,
           (flags & qkz80_cpu_flags::N) != 0,
           (flags & qkz80_cpu_flags::CY) != 0);
}

int main() {
    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_8080);

    char* mem = cpu.get_mem();

    printf("Testing ALU operations in 8080 mode:\n\n");

    // Test ADD with half-carry
    mem[0x100] = 0x3E; mem[0x101] = 0x0E;  // MVI A,0x0E
    mem[0x102] = 0x06; mem[0x103] = 0x02;  // MVI B,0x02
    mem[0x104] = 0x80;                      // ADD B

    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    cpu.execute();  // ADD B

    printf("ADD 0x0E + 0x02 = 0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());
    printf("\nExpected: 0x10, H=1 (carry from bit 3)\n\n");

    // Test ADC with carry in
    mem[0x110] = 0x3E; mem[0x111] = 0xFF;  // MVI A,0xFF
    mem[0x112] = 0x06; mem[0x113] = 0x01;  // MVI B,0x01
    mem[0x114] = 0x37;                      // STC (set carry)
    mem[0x115] = 0x88;                      // ADC B

    cpu.set_reg16(0x110, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    cpu.execute();  // STC
    cpu.execute();  // ADC B

    printf("ADC 0xFF + 0x01 + carry(1) = 0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());
    printf("\nExpected: 0x01, C=1, H=1, Z=0, P=0\n\n");

    // Test SUB
    mem[0x120] = 0x3E; mem[0x121] = 0x10;  // MVI A,0x10
    mem[0x122] = 0x06; mem[0x123] = 0x01;  // MVI B,0x01
    mem[0x124] = 0x90;                      // SUB B

    cpu.set_reg16(0x120, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    cpu.execute();  // SUB B

    printf("SUB 0x10 - 0x01 = 0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());
    printf("\nExpected: 0x0F, H=1 (borrow from bit 4), C=0\n\n");

    // Test SBB with borrow
    mem[0x130] = 0x3E; mem[0x131] = 0x00;  // MVI A,0x00
    mem[0x132] = 0x06; mem[0x133] = 0x01;  // MVI B,0x01
    mem[0x134] = 0x37;                      // STC (set carry)
    mem[0x135] = 0x98;                      // SBB B

    cpu.set_reg16(0x130, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    cpu.execute();  // STC
    cpu.execute();  // SBB B

    printf("SBB 0x00 - 0x01 - carry(1) = 0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());
    printf("\nExpected: 0xFE, C=1, H=1\n\n");

    // Test CP (compare)
    mem[0x140] = 0x3E; mem[0x141] = 0x05;  // MVI A,0x05
    mem[0x142] = 0x06; mem[0x143] = 0x05;  // MVI B,0x05
    mem[0x144] = 0xB8;                      // CP B

    cpu.set_reg16(0x140, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    qkz80_uint8 a_before = cpu.get_reg8(qkz80::reg_A);
    cpu.execute();  // CP B
    qkz80_uint8 a_after = cpu.get_reg8(qkz80::reg_A);

    printf("CP 0x05 with 0x05: A before=0x%02X, after=0x%02X\n", a_before, a_after);
    print_flags(cpu.regs.get_flags());
    printf("\nExpected: A unchanged, Z=1, C=0 (equal)\n");

    return 0;
}
