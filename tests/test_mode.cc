#include "src/qkz80.h"
#include <stdio.h>

int main() {
    qkz80 cpu;

    printf("Initial mode - qkz80: %d, regs: %d\n",
           (int)cpu.get_cpu_mode(), (int)cpu.regs.cpu_mode);

    cpu.set_cpu_mode(qkz80::MODE_8080);

    printf("After set_cpu_mode(MODE_8080) - qkz80: %d, regs: %d\n",
           (int)cpu.get_cpu_mode(), (int)cpu.regs.cpu_mode);

    return 0;
}
