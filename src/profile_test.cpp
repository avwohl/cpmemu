// Profile where time is spent
#include "qkz80.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define TPA_START 0x0100

int main(int argc, char** argv) {
    if (argc < 2) return 1;

    FILE* fp = fopen(argv[1], "rb");
    if (!fp) return 1;

    uint8_t program_buffer[0xE000];
    size_t loaded = fread(program_buffer, 1, 0xE000, fp);
    fclose(fp);

    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_Z80);
    cpu.regs.PC.set_pair16(TPA_START);
    cpu.regs.SP.set_pair16(0xFFF0);

    char* mem = cpu.get_mem();
    memcpy(&mem[TPA_START], program_buffer, loaded);

    // Count opcodes to find hotspots
    unsigned long opcode_counts[256] = {0};
    unsigned long pc_range_counts[16] = {0};  // Count by 4K ranges
    
    long total = 0;
    long limit = 100000;  // Sample first 100K instructions
    
    clock_t start = clock();
    
    while (total < limit) {
        uint16_t pc = cpu.regs.PC.get_pair16();
        if (pc == 0) break;
        
        uint8_t opcode = (uint8_t)mem[pc];
        opcode_counts[opcode]++;
        pc_range_counts[pc >> 12]++;  // 4K ranges
        
        cpu.execute();
        total++;
    }
    
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("Profiled %ld instructions in %.3f seconds (%.0f inst/sec)\n", 
           total, elapsed, total/elapsed);
    
    printf("\nTop 10 most executed opcodes:\n");
    for (int pass = 0; pass < 10; pass++) {
        int max_idx = 0;
        for (int i = 1; i < 256; i++) {
            if (opcode_counts[i] > opcode_counts[max_idx]) max_idx = i;
        }
        if (opcode_counts[max_idx] > 0) {
            printf("  0x%02X: %lu times (%.1f%%)\n", 
                   max_idx, opcode_counts[max_idx],
                   100.0 * opcode_counts[max_idx] / total);
            opcode_counts[max_idx] = 0;
        }
    }
    
    printf("\nExecution by address range:\n");
    for (int i = 0; i < 16; i++) {
        if (pc_range_counts[i] > 0) {
            printf("  0x%04X-0x%04X: %lu times (%.1f%%)\n",
                   i * 0x1000, (i+1) * 0x1000 - 1,
                   pc_range_counts[i],
                   100.0 * pc_range_counts[i] / total);
        }
    }
    
    return 0;
}
