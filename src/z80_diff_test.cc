/*
 * Differential Testing Harness for qkz80 vs tnylpo Z80
 *
 * This program runs both emulators in lockstep on the same program,
 * comparing registers after each instruction to find the first divergence.
 */

#include "qkz80.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MEMORY_SIZE 0x10000
#define TPA_START 0x0100

// Forward declarations for tnylpo Z80 CPU
extern "C" {
  extern unsigned char *tnylpo_memory;
  extern int tnylpo_reg_sp;
  extern int tnylpo_reg_pc;
  extern unsigned char tnylpo_reg_a;
  extern unsigned char tnylpo_reg_b;
  extern unsigned char tnylpo_reg_c;
  extern unsigned char tnylpo_reg_d;
  extern unsigned char tnylpo_reg_e;
  extern unsigned char tnylpo_reg_h;
  extern unsigned char tnylpo_reg_l;
  extern int tnylpo_flag_s;
  extern int tnylpo_flag_z;
  extern int tnylpo_flag_h;
  extern int tnylpo_flag_p;
  extern int tnylpo_flag_c;
  extern unsigned char tnylpo_reg_ixh;
  extern unsigned char tnylpo_reg_ixl;
  extern unsigned char tnylpo_reg_iyh;
  extern unsigned char tnylpo_reg_iyl;

  int tnylpo_cpu_init(void);
  void tnylpo_cpu_step(void);  // Single-step function we'll create
  int tnylpo_cpu_exit(void);
}

// Helper to get flags from qkz80
struct cpu_flags {
  bool sf, zf, hf, pf, cf;
};

cpu_flags get_qkz80_flags(qkz80* cpu) {
  cpu_flags f;
  qkz80_uint16 af = cpu->get_reg16(qkz80::regp_AF);
  qkz80_uint8 flags = af & 0xFF;

  f.sf = (flags & 0x80) != 0;  // Sign flag (bit 7)
  f.zf = (flags & 0x40) != 0;  // Zero flag (bit 6)
  f.hf = (flags & 0x10) != 0;  // Half-carry flag (bit 4)
  f.pf = (flags & 0x04) != 0;  // Parity/Overflow flag (bit 2)
  f.cf = (flags & 0x01) != 0;  // Carry flag (bit 0)

  return f;
}

