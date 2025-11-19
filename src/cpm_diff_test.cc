/*
 * Differential Testing Harness for qkz80 vs superzazu i8080
 *
 * This program runs both emulators in lockstep on the same program,
 * comparing registers after each instruction to find the first divergence.
 */

#include "qkz80.h"
#include "i8080.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MEMORY_SIZE 0x10000
#define TPA_START 0x0100

// Separate memory for i8080 (superzazu's emulator needs its own space)
static uint8_t i8080_memory[MEMORY_SIZE];

// Memory callbacks for superzazu's i8080
static uint8_t rb_i8080(void* userdata, uint16_t addr) {
  return i8080_memory[addr];
}

static void wb_i8080(void* userdata, uint16_t addr, uint8_t val) {
  i8080_memory[addr] = val;
}

static uint8_t port_in(void* userdata, uint8_t port) {
  return 0x00;
}

static void port_out(void* userdata, uint8_t port, uint8_t value) {
  // Ignore port I/O for differential testing
}

// Helper to get flags from qkz80
struct qkz80_flags {
  bool sf, zf, hf, pf, cf;
};

qkz80_flags get_qkz80_flags(qkz80* cpu) {
  qkz80_flags f;
  qkz80_uint16 af = cpu->get_reg16(qkz80::regp_AF);
  qkz80_uint8 flags = af & 0xFF;

  f.sf = (flags & 0x80) != 0;  // Sign flag (bit 7)
  f.zf = (flags & 0x40) != 0;  // Zero flag (bit 6)
  f.hf = (flags & 0x10) != 0;  // Half-carry flag (bit 4)
  f.pf = (flags & 0x04) != 0;  // Parity flag (bit 2)
  f.cf = (flags & 0x01) != 0;  // Carry flag (bit 0)

  return f;
}

