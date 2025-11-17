#include "src/qkz80.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program.com>\n", argv[0]);
        return 1;
    }

    // Create CPU in 8080 mode
    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_8080);

    // Load program
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", argv[1]);
        return 1;
    }

    // Read program into memory at 0x100 (CP/M TPA start)
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char* buffer = (unsigned char*)malloc(size);
    fread(buffer, 1, size, fp);
    fclose(fp);

    char* mem = cpu.get_mem();
    for (int i = 0; i < size; i++) {
        mem[0x100 + i] = buffer[i];
    }
    free(buffer);

    printf("Loaded %ld bytes from %s\n", size, argv[1]);

    // Set up CP/M environment
    mem[0x0000] = 0xC3;  // JMP to warm boot
    mem[0x0001] = 0x00;
    mem[0x0002] = 0x00;

    mem[0x0005] = 0xC9;  // RET at BDOS entry (will return to 0)

    // Set PC to 0x100
    cpu.set_reg16(0x100, qkz80::regp_PC);
    cpu.set_reg16(0xF000, qkz80::regp_SP);  // Set stack

    // Run
    int instructions = 0;
    while (true) {
        qkz80_uint16 pc = cpu.get_reg16(qkz80::regp_PC);

        // Check for halt or return to 0
        if (pc == 0x0000) {
            printf("\nProgram exit via JMP 0 (SP=0x%04X)\n", cpu.get_reg16(qkz80::regp_SP));
            break;
        }

        // Handle BDOS console output call (INT 5, C=2)
        if (pc == 0x0005) {
            qkz80_uint8 func = cpu.get_reg8(qkz80::reg_C);
            if (func == 2) {  // Console output
                qkz80_uint8 ch = cpu.get_reg8(qkz80::reg_E);
                putchar(ch);
                fflush(stdout);
            } else if (func == 9) {  // Print string
                qkz80_uint16 addr = cpu.get_reg16(qkz80::regp_DE);
                while (true) {
                    qkz80_uint8 ch = mem[addr++];
                    if (ch == '$') break;
                    putchar(ch);
                }
                fflush(stdout);
            }
            // Return from BDOS call
            qkz80_uint16 sp = cpu.get_reg16(qkz80::regp_SP);
            qkz80_uint16 ret_addr = mem[sp] | (mem[sp+1] << 8);
            cpu.set_reg16(ret_addr, qkz80::regp_PC);
            cpu.set_reg16(sp + 2, qkz80::regp_SP);
            continue;
        }

        cpu.execute();
        instructions++;

        if (instructions > 100000000) {
            printf("\nExceeded instruction limit\n");
            break;
        }
    }

    return 0;
}