// Compare both emulator states
bool compare_state(qkz80* qk, uint16_t prev_pc, uint8_t prev_opcode, int instr_count) {
  bool match = true;

  // Get register values from both emulators
  uint16_t qk_pc = qk->regs.PC.get_pair16();
  uint16_t qk_sp = qk->get_reg16(qkz80::regp_SP);
  uint16_t qk_bc = qk->get_reg16(qkz80::regp_BC);
  uint16_t qk_de = qk->get_reg16(qkz80::regp_DE);
  uint16_t qk_hl = qk->get_reg16(qkz80::regp_HL);
  uint16_t qk_ix = qk->get_reg16(qkz80::regp_IX);
  uint16_t qk_iy = qk->get_reg16(qkz80::regp_IY);
  uint8_t qk_a = qk->get_reg8(qkz80::reg_A);
  cpu_flags qk_f = get_qkz80_flags(qk);

  uint16_t tn_pc = tnylpo_reg_pc;
  uint16_t tn_sp = tnylpo_reg_sp;
  uint16_t tn_bc = (tnylpo_reg_b << 8) | tnylpo_reg_c;
  uint16_t tn_de = (tnylpo_reg_d << 8) | tnylpo_reg_e;
  uint16_t tn_hl = (tnylpo_reg_h << 8) | tnylpo_reg_l;
  uint16_t tn_ix = (tnylpo_reg_ixh << 8) | tnylpo_reg_ixl;
  uint16_t tn_iy = (tnylpo_reg_iyh << 8) | tnylpo_reg_iyl;
  uint8_t tn_a = tnylpo_reg_a;

  // Compare registers
  if (qk_pc != tn_pc) {
    printf("MISMATCH at instruction %d: PC: qkz80=0x%04X vs tnylpo=0x%04X\n",
           instr_count, qk_pc, tn_pc);
    match = false;
  }
  if (qk_sp != tn_sp) {
    printf("MISMATCH at instruction %d: SP: qkz80=0x%04X vs tnylpo=0x%04X\n",
           instr_count, qk_sp, tn_sp);
    match = false;
  }
  if (qk_a != tn_a) {
    printf("MISMATCH at instruction %d: A: qkz80=0x%02X vs tnylpo=0x%02X\n",
           instr_count, qk_a, tn_a);
    match = false;
  }
  if (qk_bc != tn_bc) {
    printf("MISMATCH at instruction %d: BC: qkz80=0x%04X vs tnylpo=0x%04X\n",
           instr_count, qk_bc, tn_bc);
    match = false;
  }
  if (qk_de != tn_de) {
    printf("MISMATCH at instruction %d: DE: qkz80=0x%04X vs tnylpo=0x%04X\n",
           instr_count, qk_de, tn_de);
    match = false;
  }
  if (qk_hl != tn_hl) {
    printf("MISMATCH at instruction %d: HL: qkz80=0x%04X vs tnylpo=0x%04X\n",
           instr_count, qk_hl, tn_hl);
    match = false;
  }
  if (qk_ix != tn_ix) {
    printf("MISMATCH at instruction %d: IX: qkz80=0x%04X vs tnylpo=0x%04X\n",
           instr_count, qk_ix, tn_ix);
    match = false;
  }
  if (qk_iy != tn_iy) {
    printf("MISMATCH at instruction %d: IY: qkz80=0x%04X vs tnylpo=0x%04X\n",
           instr_count, qk_iy, tn_iy);
    match = false;
  }

  // Compare flags
  if (qk_f.sf != (tnylpo_flag_s != 0)) {
    printf("MISMATCH at instruction %d: SF: qkz80=%d vs tnylpo=%d\n",
           instr_count, qk_f.sf, tnylpo_flag_s);
    match = false;
  }
  if (qk_f.zf != (tnylpo_flag_z != 0)) {
    printf("MISMATCH at instruction %d: ZF: qkz80=%d vs tnylpo=%d\n",
           instr_count, qk_f.zf, tnylpo_flag_z);
    match = false;
  }
  if (qk_f.hf != (tnylpo_flag_h != 0)) {
    printf("MISMATCH at instruction %d: HF: qkz80=%d vs tnylpo=%d\n",
           instr_count, qk_f.hf, tnylpo_flag_h);
    match = false;
  }
  if (qk_f.pf != (tnylpo_flag_p != 0)) {
    printf("MISMATCH at instruction %d: PF: qkz80=%d vs tnylpo=%d\n",
           instr_count, qk_f.pf, tnylpo_flag_p);
    match = false;
  }
  if (qk_f.cf != (tnylpo_flag_c != 0)) {
    printf("MISMATCH at instruction %d: CF: qkz80=%d vs tnylpo=%d\n",
           instr_count, qk_f.cf, tnylpo_flag_c);
    match = false;
  }

  if (!match) {
    printf("\nDIVERGENCE DETECTED!\n");
    printf("Previous PC: 0x%04X, Opcode: 0x%02X\n", prev_pc, prev_opcode);

    printf("\nqkz80 state:\n");
    printf("  PC=%04X SP=%04X A=%02X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X\n",
           qk_pc, qk_sp, qk_a, qk_bc, qk_de, qk_hl, qk_ix, qk_iy);
    printf("  Flags: S=%d Z=%d H=%d P=%d C=%d\n",
           qk_f.sf, qk_f.zf, qk_f.hf, qk_f.pf, qk_f.cf);

    printf("\ntnylpo state:\n");
    printf("  PC=%04X SP=%04X A=%02X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X\n",
           tn_pc, tn_sp, tn_a, tn_bc, tn_de, tn_hl, tn_ix, tn_iy);
    printf("  Flags: S=%d Z=%d H=%d P=%d C=%d\n",
           tnylpo_flag_s, tnylpo_flag_z, tnylpo_flag_h, tnylpo_flag_p, tnylpo_flag_c);
    return false;
  }

  return true;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <test.com> [max_instructions]\n", argv[0]);
    fprintf(stderr, "\nRuns both qkz80 and tnylpo Z80 emulators side-by-side\n");
    fprintf(stderr, "and compares their state after each instruction.\n");
    fprintf(stderr, "\nExample: %s tests/zexdoc.com 100000\n", argv[0]);
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
  qk_cpu.set_cpu_mode(qkz80::MODE_Z80);
  qk_cpu.regs.PC.set_pair16(TPA_START);
  qk_cpu.regs.SP.set_pair16(0xFFF0);

  // Copy program to qkz80's memory
  char* qkz80_mem = qk_cpu.get_mem();
  memcpy(&qkz80_mem[TPA_START], program_buffer, loaded);

  // Initialize tnylpo (reference Z80 emulator)
  if (tnylpo_cpu_init() != 0) {
    fprintf(stderr, "Failed to initialize tnylpo\n");
    return 1;
  }

  // Copy program to tnylpo's memory
  memcpy(&tnylpo_memory[TPA_START], program_buffer, loaded);
  tnylpo_reg_pc = TPA_START;
  tnylpo_reg_sp = 0xFFF0;

  printf("Running differential test (Z80 mode)...\n");
  printf("Will stop at first mismatch or after %ld instructions.\n\n", max_instructions);

  // Run both emulators in lockstep
  int instr_count = 0;
  uint16_t prev_pc = TPA_START;
  uint8_t prev_opcode = 0;

  while (instr_count < max_instructions) {
    // Save current state before execution
    prev_pc = qk_cpu.regs.PC.get_pair16();
    prev_opcode = (uint8_t)qkz80_mem[prev_pc];

    // Execute one instruction on each emulator
    qk_cpu.execute();
    tnylpo_cpu_step();

    instr_count++;

    // Compare states
    if (!compare_state(&qk_cpu, prev_pc, prev_opcode, instr_count)) {
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

  tnylpo_cpu_exit();
  return 0;
}