// Compare both emulator states
bool compare_state(qkz80* qk, i8080* i8, uint16_t prev_pc, uint8_t prev_opcode, uint8_t prev_a, int instr_count) {
  bool match = true;

  // Get register values from both emulators
  uint16_t qk_pc = qk->regs.PC.get_pair16();
  uint16_t qk_sp = qk->get_reg16(qkz80::regp_SP);
  uint16_t qk_bc = qk->get_reg16(qkz80::regp_BC);
  uint16_t qk_de = qk->get_reg16(qkz80::regp_DE);
  uint16_t qk_hl = qk->get_reg16(qkz80::regp_HL);
  uint8_t qk_a = qk->get_reg8(qkz80::reg_A);
  qkz80_flags qk_f = get_qkz80_flags(qk);

  uint16_t i8_pc = i8->pc;
  uint16_t i8_sp = i8->sp;
  uint16_t i8_bc = (i8->b << 8) | i8->c;
  uint16_t i8_de = (i8->d << 8) | i8->e;
  uint16_t i8_hl = (i8->h << 8) | i8->l;
  uint8_t i8_a = i8->a;

  // Compare registers
  if (qk_pc != i8_pc) {
    printf("MISMATCH at instruction %d: PC: qkz80=0x%04X vs i8080=0x%04X\n",
           instr_count, qk_pc, i8_pc);
    match = false;
  }
  if (qk_sp != i8_sp) {
    printf("MISMATCH at instruction %d: SP: qkz80=0x%04X vs i8080=0x%04X\n",
           instr_count, qk_sp, i8_sp);
    match = false;
  }
  if (qk_a != i8_a) {
    printf("MISMATCH at instruction %d: A: qkz80=0x%02X vs i8080=0x%02X\n",
           instr_count, qk_a, i8_a);
    match = false;
  }
  if (qk_bc != i8_bc) {
    printf("MISMATCH at instruction %d: BC: qkz80=0x%04X vs i8080=0x%04X\n",
           instr_count, qk_bc, i8_bc);
    match = false;
  }
  if (qk_de != i8_de) {
    printf("MISMATCH at instruction %d: DE: qkz80=0x%04X vs i8080=0x%04X\n",
           instr_count, qk_de, i8_de);
    match = false;
  }
  if (qk_hl != i8_hl) {
    printf("MISMATCH at instruction %d: HL: qkz80=0x%04X vs i8080=0x%04X\n",
           instr_count, qk_hl, i8_hl);
    match = false;
  }

  // Compare flags
  if (qk_f.sf != i8->sf) {
    printf("MISMATCH at instruction %d: SF: qkz80=%d vs i8080=%d\n",
           instr_count, qk_f.sf, i8->sf);
    match = false;
  }
  if (qk_f.zf != i8->zf) {
    printf("MISMATCH at instruction %d: ZF: qkz80=%d vs i8080=%d\n",
           instr_count, qk_f.zf, i8->zf);
    match = false;
  }
  if (qk_f.hf != i8->hf) {
    printf("MISMATCH at instruction %d: HF: qkz80=%d vs i8080=%d\n",
           instr_count, qk_f.hf, i8->hf);
    match = false;
  }
  if (qk_f.pf != i8->pf) {
    printf("MISMATCH at instruction %d: PF: qkz80=%d vs i8080=%d\n",
           instr_count, qk_f.pf, i8->pf);
    match = false;
  }
  if (qk_f.cf != i8->cf) {
    printf("MISMATCH at instruction %d: CF: qkz80=%d vs i8080=%d\n",
           instr_count, qk_f.cf, i8->cf);
    match = false;
  }

  if (!match) {
    printf("\nDIVERGENCE DETECTED!\n");
    printf("Previous PC: 0x%04X, Opcode: 0x%02X", prev_pc, prev_opcode);

    // Show instruction bytes for immediate operands
    char* qkz80_mem = qk->get_mem();
    if (prev_opcode == 0xFE || prev_opcode == 0xD6 || prev_opcode == 0xE6 ||
        prev_opcode == 0xF6 || prev_opcode == 0xEE || prev_opcode == 0xC6 ||
        prev_opcode == 0xCE || prev_opcode == 0xDE) {
      // Instructions with immediate byte operand
      printf(" %02X", (uint8_t)qkz80_mem[prev_pc + 1]);
    }
    printf(" (A before: 0x%02X)\n", prev_a);

    printf("\nqkz80 state:\n");
    printf("  PC=%04X SP=%04X A=%02X BC=%04X DE=%04X HL=%04X\n",
           qk_pc, qk_sp, qk_a, qk_bc, qk_de, qk_hl);
    printf("  Flags: S=%d Z=%d H=%d P=%d C=%d\n",
           qk_f.sf, qk_f.zf, qk_f.hf, qk_f.pf, qk_f.cf);
    printf("\ni8080 state:\n");
    printf("  PC=%04X SP=%04X A=%02X BC=%04X DE=%04X HL=%04X\n",
           i8_pc, i8_sp, i8_a, i8_bc, i8_de, i8_hl);
    printf("  Flags: S=%d Z=%d H=%d P=%d C=%d\n",
           i8->sf, i8->zf, i8->hf, i8->pf, i8->cf);
    return false;
  }

  return true;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <test.com> [max_instructions]\n", argv[0]);
    fprintf(stderr, "\nRuns both qkz80 and superzazu i8080 emulators side-by-side\n");
    fprintf(stderr, "and compares their state after each instruction.\n");
    fprintf(stderr, "\nExample: %s cpu_tests/TST8080.COM 10000\n", argv[0]);
    return 1;
  }

  const char* program = argv[1];
  long max_instructions = (argc >= 3) ? atol(argv[2]) : 1000000L;

  // Load program into temporary buffer
  FILE* fp = fopen(program, "rb");
  if (!fp) {
    fprintf(stderr, "Cannot open %s\n", program);
    return 1;
  }

  uint8_t program_buffer[0xE000];
  size_t loaded = fread(program_buffer, 1, 0xE000, fp);
  fclose(fp);

  printf("Loaded %zu bytes from %s\n", loaded, program);

  // Initialize qkz80 (our emulator) with its own memory
  qkz80 qk_cpu;
  qk_cpu.set_cpu_mode(qkz80::MODE_8080);
  qk_cpu.regs.PC.set_pair16(TPA_START);
  qk_cpu.regs.SP.set_pair16(0xFFF0);

  // Copy program to qkz80's memory
  char* qkz80_mem = qk_cpu.get_mem();
  memcpy(&qkz80_mem[TPA_START], program_buffer, loaded);

  // Initialize i8080 (superzazu's emulator) with its own memory
  memset(i8080_memory, 0, MEMORY_SIZE);
  memcpy(&i8080_memory[TPA_START], program_buffer, loaded);

  i8080 i8_cpu;
  i8080_init(&i8_cpu);
  i8_cpu.read_byte = rb_i8080;
  i8_cpu.write_byte = wb_i8080;
  i8_cpu.port_in = port_in;
  i8_cpu.port_out = port_out;
  i8_cpu.userdata = &i8_cpu;
  i8_cpu.pc = TPA_START;
  i8_cpu.sp = 0xFFF0;

  printf("Running differential test (8080 mode)...\n");
  printf("Will stop at first mismatch or after %ld instructions.\n\n", max_instructions);

  // Run both emulators in lockstep
  int instr_count = 0;
  uint16_t prev_pc = TPA_START;
  uint8_t prev_opcode = 0;
  uint8_t prev_a = 0;

  while (instr_count < max_instructions) {
    // Save current state before execution
    prev_pc = qk_cpu.regs.PC.get_pair16();
    prev_opcode = (uint8_t)qkz80_mem[prev_pc];
    prev_a = qk_cpu.get_reg8(qkz80::reg_A);

    // Execute one instruction on each emulator
    qk_cpu.execute();
    i8080_step(&i8_cpu);

    instr_count++;

    // Compare states
    if (!compare_state(&qk_cpu, &i8_cpu, prev_pc, prev_opcode, prev_a, instr_count)) {
      return 1;  // Mismatch found
    }

    // Progress indicator every 10000 instructions
    if (instr_count % 10000 == 0) {
      printf("Instruction %d: PC=0x%04X - Still matching\n", instr_count, prev_pc);
    }

    // Stop if we hit address 0 (program exit)
    if (qk_cpu.regs.PC.get_pair16() == 0) {
      printf("\nProgram exited (JMP 0) after %d instructions\n", instr_count);
      break;
    }
  }

  if (instr_count >= max_instructions) {
    printf("\nReached maximum instruction count (%ld)\n", max_instructions);
    printf("No divergence detected!\n");
  } else if (qk_cpu.regs.PC.get_pair16() == 0) {
    printf("Both emulators match perfectly!\n");
  }

  return 0;
}
