// Simple test to see if we're making progress or stuck
#include "qkz80.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define TPA_START 0x0100

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <test.com>\n", argv[0]);
        return 1;
    }

    FILE* fp = fopen(argv[1], "rb");
    if (!fp) {
        fprintf(stderr, "Cannot open %s\n", argv[1]);
        return 1;
    }

    uint8_t program_buffer[0xE000];
    size_t loaded = fread(program_buffer, 1, 0xE000, fp);
    fclose(fp);

    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_Z80);
    cpu.regs.PC.set_pair16(TPA_START);
    cpu.regs.SP.set_pair16(0xFFF0);

    char* mem = cpu.get_mem();
    memcpy(&mem[TPA_START], program_buffer, loaded);

    printf("Running %s, will print progress every 100K instructions...\n", argv[1]);

    long count = 0;
    long total = 0;
    uint16_t last_pc = TPA_START;

    while (total < 10000000) {  // Max 10M instructions
        uint16_t pc = cpu.regs.PC.get_pair16();

        if (pc == 0) {
            printf("\nProgram exited after %ld instructions\n", total);
            break;
        }

        cpu.execute();
        count++;
        total++;

        if (count >= 100000) {
            printf("  %ld instructions (PC=0x%04X, opcode=0x%02X)\n",
                   total, last_pc, (uint8_t)mem[last_pc]);
            count = 0;
        }

        last_pc = pc;
    }

    if (total >= 10000000) {
        printf("\nReached 10M instruction limit - likely stuck\n");
        printf("Last PC: 0x%04X, opcode: 0x%02X\n", last_pc, (uint8_t)mem[last_pc]);
    }

    return 0;
}
