#include "src/qkz80.h"
#include "src/qkz80_cpu_flags.h"
#include <stdio.h>

int main() {
    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_8080);

    char* mem = cpu.get_mem();

    printf("Testing MVI flag preservation in 8080 mode:\n\n");

    // Test 1: MVI should preserve all flags
    mem[0x100] = 0x37;                      // STC (set carry and other flags)
    mem[0x101] = 0x3E; mem[0x102] = 0x42;  // MVI A,0x42

    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.regs.set_flags(0x00);

    cpu.execute();  // STC
    qkz80_uint8 flags_after_stc = cpu.regs.get_flags();
    printf("After STC: Flags=0x%02X\n", flags_after_stc);

    cpu.execute();  // MVI A,0x42
    qkz80_uint8 flags_after_mvi = cpu.regs.get_flags();
    printf("After MVI: Flags=0x%02X\n", flags_after_mvi);

    if (flags_after_stc == flags_after_mvi) {
        printf("✓ MVI correctly preserved all flags\n\n");
    } else {
        printf("✗ MVI modified flags! Before=0x%02X, After=0x%02X\n\n",
               flags_after_stc, flags_after_mvi);
    }

    // Test 2: MVI with all flags set
    mem[0x110] = 0x3E; mem[0x111] = 0xFF;  // MVI A,0xFF

    cpu.set_reg16(0x110, qkz80::regp_PC);
    cpu.regs.set_flags(0xFF);  // Set all flags

    printf("Before MVI with all flags set: Flags=0x%02X\n", cpu.regs.get_flags());
    cpu.execute();  // MVI A,0xFF
    printf("After MVI:  Flags=0x%02X\n", cpu.regs.get_flags());

    if (cpu.regs.get_flags() == 0xFF) {
        printf("✓ MVI preserved all flags when all were set\n\n");
    } else {
        printf("✗ MVI modified flags when all were set!\n\n");
    }

    // Test 3: Check other MVI variants
    mem[0x120] = 0x06; mem[0x121] = 0x55;  // MVI B,0x55

    cpu.set_reg16(0x120, qkz80::regp_PC);
    cpu.regs.set_flags(0xAA);  // Set some flags

    printf("Before MVI B: Flags=0x%02X\n", cpu.regs.get_flags());
    cpu.execute();  // MVI B,0x55
    printf("After MVI B: Flags=0x%02X\n", cpu.regs.get_flags());

    if (cpu.regs.get_flags() == 0xAA) {
        printf("✓ MVI B preserved flags\n");
    } else {
        printf("✗ MVI B modified flags!\n");
    }

    return 0;
}
