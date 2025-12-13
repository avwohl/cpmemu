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

    // Test RLC (Rotate Left Circular)
    // In 8080: Only affects carry flag, clears N and H, preserves others
    mem[0x100] = 0x3E; mem[0x101] = 0x80;  // MVI A,0x80
    mem[0x102] = 0x07;                      // RLC

    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);  // Clear all flags

    cpu.execute();  // MVI A,0x80
    printf("Before RLC: A=0x%02X, Flags=", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());
    printf("\n");

    cpu.execute();  // RLC
    printf("After RLC:  A=0x%02X, Flags=", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());
    printf("\n");
    printf("Expected: A=0x01, Carry=1, N=0, H=0, bit1=1\n");
    printf("          All other flags (S,Z,P) preserved from before\n\n");

    // Test with different initial flags
    mem[0x110] = 0x3E; mem[0x111] = 0x01;  // MVI A,0x01
    mem[0x112] = 0x0F;                      // RRC

    cpu.set_reg16(0x110, qkz80::regp_PC);
    cpu.regs.set_flags(0xFF);  // Set all flags

    cpu.execute();  // MVI A,0x01
    printf("Before RRC: A=0x%02X, Flags=", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());
    printf("\n");

    cpu.execute();  // RRC
    printf("After RRC:  A=0x%02X, Flags=", cpu.get_reg8(qkz80::reg_A));
    print_flags(cpu.regs.get_flags());
    printf("\n");
    printf("Expected: A=0x80, Carry=1, N=0, H=0, bit1=1\n");
    printf("          All other flags (S,Z,P) preserved\n");

    return 0;
}
