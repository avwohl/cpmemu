// Check what's in low memory
#include "qkz80.h"
#include <stdio.h>
#include <string.h>

int main() {
    qkz80 cpu;
    cpu.set_cpu_mode(qkz80::MODE_Z80);
    cpu.cpm_setup_memory();
    
    char* mem = cpu.get_mem();
    
    printf("First 256 bytes of memory:\n");
    for (int i = 0; i < 256; i += 16) {
        printf("%04X: ", i);
        for (int j = 0; j < 16; j++) {
            printf("%02X ", (unsigned char)mem[i+j]);
        }
        printf("\n");
    }
    
    return 0;
}
