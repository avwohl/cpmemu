#include "src/qkz80.h"
#include "src/qkz80_cpu_flags.h"
#include <stdio.h>

int main() {
    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_8080);

    char* mem = cpu.get_mem();

    printf("Testing INR/DCR carry preservation in 8080 mode:\n\n");

    // Test INR with carry set
    mem[0x100] = 0x3E; mem[0x101] = 0xFF;  // MVI A,0xFF
    mem[0x102] = 0x37;                      // STC (set carry)
    mem[0x103] = 0x3C;                      // INR A

    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.regs.set_flags(0);

    cpu.execute();  // MVI A
    cpu.execute();  // STC
    printf("After STC: Flags=0x%02X, C=%d\n",
           cpu.regs.get_flags(),
           (cpu.regs.get_flags() & qkz80_cpu_flags::CY) != 0);

    cpu.execute();  // INR A
    printf("After INR: A=0x%02X, Flags=0x%02X, C=%d\n",
           cpu.get_reg8(qkz80::reg_A),
           cpu.regs.get_flags(),
           (cpu.regs.get_flags() & qkz80_cpu_flags::CY) != 0);
    printf("Expected: Carry should be preserved (C=1)\n\n");

    // Test DCR with carry clear
    mem[0x110] = 0x3E; mem[0x111] = 0x01;  // MVI A,0x01
    mem[0x112] = 0x3D;                      // DCR A

    cpu.set_reg16(0x110, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);  // Clear all flags

    cpu.execute();  // MVI A
    printf("Before DCR: Flags=0x%02X, C=%d\n",
           cpu.regs.get_flags(),
           (cpu.regs.get_flags() & qkz80_cpu_flags::CY) != 0);

    cpu.execute();  // DCR A
    printf("After DCR: A=0x%02X, Flags=0x%02X, C=%d\n",
           cpu.get_reg8(qkz80::reg_A),
           cpu.regs.get_flags(),
           (cpu.regs.get_flags() & qkz80_cpu_flags::CY) != 0);
    printf("Expected: Carry should be preserved (C=0)\n\n");

    // Test DCR with carry set
    mem[0x120] = 0x3E; mem[0x121] = 0x10;  // MVI A,0x10
    mem[0x122] = 0x37;                      // STC
    mem[0x123] = 0x3D;                      // DCR A

    cpu.set_reg16(0x120, qkz80::regp_PC);
    cpu.regs.set_flags(0);

    cpu.execute();  // MVI A
    cpu.execute();  // STC
    cpu.execute();  // DCR A
    printf("After DCR with carry set: A=0x%02X, Flags=0x%02X, C=%d\n",
           cpu.get_reg8(qkz80::reg_A),
           cpu.regs.get_flags(),
           (cpu.regs.get_flags() & qkz80_cpu_flags::CY) != 0);
    printf("Expected: Carry should be preserved (C=1)\n");

    return 0;
}
