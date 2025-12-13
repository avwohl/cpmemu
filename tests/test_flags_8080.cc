#include "src/qkz80.h"
#include "src/qkz80_cpu_flags.h"
#include <stdio.h>

int main() {
    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_8080);

    char* mem = cpu.get_mem();

    printf("Testing flag setting in 8080 mode:\n");
    printf("Expected: Bit 1 (0x02) always set, bits 3 and 5 (0x08, 0x20) always clear\n\n");

    // Test AND operation
    mem[0x100] = 0x3E; mem[0x101] = 0xFF;  // MVI A,0xFF
    mem[0x102] = 0x06; mem[0x103] = 0xFF;  // MVI B,0xFF
    mem[0x104] = 0xA0;                      // AND B (result = 0xFF)

    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    cpu.execute();  // AND B

    qkz80_uint8 flags = cpu.regs.get_flags();
    printf("AND 0xFF & 0xFF = 0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    printf("Flags=0x%02X ", flags);
    printf("[bit1=%d bit3=%d bit5=%d]\n",
           (flags & 0x02) ? 1 : 0,
           (flags & 0x08) ? 1 : 0,
           (flags & 0x20) ? 1 : 0);
    printf("Expected: bit1=1, bit3=0, bit5=0\n\n");

    // Test OR operation
    mem[0x110] = 0x3E; mem[0x111] = 0x0F;  // MVI A,0x0F
    mem[0x112] = 0x06; mem[0x113] = 0xF0;  // MVI B,0xF0
    mem[0x114] = 0xB0;                      // OR B (result = 0xFF)

    cpu.set_reg16(0x110, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    cpu.execute();  // OR B

    flags = cpu.regs.get_flags();
    printf("OR 0x0F | 0xF0 = 0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    printf("Flags=0x%02X ", flags);
    printf("[bit1=%d bit3=%d bit5=%d]\n",
           (flags & 0x02) ? 1 : 0,
           (flags & 0x08) ? 1 : 0,
           (flags & 0x20) ? 1 : 0);
    printf("Expected: bit1=1, bit3=0, bit5=0\n\n");

    // Test XOR operation
    mem[0x120] = 0x3E; mem[0x121] = 0xAA;  // MVI A,0xAA
    mem[0x122] = 0x06; mem[0x123] = 0x55;  // MVI B,0x55
    mem[0x124] = 0xA8;                      // XOR B (result = 0xFF)

    cpu.set_reg16(0x120, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    cpu.execute();  // XOR B

    flags = cpu.regs.get_flags();
    printf("XOR 0xAA ^ 0x55 = 0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    printf("Flags=0x%02X ", flags);
    printf("[bit1=%d bit3=%d bit5=%d]\n",
           (flags & 0x02) ? 1 : 0,
           (flags & 0x08) ? 1 : 0,
           (flags & 0x20) ? 1 : 0);
    printf("Expected: bit1=1, bit3=0, bit5=0\n\n");

    // Test ADD operation
    mem[0x130] = 0x3E; mem[0x131] = 0x01;  // MVI A,0x01
    mem[0x132] = 0x06; mem[0x133] = 0x01;  // MVI B,0x01
    mem[0x134] = 0x80;                      // ADD B (result = 0x02)

    cpu.set_reg16(0x130, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // MVI A
    cpu.execute();  // MVI B
    cpu.execute();  // ADD B

    flags = cpu.regs.get_flags();
    printf("ADD 0x01 + 0x01 = 0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    printf("Flags=0x%02X ", flags);
    printf("[bit1=%d bit3=%d bit5=%d]\n",
           (flags & 0x02) ? 1 : 0,
           (flags & 0x08) ? 1 : 0,
           (flags & 0x20) ? 1 : 0);
    printf("Expected: bit1=1, bit3=0, bit5=0\n\n");

    // Test INR operation
    mem[0x140] = 0x3E; mem[0x141] = 0x00;  // MVI A,0x00
    mem[0x142] = 0x3C;                      // INR A (result = 0x01)

    cpu.set_reg16(0x140, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // MVI A
    cpu.execute();  // INR A

    flags = cpu.regs.get_flags();
    printf("INR 0x00 -> 0x%02X\n", cpu.get_reg8(qkz80::reg_A));
    printf("Flags=0x%02X ", flags);
    printf("[bit1=%d bit3=%d bit5=%d]\n",
           (flags & 0x02) ? 1 : 0,
           (flags & 0x08) ? 1 : 0,
           (flags & 0x20) ? 1 : 0);
    printf("Expected: bit1=1, bit3=0, bit5=0\n");

    return 0;
}
