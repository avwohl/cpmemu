#include "qkz80.h"
#include "qkz80_cpu_flags.h"

static qkz80_trace dummy_trace;
qkz80::qkz80(qkz80_cpu_mem *memory):
  mem(memory),
  trace(&dummy_trace),
  qkz80_debug(false),
  cpu_mode(MODE_Z80),
  cycles(0) { // Default to Z80 mode
  regs.cpu_mode = qkz80_reg_set::MODE_Z80;
}

#define LOW_NIBBLE(xx_foo) ((xx_foo)&0x0f)

void qkz80::cpm_setup_memory(void) {
  qkz80_uint16 start_offset(0x0100);
  regs.PC.set_pair16(start_offset);
  // starting stack
  regs.SP.set_pair16(0xfff0);
  // set ret for each restart
  for(qkz80_uint16 i(1); i<8; i++) {
    qkz80_uint16 addr(i*20);
    qkz80_uint8 return_instruction(0xc9);
    mem->store_mem(addr,return_instruction);
  }
}

qkz80_uint8 qkz80::compute_sum_half_carry(qkz80_uint16 rega,
    qkz80_uint16 dat,
    qkz80_uint16 carry) {
  qkz80_uint16 rega_low(LOW_NIBBLE(rega));
  qkz80_uint16 dat_low(LOW_NIBBLE(dat));
  qkz80_uint16 sum_low(rega_low+dat_low+carry);

  if((sum_low & 0xf0)!=0) {
    return 1;
  }
  return 0;
}

qkz80_uint8 qkz80::compute_subtract_half_carry(qkz80_uint16 rega,
    qkz80_uint16 diff,
    qkz80_uint16 dat,
    qkz80_uint16 carry) {

  if(( ~(rega ^ diff ^ dat ^ carry) & 0x10) != 0)
    return 1;
  return 0;
}

void qkz80::debug_dump_regs(const char* label) {
  // Empty - override in subclass for debug output
  (void)label;
}

void qkz80::halt(void) {
  // Empty - override in subclass to handle halt
}

void qkz80::unimplemented_opcode(qkz80_uint8 opcode, qkz80_uint16 pc) {
  // Empty - override in subclass to handle unimplemented opcodes
  (void)opcode;
  (void)pc;
}

qkz80_uint16 qkz80::read_word(qkz80_uint16 addr) {
  qkz80_uint8 low(mem->fetch_mem(addr));
  qkz80_uint8 high(mem->fetch_mem(addr+1));
  return qkz80_MK_INT16(low,high);
}

qkz80_uint16 qkz80::pop_word(void) {
  qkz80_uint16 sp_val(get_reg16(regp_SP));
  qkz80_uint16 result(read_word(sp_val));
  sp_val+=2;
  set_reg16(sp_val,regp_SP);
  return result;
}

void qkz80::push_word(qkz80_uint16 aword) {
  qkz80_uint16 sp_val(get_reg16(regp_SP));
  sp_val-=2;
  set_reg16(sp_val,regp_SP);
  write_2_bytes(aword,sp_val);
}

const char *qkz80::name_condition_code(qkz80_uint8 cond) {
  switch(cond) {
  case 0: //NZ
    return "nz";
  case 1: //Z
    return "z";
  case 2: //NC
    return "nc";
  case 3: //C
    return "c";
  case 4: // PO (parity)
    return "po";
  case 5: // PE (parity)
    return "pe";
  case 6: // P (positive)
    return "p";
  case 7: // M (minus)
    return "m";
  default:
    return "?";
  }
}

const char *qkz80::name_reg8(qkz80_uint8 reg8) {
  switch(reg8) {
  case reg_B:
    return "b";
  case reg_C:
    return "c";
  case reg_D:
    return "d";
  case reg_E:
    return "e";
  case reg_H:
    return "h";
  case reg_L:
    return "l";
  case reg_M:
    return "m";
  case reg_A:
    return "a";
  }
  return "?";
}

const char *qkz80::name_reg16(qkz80_uint8 rpair) {
  switch(rpair) {
  case regp_BC:
    return "bc";
  case regp_DE:
    return "de";
  case regp_HL:
    return "hl";
  case regp_SP:
    return "sp";
  case regp_AF:
    return "psw";
  }
  return "?";
}

void qkz80::set_reg16(qkz80_uint16 a,qkz80_uint8 rp) {
  trace->add_reg16(rp);
  switch(rp) {
  case regp_BC:
    regs.BC.set_pair16(a);
    break;
  case regp_DE:
    regs.DE.set_pair16(a);
    break;
  case regp_HL:
    regs.HL.set_pair16(a);
    break;
  case regp_AF: {
    qkz80_uint8 low(qkz80_GET_CLEAN8(a));
    qkz80_uint8 high(qkz80_GET_HIGH8(a));
    set_reg8(high,reg_A);
    regs.set_flags(low);
  }
  break;
  case regp_SP:
    regs.SP.set_pair16(a);
    break;
  case regp_PC:
    regs.PC.set_pair16(a);
    break;
  case regp_IX:
    regs.IX.set_pair16(a);
    break;
  case regp_IY:
    regs.IY.set_pair16(a);
    break;
  default:
    qkz80_global_fatal("set_reg16 bad selector rp=%d",int(rp));
  }
}

void qkz80::write_2_bytes(qkz80_uint16 store_me,qkz80_uint16 location) {
  qkz80_uint8 low(qkz80_GET_CLEAN8(store_me));
  qkz80_uint8 high(qkz80_GET_HIGH8(store_me));
  mem->store_mem(location,low);
  mem->store_mem(location+1,high);
}

qkz80_uint16 qkz80::get_reg16(qkz80_uint8 rnum) {
  switch(rnum) {
  case regp_BC:
    return regs.BC.get_pair16();
  case regp_DE:
    return regs.DE.get_pair16();
  case regp_HL:
    return regs.HL.get_pair16();
  case regp_SP:
    return regs.SP.get_pair16();
  case regp_AF:
    // get_flags() now returns already fixed flags in 8080 mode
    return qkz80_MK_INT16(regs.get_flags(),get_reg8(reg_A));
  case regp_PC:
    return regs.PC.get_pair16();
  case regp_IX:
    return regs.IX.get_pair16();
  case regp_IY:
    return regs.IY.get_pair16();
  default:
    qkz80_global_fatal("Illegal 16bit reg selector rnum=%d",int(rnum));
  }
  return 0;
}

qkz80_uint8 qkz80::get_reg8(qkz80_uint8 rnum) {
  switch(rnum) {
  case reg_B:
    return regs.BC.get_high();
  case reg_C:
    return regs.BC.get_low();
  case reg_D:
    return regs.DE.get_high();
  case reg_E:
    return regs.DE.get_low();
  case reg_H:
    return regs.HL.get_high();
  case reg_L:
    return regs.HL.get_low();
  case reg_M:
    return mem->fetch_mem(regs.HL.get_pair16());
  case reg_A:
    return regs.AF.get_high();
  default:
    qkz80_global_fatal("invalid register reg=%d",int(rnum));
  }
  return 0;
}

qkz80_uint8 qkz80::fetch_carry_as_int(void) {
  if((regs.get_flags()&qkz80_cpu_flags::CY)!=0)
    return 1;
  return 0;
}

void qkz80::set_reg8(qkz80_uint8 dat,qkz80_uint8 rnum) {
  trace->add_reg8(rnum);
  switch(rnum) {
  case reg_B:
    regs.BC.set_high(dat);
    break;
  case reg_C:
    regs.BC.set_low(dat);
    break;
  case reg_D:
    regs.DE.set_high(dat);
    break;
  case reg_E:
    regs.DE.set_low(dat);
    break;
  case reg_H:
    regs.HL.set_high(dat);
    break;
  case reg_L:
    regs.HL.set_low(dat);
    break;
  case reg_M:
    mem->store_mem(regs.HL.get_pair16(),dat);
    break;
  case reg_A:
    regs.AF.set_high(dat);
    break;
  default:
    qkz80_global_fatal("invalid register reg=%d",int(rnum));
  }
}

qkz80_uint8 qkz80::peek_byte_from_opcode_stream(void) {
  qkz80_uint16 pc=regs.PC.get_pair16();
  qkz80_uint8 opcode_byte(mem->fetch_mem(pc, true));  // true = instruction fetch
  trace->fetch(opcode_byte,pc);
  return opcode_byte;
}

qkz80_uint8 qkz80::pull_byte_from_opcode_stream(void) {
  qkz80_uint16 pc=regs.PC.get_pair16();
  qkz80_uint8 opcode_byte(mem->fetch_mem(pc, true));  // true = instruction fetch
  trace->fetch(opcode_byte,pc);
  pc++;
  regs.PC.set_pair16(pc);
  return opcode_byte;
}

qkz80_uint16 qkz80::pull_word_from_opcode_stream(void) {
  qkz80_uint8 low(pull_byte_from_opcode_stream());
  qkz80_uint8 high(pull_byte_from_opcode_stream());
  qkz80_uint16 result(qkz80_MK_INT16(low,high));
  return result;
}

void qkz80::execute(void) {
  // Add approximate cycle count (average ~5 cycles per instruction)
  // This is a rough approximation for interrupt timing purposes
  cycles += 5;

  // Handle prefix bytes - Z80 processes these inline
  bool has_dd_prefix = false;  // IX operations
  bool has_fd_prefix = false;  // IY operations
  bool has_ed_prefix = false;  // Extended instructions
  bool has_cb_prefix = false;  // Bit operations
  qkz80_int8 index_offset = 0; // For (IX+d) or (IY+d) addressing

  qkz80_uint8 opcode(pull_byte_from_opcode_stream());

  // Process prefix bytes
  // Note: DD and FD can chain - the last one wins (e.g., FD DD = DD, DD FD = FD)
  // Limit iterations to prevent infinite loops from corrupted/unusual code
  int prefix_count = 0;
  while ((opcode == 0xdd || opcode == 0xfd) && prefix_count < 4) {
    if (cpu_mode == MODE_8080) {
      return;  // DD/FD acts as single-byte NOP in 8080 mode
    }
    prefix_count++;
    // Reset any previous DD/FD prefix - last one wins
    has_dd_prefix = (opcode == 0xdd);
    has_fd_prefix = (opcode == 0xfd);
    opcode = pull_byte_from_opcode_stream();
    if (opcode == 0xcb) {
      has_cb_prefix = true;
      index_offset = (qkz80_int8)pull_byte_from_opcode_stream();
      opcode = pull_byte_from_opcode_stream();
      break;  // CB terminates the prefix chain
    }
  }
  if (opcode == 0xed) {
    // ED prefix is Z80-only, treat as NOP NOP in 8080 mode
    if (cpu_mode == MODE_8080) {
      // In 8080, 0xED is just a 2-byte NOP (ED xx)
      pull_byte_from_opcode_stream();  // consume next byte
      return;
    }
    has_ed_prefix = true;
    opcode = pull_byte_from_opcode_stream();
  } else if (opcode == 0xcb) {
    // CB prefix (bit operations) is Z80-only, treat as NOP NOP in 8080 mode
    if (cpu_mode == MODE_8080) {
      // In 8080, 0xCB is just a 2-byte NOP (CB xx)
      pull_byte_from_opcode_stream();  // consume next byte
      return;
    }
    has_cb_prefix = true;
    opcode = pull_byte_from_opcode_stream();
  }

  // Select which register pair to use for HL operations (HL, IX, or IY)
  qkz80_uint8 active_hl = has_dd_prefix ? regp_IX : (has_fd_prefix ? regp_IY : regp_HL);

  // Handle ED prefix instructions first (they're completely different)
  if (has_ed_prefix) {
    // Most ED opcodes are duplicates or NOPs
    // We implement the documented and important undocumented ones
    switch (opcode) {
    // 16-bit ADD/SUB with carry
    // ADC HL,ss: (opcode & 0xcf) == 0x4a
    case 0x4a: case 0x5a: case 0x6a: case 0x7a: {
      qkz80_uint8 rp = (opcode >> 4) & 0x03;
      qkz80_big_uint hl_val = get_reg16(regp_HL);
      qkz80_big_uint rp_val = get_reg16(rp);
      qkz80_big_uint carry = fetch_carry_as_int();
      qkz80_big_uint result = hl_val + rp_val + carry;
      set_reg16(result, regp_HL);
      // Z80: ADC HL is addition with carry, sets all flags
      regs.set_flags_from_adc16(result, hl_val, rp_val, carry);
      trace->asm_op("adc hl,%s", name_reg16(rp));
      return;
    }

    // SBC HL,ss: (opcode & 0xcf) == 0x42
    case 0x42: case 0x52: case 0x62: case 0x72: {
      qkz80_uint8 rp = (opcode >> 4) & 0x03;
      qkz80_big_uint hl_val = get_reg16(regp_HL);
      qkz80_big_uint rp_val = get_reg16(rp);
      qkz80_big_uint carry = fetch_carry_as_int();
      qkz80_big_uint result = hl_val - rp_val - carry;
      set_reg16(result, regp_HL);
      // Z80: SBC HL is subtraction with borrow, sets all flags
      regs.set_flags_from_sbc16(result, hl_val, rp_val, carry);
      trace->asm_op("sbc hl,%s", name_reg16(rp));
      return;
    }

    // Extended LD instructions
    // LD (nn),BC/DE/SP/HL: (opcode & 0xcf) == 0x43
    case 0x43: case 0x53: case 0x63: case 0x73: {
      qkz80_uint8 rp = (opcode >> 4) & 0x03;
      qkz80_uint16 addr = pull_word_from_opcode_stream();
      qkz80_uint16 val = get_reg16(rp);
      write_2_bytes(val, addr);
      trace->asm_op("ld (0x%04x),%s", addr, name_reg16(rp));
      return;
    }

    // LD BC/DE/SP/HL,(nn): (opcode & 0xcf) == 0x4b
    case 0x4b: case 0x5b: case 0x6b: case 0x7b: {
      qkz80_uint8 rp = (opcode >> 4) & 0x03;
      qkz80_uint16 addr = pull_word_from_opcode_stream();
      qkz80_uint16 val = read_word(addr);
      set_reg16(val, rp);
      trace->asm_op("ld %s,(0x%04x)", name_reg16(rp), addr);
      return;
    }

    // NEG - negate accumulator: (opcode & 0xc7) == 0x44
    // (also duplicates at 4C,54,5C,64,6C,74,7C)
    case 0x44: case 0x4c: case 0x54: case 0x5c:
    case 0x64: case 0x6c: case 0x74: case 0x7c: {
      qkz80_uint8 a_val = get_reg8(reg_A);
      qkz80_big_uint result = 0 - a_val;
      regs.set_flags_from_diff8(result, 0, a_val, 0);
      set_A(result);
      trace->asm_op("neg");
      return;
    }

    // Interrupt mode
    // IM 0
    case 0x46: case 0x4e: case 0x66: case 0x6e:
      regs.IM = 0;
      trace->asm_op("im 0");
      return;

    // IM 1
    case 0x56: case 0x76:
      regs.IM = 1;
      trace->asm_op("im 1");
      return;

    // IM 2
    case 0x5e: case 0x7e:
      regs.IM = 2;
      trace->asm_op("im 2");
      return;

    // LD I,A / LD R,A / LD A,I / LD A,R
    case 0x47: // LD I,A
      regs.I = get_reg8(reg_A);
      trace->asm_op("ld i,a");
      return;

    case 0x4f: // LD R,A
      regs.R = get_reg8(reg_A);
      trace->asm_op("ld r,a");
      return;

    case 0x57: { // LD A,I
      qkz80_uint8 val = regs.I;
      set_A(val);
      regs.set_flags_from_ld_a_ir(val);
      trace->asm_op("ld a,i");
      return;
    }

    case 0x5f: { // LD A,R
      qkz80_uint8 val = regs.R;
      set_A(val);
      regs.set_flags_from_ld_a_ir(val);
      trace->asm_op("ld a,r");
      return;
    }

    // RETI / RETN
    case 0x4d: { // RETI
      qkz80_uint16 addr = pop_word();
      regs.PC.set_pair16(addr);
      trace->asm_op("reti");
      return;
    }

    // RETN: (opcode & 0xc7) == 0x45 (also at 55,5D,65,6D,75,7D)
    // Note: 0x4d is RETI, handled above
    case 0x45: case 0x55: case 0x5d: case 0x65:
    case 0x6d: case 0x75: case 0x7d: {
      qkz80_uint16 addr = pop_word();
      regs.PC.set_pair16(addr);
      regs.IFF1 = regs.IFF2;  // Restore IFF1 from IFF2
      trace->asm_op("retn");
      return;
    }

    // RRD / RLD - decimal rotates
    case 0x67: { // RRD
      qkz80_uint16 hl_addr = get_reg16(regp_HL);
      qkz80_uint8 a_val = get_reg8(reg_A);
      qkz80_uint8 mem_val = mem->fetch_mem(hl_addr);
      qkz80_uint8 new_a = (a_val & 0xf0) | (mem_val & 0x0f);
      qkz80_uint8 new_mem = (mem_val >> 4) | ((a_val & 0x0f) << 4);
      set_A(new_a);
      mem->store_mem(hl_addr, new_mem);
      regs.set_flags_from_logic8(new_a, regs.get_carry_as_int(), 0);
      trace->asm_op("rrd");
      return;
    }

    case 0x6f: { // RLD
      qkz80_uint16 hl_addr = get_reg16(regp_HL);
      qkz80_uint8 a_val = get_reg8(reg_A);
      qkz80_uint8 mem_val = mem->fetch_mem(hl_addr);
      qkz80_uint8 new_a = (a_val & 0xf0) | ((mem_val >> 4) & 0x0f);
      qkz80_uint8 new_mem = (mem_val << 4) | (a_val & 0x0f);
      set_A(new_a);
      mem->store_mem(hl_addr, new_mem);
      regs.set_flags_from_logic8(new_a, regs.get_carry_as_int(), 0);
      trace->asm_op("rld");
      return;
    }

    // Block load/compare/I/O instructions
    case 0xa0: { // LDI
      qkz80_uint16 hl = get_reg16(regp_HL);
      qkz80_uint16 de = get_reg16(regp_DE);
      qkz80_uint16 bc = get_reg16(regp_BC);
      qkz80_uint8 byte_val = mem->fetch_mem(hl);
      mem->store_mem(de, byte_val);
      set_reg16(hl + 1, regp_HL);
      set_reg16(de + 1, regp_DE);
      set_reg16(bc - 1, regp_BC);
      regs.set_flags_from_block_ld(get_reg8(reg_A), byte_val, bc - 1);
      trace->asm_op("ldi");
      return;
    }

    case 0xb0: { // LDIR
      qkz80_uint16 hl = get_reg16(regp_HL);
      qkz80_uint16 de = get_reg16(regp_DE);
      qkz80_uint16 bc = get_reg16(regp_BC);
      qkz80_uint8 byte_val = mem->fetch_mem(hl);
      mem->store_mem(de, byte_val);
      set_reg16(hl + 1, regp_HL);
      set_reg16(de + 1, regp_DE);
      set_reg16(bc - 1, regp_BC);
      regs.set_flags_from_block_ld(get_reg8(reg_A), byte_val, bc - 1);
      if (bc != 1) regs.PC.set_pair16(regs.PC.get_pair16() - 2);  // Repeat
      trace->asm_op("ldir");
      return;
    }

    case 0xa8: { // LDD
      qkz80_uint16 hl = get_reg16(regp_HL);
      qkz80_uint16 de = get_reg16(regp_DE);
      qkz80_uint16 bc = get_reg16(regp_BC);
      qkz80_uint8 byte_val = mem->fetch_mem(hl);
      mem->store_mem(de, byte_val);
      set_reg16(hl - 1, regp_HL);
      set_reg16(de - 1, regp_DE);
      set_reg16(bc - 1, regp_BC);
      regs.set_flags_from_block_ld(get_reg8(reg_A), byte_val, bc - 1);
      trace->asm_op("ldd");
      return;
    }

    case 0xb8: { // LDDR
      qkz80_uint16 hl = get_reg16(regp_HL);
      qkz80_uint16 de = get_reg16(regp_DE);
      qkz80_uint16 bc = get_reg16(regp_BC);
      qkz80_uint8 byte_val = mem->fetch_mem(hl);
      mem->store_mem(de, byte_val);
      set_reg16(hl - 1, regp_HL);
      set_reg16(de - 1, regp_DE);
      set_reg16(bc - 1, regp_BC);
      regs.set_flags_from_block_ld(get_reg8(reg_A), byte_val, bc - 1);
      if (bc != 1) regs.PC.set_pair16(regs.PC.get_pair16() - 2);  // Repeat
      trace->asm_op("lddr");
      return;
    }

    case 0xa1: { // CPI
      qkz80_uint16 hl = get_reg16(regp_HL);
      qkz80_uint16 bc = get_reg16(regp_BC);
      qkz80_uint8 a_val = get_reg8(reg_A);
      qkz80_uint8 mem_val = mem->fetch_mem(hl);
      regs.set_flags_from_block_cp(a_val, mem_val, bc - 1);
      set_reg16(hl + 1, regp_HL);
      set_reg16(bc - 1, regp_BC);
      trace->asm_op("cpi");
      return;
    }

    case 0xb1: { // CPIR
      qkz80_uint16 hl = get_reg16(regp_HL);
      qkz80_uint16 bc = get_reg16(regp_BC);
      qkz80_uint8 a_val = get_reg8(reg_A);
      qkz80_uint8 mem_val = mem->fetch_mem(hl);
      qkz80_big_uint diff = a_val - mem_val;
      regs.set_flags_from_block_cp(a_val, mem_val, bc - 1);
      set_reg16(hl + 1, regp_HL);
      set_reg16(bc - 1, regp_BC);
      if (bc != 1 && diff != 0) regs.PC.set_pair16(regs.PC.get_pair16() - 2);  // Repeat if not found
      trace->asm_op("cpir");
      return;
    }

    case 0xa9: { // CPD
      qkz80_uint16 hl = get_reg16(regp_HL);
      qkz80_uint16 bc = get_reg16(regp_BC);
      qkz80_uint8 a_val = get_reg8(reg_A);
      qkz80_uint8 mem_val = mem->fetch_mem(hl);
      regs.set_flags_from_block_cp(a_val, mem_val, bc - 1);
      set_reg16(hl - 1, regp_HL);
      set_reg16(bc - 1, regp_BC);
      trace->asm_op("cpd");
      return;
    }

    case 0xb9: { // CPDR
      qkz80_uint16 hl = get_reg16(regp_HL);
      qkz80_uint16 bc = get_reg16(regp_BC);
      qkz80_uint8 a_val = get_reg8(reg_A);
      qkz80_uint8 mem_val = mem->fetch_mem(hl);
      qkz80_big_uint diff = a_val - mem_val;
      regs.set_flags_from_block_cp(a_val, mem_val, bc - 1);
      set_reg16(hl - 1, regp_HL);
      set_reg16(bc - 1, regp_BC);
      if (bc != 1 && diff != 0) regs.PC.set_pair16(regs.PC.get_pair16() - 2);  // Repeat if not found
      trace->asm_op("cpdr");
      return;
    }

    // Block I/O - simplified (real implementation would need I/O port system)
    case 0xa2: case 0xb2: case 0xaa: case 0xba:
    case 0xa3: case 0xb3: case 0xab: case 0xbb:
      block_io(opcode);
      return;

    // Many ED opcodes are just NOPs or duplicates
    default:
      trace->asm_op("ED %02x (nop or duplicate)", opcode);
      return;
    }
  }

  // Handle CB prefix instructions (bit operations)
  if (has_cb_prefix) {
    qkz80_uint8 reg_sel = opcode & 0x07;  // Which register (B,C,D,E,H,L,(HL),A)
    qkz80_uint16 addr = 0;
    qkz80_uint8 val = 0;

    // For DDCB/FDCB, address is already calculated
    if (has_dd_prefix || has_fd_prefix) {
      addr = get_reg16(active_hl) + index_offset;
      val = mem->fetch_mem(addr);
    } else if (reg_sel == reg_M) {
      addr = get_reg16(regp_HL);
      val = mem->fetch_mem(addr);
    } else {
      val = get_reg8(reg_sel);
    }

    qkz80_uint8 bit_num = (opcode >> 3) & 0x07;
    qkz80_uint8 result = val;

    // Decode CB instruction groups
    if (opcode < 0x40) {
      // Rotates and shifts (00-3F)
      qkz80_uint8 op = (opcode >> 3) & 0x07;
      switch(op) {
      case 0:
        result = do_rlc(val);
        trace->asm_op("rlc %s", name_reg8(reg_sel));
        break;
      case 1:
        result = do_rrc(val);
        trace->asm_op("rrc %s", name_reg8(reg_sel));
        break;
      case 2:
        result = do_rl(val);
        trace->asm_op("rl %s", name_reg8(reg_sel));
        break;
      case 3:
        result = do_rr(val);
        trace->asm_op("rr %s", name_reg8(reg_sel));
        break;
      case 4:
        result = do_sla(val);
        trace->asm_op("sla %s", name_reg8(reg_sel));
        break;
      case 5:
        result = do_sra(val);
        trace->asm_op("sra %s", name_reg8(reg_sel));
        break;
      case 6:
        result = do_sll(val);
        trace->asm_op("sll %s", name_reg8(reg_sel));
        break; // undocumented
      case 7:
        result = do_srl(val);
        trace->asm_op("srl %s", name_reg8(reg_sel));
        break;
      }
      // Write result back
      if (has_dd_prefix || has_fd_prefix) {
        mem->store_mem(addr, result);
        // DDCB/FDCB undocumented: also store in register
        if (reg_sel != reg_M) set_reg8(result, reg_sel);
      } else if (reg_sel == reg_M) {
        mem->store_mem(addr, result);
      } else {
        set_reg8(result, reg_sel);
      }
    } else if (opcode < 0x80) {
      // BIT b,r (40-7F) - test bit
      qkz80_uint8 bit_mask = 1 << bit_num;
      qkz80_uint8 bit_val = (val & bit_mask) ? 0 : 1;  // Z flag set if bit is 0
      qkz80_uint8 flags = regs.get_flags();
      flags = (flags & qkz80_cpu_flags::CY) | qkz80_cpu_flags::H;  // Keep carry, set H, clear N
      if (bit_val) flags |= qkz80_cpu_flags::Z | qkz80_cpu_flags::P;  // Set Z and P/V if bit is 0
      if ((val & 0x80) && bit_num == 7) flags |= qkz80_cpu_flags::S;  // Set S if bit 7 is set

      // X and Y flags (Z80 only) - undocumented behavior
      // Source depends on addressing mode:
      if (regs.cpu_mode == qkz80_reg_set::MODE_Z80) {
        qkz80_uint8 xy_source;
        if (has_dd_prefix || has_fd_prefix) {
          // BIT n,(IX+d) or BIT n,(IY+d): Use high byte of effective address
          xy_source = (addr >> 8) & 0xFF;
        } else if (reg_sel == reg_M) {
          // BIT n,(HL): Use H register (high byte of HL)
          xy_source = get_reg8(reg_H);
        } else {
          // BIT n,r: Use the register value
          xy_source = val;
        }
        if (xy_source & 0x08) flags |= qkz80_cpu_flags::X;
        if (xy_source & 0x20) flags |= qkz80_cpu_flags::Y;
      }

      regs.set_flags(flags);
      trace->asm_op("bit %d,%s", bit_num, name_reg8(reg_sel));
    } else if (opcode < 0xC0) {
      // RES b,r (80-BF) - reset bit
      qkz80_uint8 bit_mask = ~(1 << bit_num);
      result = val & bit_mask;
      if (has_dd_prefix || has_fd_prefix) {
        mem->store_mem(addr, result);
        if (reg_sel != reg_M) set_reg8(result, reg_sel);  // undocumented
      } else if (reg_sel == reg_M) {
        mem->store_mem(addr, result);
      } else {
        set_reg8(result, reg_sel);
      }
      trace->asm_op("res %d,%s", bit_num, name_reg8(reg_sel));
    } else {
      // SET b,r (C0-FF) - set bit
      qkz80_uint8 bit_mask = 1 << bit_num;
      result = val | bit_mask;
      if (has_dd_prefix || has_fd_prefix) {
        mem->store_mem(addr, result);
        if (reg_sel != reg_M) set_reg8(result, reg_sel);  // undocumented
      } else if (reg_sel == reg_M) {
        mem->store_mem(addr, result);
      } else {
        set_reg8(result, reg_sel);
      }
      trace->asm_op("set %d,%s", bit_num, name_reg8(reg_sel));
    }
    return;
  }

  // Special handling for ALU operations with IXH/IXL/IYH/IYL (undocumented Z80 instructions)
  // and indexed addressing (IX+d)/(IY+d)
  // Check if this is an ALU operation (opcodes 0x80-0xBF) with DD/FD prefix
  // This must be checked before the main switch to handle the special cases
  if ((has_dd_prefix || has_fd_prefix) && (opcode >= 0x80 && opcode <= 0xBF)) {
    qkz80_uint8 reg_num = opcode & 0x7;
    if (reg_num == reg_H || reg_num == reg_L || reg_num == reg_M) {
      // Get the operand value
      qkz80_uint8 regb;
      if (reg_num == reg_M) {
        // Indexed addressing: (IX+d) or (IY+d)
        qkz80_int8 displacement = static_cast<qkz80_int8>(pull_byte_from_opcode_stream());
        qkz80_uint16 base_addr = has_dd_prefix ? regs.IX.get_pair16() : regs.IY.get_pair16();
        qkz80_uint16 addr = base_addr + displacement;
        regb = mem->fetch_mem(addr);
      } else {
        // IXH/IXL/IYH/IYL (undocumented half-register operations)
        regb = (reg_num == reg_H) ?
               (has_dd_prefix ? regs.IX.get_high() : regs.IY.get_high()) :
               (has_dd_prefix ? regs.IX.get_low() : regs.IY.get_low());
      }

      qkz80_uint16 rega = get_reg8(reg_A);

      // Determine which ALU operation
      qkz80_uint8 alu_op = (opcode >> 3) & 0x7;
      switch(alu_op) {
      case 0: { // ADD
        qkz80_big_uint sum(rega + regb);
        regs.set_flags_from_sum8(sum, rega, regb, 0);
        set_A(sum);
        if (reg_num == reg_M) {
          trace->asm_op("add (%s+d)", has_dd_prefix ? "ix" : "iy");
        } else {
          trace->asm_op("add %s", reg_num == reg_H ? (has_dd_prefix ? "ixh" : "iyh") : (has_dd_prefix ? "ixl" : "iyl"));
        }
        break;
      }
      case 1: { // ADC
        qkz80_uint16 carry = fetch_carry_as_int();
        qkz80_big_uint sum(rega + regb + carry);
        regs.set_flags_from_sum8(sum, rega, regb, carry);
        set_A(sum);
        if (reg_num == reg_M) {
          trace->asm_op("adc (%s+d)", has_dd_prefix ? "ix" : "iy");
        } else {
          trace->asm_op("adc %s", reg_num == reg_H ? (has_dd_prefix ? "ixh" : "iyh") : (has_dd_prefix ? "ixl" : "iyl"));
        }
        break;
      }
      case 2: { // SUB
        qkz80_big_uint diff(rega - regb);
        regs.set_flags_from_diff8(diff, rega, regb, 0);
        set_A(diff);
        if (reg_num == reg_M) {
          trace->asm_op("sub (%s+d)", has_dd_prefix ? "ix" : "iy");
        } else {
          trace->asm_op("sub %s", reg_num == reg_H ? (has_dd_prefix ? "ixh" : "iyh") : (has_dd_prefix ? "ixl" : "iyl"));
        }
        break;
      }
      case 3: { // SBC
        qkz80_uint16 carry = fetch_carry_as_int();
        qkz80_big_uint diff(rega - regb - carry);
        regs.set_flags_from_diff8(diff, rega, regb, carry);
        set_A(diff);
        if (reg_num == reg_M) {
          trace->asm_op("sbc (%s+d)", has_dd_prefix ? "ix" : "iy");
        } else {
          trace->asm_op("sbc %s", reg_num == reg_H ? (has_dd_prefix ? "ixh" : "iyh") : (has_dd_prefix ? "ixl" : "iyl"));
        }
        break;
      }
      case 4: { // AND
        qkz80_uint8 result = rega & regb;
        // Z80: H always 1, 8080: H = bit 3 of (op1 | op2)
        qkz80_uint8 hc = (cpu_mode == MODE_Z80) ? 1 : (((rega | regb) & 0x08) != 0);
        regs.set_flags_from_logic8(result, 0, hc);
        set_A(result);
        if (reg_num == reg_M) {
          trace->asm_op("and (%s+d)", has_dd_prefix ? "ix" : "iy");
        } else {
          trace->asm_op("and %s", reg_num == reg_H ? (has_dd_prefix ? "ixh" : "iyh") : (has_dd_prefix ? "ixl" : "iyl"));
        }
        break;
      }
      case 5: { // XOR
        qkz80_uint8 result = rega ^ regb;
        regs.set_flags_from_logic8(result, 0, 0);
        set_A(result);
        if (reg_num == reg_M) {
          trace->asm_op("xor (%s+d)", has_dd_prefix ? "ix" : "iy");
        } else {
          trace->asm_op("xor %s", reg_num == reg_H ? (has_dd_prefix ? "ixh" : "iyh") : (has_dd_prefix ? "ixl" : "iyl"));
        }
        break;
      }
      case 6: { // OR
        qkz80_uint8 result = rega | regb;
        regs.set_flags_from_logic8(result, 0, 0);
        set_A(result);
        if (reg_num == reg_M) {
          trace->asm_op("or (%s+d)", has_dd_prefix ? "ix" : "iy");
        } else {
          trace->asm_op("or %s", reg_num == reg_H ? (has_dd_prefix ? "ixh" : "iyh") : (has_dd_prefix ? "ixl" : "iyl"));
        }
        break;
      }
      case 7: { // CP
        qkz80_big_uint diff(rega - regb);
        regs.set_flags_from_diff8(diff, rega, regb, 0);
        // CP is special: X and Y flags come from the operand, not the result
        qkz80_uint8 flags = regs.get_flags();
        flags &= ~(qkz80_cpu_flags::X | qkz80_cpu_flags::Y);  // Clear X and Y
        if (regb & 0x08) flags |= qkz80_cpu_flags::X;          // Set X from bit 3 of operand
        if (regb & 0x20) flags |= qkz80_cpu_flags::Y;          // Set Y from bit 5 of operand
        regs.set_flags(flags);
        if (reg_num == reg_M) {
          trace->asm_op("cp (%s+d)", has_dd_prefix ? "ix" : "iy");
        } else {
          trace->asm_op("cp %s", reg_num == reg_H ? (has_dd_prefix ? "ixh" : "iyh") : (has_dd_prefix ? "ixl" : "iyl"));
        }
        break;
      }
      }
      return;
    }
  }

  // Main opcode dispatch switch
  switch (opcode) {

  case 0x00: // NOP
    trace->asm_op("nop");
    return;

  // LXI - Load register pair immediate
  // (opcode & 0xcf) == 0x01: 0x01, 0x11, 0x21, 0x31
  case 0x01: case 0x11: case 0x21: case 0x31: {
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint8 rpair = ((opcode >> 4) & 0x03);
    // If DD/FD prefix and register pair is HL (2), use IX/IY instead
    if ((has_dd_prefix || has_fd_prefix) && rpair == regp_HL) {
      rpair = active_hl;
    }
    set_reg16(addr,rpair);
    trace->asm_op("lxi %s,0x%0x",name_reg16(rpair),addr);
    trace->add_reg16(rpair);
    return;
  }

  // STAX - Store A indirect (BC or DE only)
  // (opcode & 0xcf) == 0x02: 0x02, 0x12 (0x22=SHLD, 0x32=STA handled separately)
  case 0x02: case 0x12: {
    qkz80_uint8 rp((opcode >> 4) & 0x03);
    qkz80_uint16 pair(get_reg16(rp));
    qkz80_uint8 rega(get_reg8(reg_A));
    trace->add_reg16(rp);
    mem->store_mem(pair,rega);
    trace->asm_op("stax %s",name_reg16(rp));
    return;
  }

  // INX - Increment register pair
  // (opcode & 0xcf) == 0x03: 0x03, 0x13, 0x23, 0x33
  case 0x03: case 0x13: case 0x23: case 0x33: {
    qkz80_uint8 rp((opcode >> 4) & 0x03);
    // If DD/FD prefix and register pair is HL (2), use IX/IY instead
    if ((has_dd_prefix || has_fd_prefix) && rp == regp_HL) {
      rp = active_hl;
    }
    qkz80_uint16 pair_val(get_reg16(rp));
    pair_val++;
    set_reg16(pair_val,rp);
    trace->asm_op("inx %s",name_reg16(rp));
    return;
  }

  // INR - Increment register
  // (opcode & 0xc7) == 0x04: 0x04, 0x0c, 0x14, 0x1c, 0x24, 0x2c, 0x34, 0x3c
  case 0x04: case 0x0c: case 0x14: case 0x1c:
  case 0x24: case 0x2c: case 0x34: case 0x3c: {
    qkz80_uint8 reg_num((opcode>>3) & 0x7);

    // Special handling for indexed memory (IX+d) or (IY+d)
    if ((has_dd_prefix || has_fd_prefix) && reg_num == reg_M) {
      qkz80_int8 offset = (qkz80_int8)pull_byte_from_opcode_stream();
      qkz80_uint16 addr = get_reg16(active_hl) + offset;
      qkz80_uint8 num = mem->fetch_mem(addr);
      num++;
      mem->store_mem(addr, num);
      qkz80_uint8 hc((num & 0xf) == 0);
      regs.set_zspa_from_inr(num,hc);
      if (has_dd_prefix)
        trace->asm_op("inc (ix%+d)", offset);
      else
        trace->asm_op("inc (iy%+d)", offset);
      return;
    }

    // Special handling for IXH, IXL, IYH, IYL (undocumented Z80 instructions)
    if ((has_dd_prefix || has_fd_prefix) && (reg_num == reg_H || reg_num == reg_L)) {
      qkz80_uint8 num;
      if (reg_num == reg_H) {
        num = has_dd_prefix ? regs.IX.get_high() : regs.IY.get_high();
        num++;
        if (has_dd_prefix) {
          regs.IX.set_high(num);
          trace->asm_op("inc ixh");
        } else {
          regs.IY.set_high(num);
          trace->asm_op("inc iyh");
        }
      } else { // reg_L
        num = has_dd_prefix ? regs.IX.get_low() : regs.IY.get_low();
        num++;
        if (has_dd_prefix) {
          regs.IX.set_low(num);
          trace->asm_op("inc ixl");
        } else {
          regs.IY.set_low(num);
          trace->asm_op("inc iyl");
        }
      }
      qkz80_uint8 hc((num & 0xf) == 0);
      regs.set_zspa_from_inr(num,hc);
      return;
    }

    qkz80_uint8 num(get_reg8(reg_num));
    num++;
    set_reg8(num,reg_num);
    qkz80_uint8 hc((num & 0xf) == 0);
    regs.set_zspa_from_inr(num,hc);
    trace->asm_op("inr %s",name_reg8(reg_num));
    return;
  }

  // DCR - Decrement register
  // (opcode & 0xc7) == 0x05: 0x05, 0x0d, 0x15, 0x1d, 0x25, 0x2d, 0x35, 0x3d
  case 0x05: case 0x0d: case 0x15: case 0x1d:
  case 0x25: case 0x2d: case 0x35: case 0x3d: {
    qkz80_uint8 reg_num((opcode>>3) & 0x7);

    // Special handling for indexed memory (IX+d) or (IY+d)
    if ((has_dd_prefix || has_fd_prefix) && reg_num == reg_M) {
      qkz80_int8 offset = (qkz80_int8)pull_byte_from_opcode_stream();
      qkz80_uint16 addr = get_reg16(active_hl) + offset;
      qkz80_uint8 num = mem->fetch_mem(addr);
      num--;
      mem->store_mem(addr, num);
      // Half-carry calculation differs between Z80 and 8080 for DCR
      qkz80_uint8 hc;
      if (cpu_mode == MODE_8080) {
        // 8080: HF=1 unless lower nibble is 0xF
        hc = ((num & 0xf) != 0xf);
      } else {
        // Z80: HF=1 when lower nibble is 0xF (borrow from bit 4)
        hc = ((num & 0xf) == 0xf);
      }
      regs.set_zspa_from_inr(num,hc,false);  // false = decrement
      if (has_dd_prefix)
        trace->asm_op("dec (ix%+d)", offset);
      else
        trace->asm_op("dec (iy%+d)", offset);
      return;
    }

    // Special handling for IXH, IXL, IYH, IYL (undocumented Z80 instructions)
    if ((has_dd_prefix || has_fd_prefix) && (reg_num == reg_H || reg_num == reg_L)) {
      qkz80_uint8 num;
      if (reg_num == reg_H) {
        num = has_dd_prefix ? regs.IX.get_high() : regs.IY.get_high();
        num--;
        if (has_dd_prefix) {
          regs.IX.set_high(num);
          trace->asm_op("dec ixh");
        } else {
          regs.IY.set_high(num);
          trace->asm_op("dec iyh");
        }
      } else { // reg_L
        num = has_dd_prefix ? regs.IX.get_low() : regs.IY.get_low();
        num--;
        if (has_dd_prefix) {
          regs.IX.set_low(num);
          trace->asm_op("dec ixl");
        } else {
          regs.IY.set_low(num);
          trace->asm_op("dec iyl");
        }
      }
      qkz80_uint8 hc((num & 0xf) == 0xf);
      regs.set_zspa_from_inr(num,hc,false);  // false = decrement
      return;
    }

    qkz80_uint8 num(get_reg8(reg_num));
    num--;
    set_reg8(num,reg_num);
    // Half-carry calculation differs between Z80 and 8080 for DCR
    qkz80_uint8 hc;
    if (cpu_mode == MODE_8080) {
      // 8080: HF=1 unless lower nibble is 0xF
      hc = ((num & 0xf) != 0xf);
    } else {
      // Z80: HF=1 when lower nibble is 0xF (borrow from bit 4)
      hc = ((num & 0xf) == 0xf);
    }
    regs.set_zspa_from_inr(num,hc,false);  // false = decrement
    trace->asm_op("dcr %s",name_reg8(reg_num));
    return;
  }

  // MVI - Move immediate to register
  // (opcode & 0xc7) == 0x06: 0x06, 0x0e, 0x16, 0x1e, 0x26, 0x2e, 0x36, 0x3e
  case 0x06: case 0x0e: case 0x16: case 0x1e:
  case 0x26: case 0x2e: case 0x36: case 0x3e: {
    qkz80_uint8 dst((opcode >> 3) & 0x07);

    // Special handling for LD (IX+d),n and LD (IY+d),n
    if ((has_dd_prefix || has_fd_prefix) && dst == reg_M) {
      qkz80_int8 offset = (qkz80_int8)pull_byte_from_opcode_stream();
      qkz80_uint8 dat = pull_byte_from_opcode_stream();
      qkz80_uint16 addr = get_reg16(active_hl) + offset;
      mem->store_mem(addr, dat);
      if (has_dd_prefix)
        trace->asm_op("ld (ix%+d),0x%02x", offset, dat);
      else
        trace->asm_op("ld (iy%+d),0x%02x", offset, dat);
      return;
    }

    // Special handling for IXH, IXL, IYH, IYL (undocumented Z80 instructions)
    if ((has_dd_prefix || has_fd_prefix) && (dst == reg_H || dst == reg_L)) {
      qkz80_uint8 dat = pull_byte_from_opcode_stream();
      if (dst == reg_H) {
        if (has_dd_prefix) {
          regs.IX.set_high(dat);
          trace->asm_op("ld ixh,0x%02x", dat);
        } else {
          regs.IY.set_high(dat);
          trace->asm_op("ld iyh,0x%02x", dat);
        }
      } else { // reg_L
        if (has_dd_prefix) {
          regs.IX.set_low(dat);
          trace->asm_op("ld ixl,0x%02x", dat);
        } else {
          regs.IY.set_low(dat);
          trace->asm_op("ld iyl,0x%02x", dat);
        }
      }
      return;
    }

    qkz80_uint8 dat(pull_byte_from_opcode_stream());
    set_reg8(dat,dst);
    trace->asm_op("mvi %s,0x%0x",name_reg8(dst),dat);
    trace->add_reg8(dst);
    return;
  }

  case 0x07: // RLCA
  {
    qkz80_big_uint dat1(get_reg8(reg_A));
    qkz80_big_uint cy(0);
    if((dat1 & 0x080)!=0) {
      cy=1;
    }
    dat1=(dat1<<1) | cy;
    set_reg8(dat1,reg_A);
    regs.set_flags_from_rotate_acc(dat1, cy);
    trace->asm_op("rlca");
    return;
  }

  case 0x08: // EX AF,AF' (Z80 only)
    if (cpu_mode == MODE_8080)
      return;
    {
      qkz80_uint16 af = regs.AF.get_pair16();
      qkz80_uint16 af_prime = regs.AF_.get_pair16();
      regs.AF.set_pair16(af_prime);
      regs.AF_.set_pair16(af);
      trace->asm_op("ex af,af'");
    }
    return;

  // DAD - Double add (ADD HL/IX/IY,rp)
  // (opcode & 0xcf) == 0x09: 0x09, 0x19, 0x29, 0x39
  case 0x09: case 0x19: case 0x29: case 0x39: {
    qkz80_uint8 rp((opcode >> 4) & 0x03);
    // If DD/FD prefix and register pair is HL (2), use IX/IY instead
    if ((has_dd_prefix || has_fd_prefix) && rp == regp_HL) {
      rp = active_hl;
    }
    qkz80_big_uint pair1(get_reg16(rp));
    qkz80_big_uint pair2(get_reg16(active_hl));
    qkz80_big_uint sum(pair1+pair2);
    set_reg16(sum,active_hl);

    // Use Z80-specific flag handling if in Z80 mode
    if (cpu_mode == MODE_Z80) {
      regs.set_flags_from_add16(sum, pair2, pair1);
    } else {
      // 8080: Only sets carry flag
      regs.set_carry_from_int((sum& ~0x0ffff)!=0);
    }

    if (has_dd_prefix) trace->asm_op("add ix,%s",name_reg16(rp));
    else if (has_fd_prefix) trace->asm_op("add iy,%s",name_reg16(rp));
    else trace->asm_op("dad %s",name_reg16(rp));
    trace->add_reg16(rp);
    return;
  }

  // LDAX - Load A indirect (BC or DE only)
  // (opcode & 0xcf) == 0x0a: 0x0a, 0x1a (0x2a=LHLD, 0x3a=LDA handled separately)
  case 0x0a: case 0x1a: {
    qkz80_uint8 rp((opcode >> 4) & 0x03);
    qkz80_uint16 pair(get_reg16(rp));
    qkz80_uint8 dat(mem->fetch_mem(pair));
    trace->add_reg16(rp);
    set_reg8(dat,reg_A);
    trace->asm_op("ldax %s",name_reg16(rp));
    return;
  }

  // DCX - Decrement register pair
  // (opcode & 0xcf) == 0x0b: 0x0b, 0x1b, 0x2b, 0x3b
  case 0x0b: case 0x1b: case 0x2b: case 0x3b: {
    qkz80_uint8 rp((opcode >> 4) & 0x03);
    // If DD/FD prefix and register pair is HL (2), use IX/IY instead
    if ((has_dd_prefix || has_fd_prefix) && rp == regp_HL) {
      rp = active_hl;
    }
    qkz80_uint16 pair_val(get_reg16(rp));
    pair_val--;
    set_reg16(pair_val,rp);
    trace->asm_op("dcx %s",name_reg16(rp));
    return;
  }

  case 0x0f: // RRCA
  {
    qkz80_big_uint dat1(get_reg8(reg_A));
    qkz80_uint8 high_bit(0);
    qkz80_uint8 low_bit(dat1 & 0x1);
    if(low_bit!=0) {
      high_bit=0x80;
    }
    dat1=(dat1>>1) | high_bit;
    set_reg8(dat1,reg_A);
    regs.set_flags_from_rotate_acc(dat1, low_bit);
    trace->asm_op("rrca");
    return;
  }

  case 0x10: // DJNZ - Decrement B and Jump if Not Zero (Z80 only)
    if (cpu_mode == MODE_8080)
      return;
    {
      qkz80_int8 offset = (qkz80_int8)pull_byte_from_opcode_stream();
      qkz80_uint8 b_val = get_reg8(reg_B);
      b_val--;
      set_reg8(b_val, reg_B);
      if (b_val != 0) {
        qkz80_uint16 pc = regs.PC.get_pair16();
        regs.PC.set_pair16(pc + offset);
        trace->asm_op("djnz $%+d", offset);
        trace->comment("taken, B=%02x", b_val);
      } else {
        trace->asm_op("djnz $%+d", offset);
        trace->comment("not taken, B=0");
      }
    }
    return;

  case 0x17: // RLA
  {
    qkz80_big_uint a_val(get_reg8(reg_A));
    qkz80_uint8 new_carry(0);
    if((a_val&0x80)!=0)
      new_carry=1;
    qkz80_uint8 old_carry(regs.get_carry_as_int());
    a_val=(a_val<<1) | old_carry;
    set_reg8(a_val,reg_A);
    regs.set_flags_from_rotate_acc(a_val, new_carry);
    trace->asm_op("rla");
    return;
  }

  case 0x18: // JR - Unconditional relative jump (Z80 only)
    if (cpu_mode == MODE_8080)
      return;
    {
      qkz80_int8 offset = (qkz80_int8)pull_byte_from_opcode_stream();
      qkz80_uint16 pc = regs.PC.get_pair16();
      regs.PC.set_pair16(pc + offset);
      trace->asm_op("jr $%+d", offset);
    }
    return;

  case 0x1f: // RRA
  {
    qkz80_big_uint a_val(get_reg8(reg_A));
    qkz80_uint8 new_carry(a_val&1);
    qkz80_uint8 old_carry(regs.get_carry_as_int());
    a_val=a_val>>1;
    if(old_carry)
      a_val|=0x80;
    else
      a_val&=0x7f;
    set_reg8(a_val,reg_A);
    regs.set_flags_from_rotate_acc(a_val, new_carry);
    trace->asm_op("rra");
    return;
  }

  case 0x20: // JR NZ (Z80 only)
    if (cpu_mode == MODE_8080)
      return;
    {
      qkz80_int8 offset = (qkz80_int8)pull_byte_from_opcode_stream();
      if (!regs.condition_code(1, regs.get_flags())) { // NZ condition
        qkz80_uint16 pc = regs.PC.get_pair16();
        regs.PC.set_pair16(pc + offset);
        trace->asm_op("jr nz,$%+d", offset);
        trace->comment("taken");
      } else {
        trace->asm_op("jr nz,$%+d", offset);
        trace->comment("not taken");
      }
    }
    return;

  case 0x22: // SHLD (LD (nn),HL/IX/IY)
  {
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint16 aword(get_reg16(active_hl));
    write_2_bytes(aword,addr);
    if (has_dd_prefix) trace->asm_op("ld (0x%0x),ix",addr);
    else if (has_fd_prefix) trace->asm_op("ld (0x%0x),iy",addr);
    else trace->asm_op("shld 0x%0x",addr);
    trace->add_reg16(active_hl);
    return;
  }

  case 0x27: // DAA - Based on tnylpo implementation
  {
    qkz80_uint8 rega = get_reg8(reg_A);
    qkz80_uint8 flags = regs.get_flags();
    qkz80_uint8 low = rega & 0x0f;
    qkz80_uint8 high = (rega >> 4) & 0x0f;
    qkz80_uint8 flag_c = fetch_carry_as_int();
    qkz80_uint8 flag_h = (flags & qkz80_cpu_flags::AC) != 0;
    qkz80_uint8 flag_n = (flags & qkz80_cpu_flags::N) != 0;
    qkz80_uint8 diff;
    qkz80_uint8 new_c, new_h;

    // Calculate adjustment byte for A (tnylpo logic)
    if (flag_c) {
      if (low < 0xa) {
        diff = flag_h ? 0x66 : 0x60;
      } else {
        diff = 0x66;
      }
    } else {
      if (low < 0xa) {
        if (high < 0xa) {
          diff = flag_h ? 0x06 : 0x00;
        } else {
          diff = flag_h ? 0x66 : 0x60;
        }
      } else {
        diff = (high < 0x9) ? 0x06 : 0x66;
      }
    }

    // Calculate new C flag (tnylpo logic)
    if (flag_c) {
      new_c = 1;
    } else {
      if (low < 0xa) {
        new_c = (high < 0xa) ? 0 : 1;
      } else {
        new_c = (high < 0x9) ? 0 : 1;
      }
    }

    // Calculate new H flag (tnylpo logic)
    if (flag_n) {
      if (flag_h) {
        new_h = (low < 0x6) ? 1 : 0;
      } else {
        new_h = 0;
      }
    } else {
      new_h = (low < 0xa) ? 0 : 1;
    }

    // Apply correction and set result
    qkz80_uint8 result;
    if (flag_n) {
      result = rega - diff;
    } else {
      result = rega + diff;
    }

    set_reg8(result, reg_A);
    regs.set_flags_from_daa(result, flag_n, new_h, new_c);
    trace->asm_op("daa");
    return;
  }

  case 0x28: // JR Z (Z80 only)
    if (cpu_mode == MODE_8080)
      return;
    {
      qkz80_int8 offset = (qkz80_int8)pull_byte_from_opcode_stream();
      if (regs.condition_code(1, regs.get_flags())) { // Z condition
        qkz80_uint16 pc = regs.PC.get_pair16();
        regs.PC.set_pair16(pc + offset);
        trace->asm_op("jr z,$%+d", offset);
        trace->comment("taken");
      } else {
        trace->asm_op("jr z,$%+d", offset);
        trace->comment("not taken");
      }
    }
    return;

  case 0x2a: // LHLD (LD HL/IX/IY,(nn))
  {
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint16 pair_val(read_word(addr));
    set_reg16(pair_val,active_hl);
    if (has_dd_prefix) trace->asm_op("ld ix,(0x%0x)",addr);
    else if (has_fd_prefix) trace->asm_op("ld iy,(0x%0x)",addr);
    else trace->asm_op("lhld 0x%0x",addr);
    return;
  }

  case 0x2f: // CPL
  {
    qkz80_uint8 result(get_reg8(reg_A));
    result=result ^ -1;
    set_reg8(result,reg_A);
    regs.set_flags_from_cpl(result);
    trace->asm_op("cpl");
    return;
  }

  case 0x30: // JR NC (Z80 only)
    if (cpu_mode == MODE_8080)
      return;
    {
      qkz80_int8 offset = (qkz80_int8)pull_byte_from_opcode_stream();
      if (!regs.condition_code(3, regs.get_flags())) { // NC condition
        qkz80_uint16 pc = regs.PC.get_pair16();
        regs.PC.set_pair16(pc + offset);
        trace->asm_op("jr nc,$%+d", offset);
        trace->comment("taken");
      } else {
        trace->asm_op("jr nc,$%+d", offset);
        trace->comment("not taken");
      }
    }
    return;

  case 0x32: // STA
  {
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint8 rega(get_reg8(reg_A));
    mem->store_mem(addr,rega);
    trace->asm_op("sta 0x%0x",addr);
    return;
  }

  case 0x37: // SCF
  {
    qkz80_uint8 a_val = get_reg8(reg_A);
    regs.set_flags_from_scf(a_val);
    trace->asm_op("scf");
    return;
  }

  case 0x38: // JR C (Z80 only)
    if (cpu_mode == MODE_8080)
      return;
    {
      qkz80_int8 offset = (qkz80_int8)pull_byte_from_opcode_stream();
      if (regs.condition_code(3, regs.get_flags())) { // C condition
        qkz80_uint16 pc = regs.PC.get_pair16();
        regs.PC.set_pair16(pc + offset);
        trace->asm_op("jr c,$%+d", offset);
        trace->comment("taken");
      } else {
        trace->asm_op("jr c,$%+d", offset);
        trace->comment("not taken");
      }
    }
    return;

  case 0x3a: // LDA
  {
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint8 dat(mem->fetch_mem(addr));
    trace->asm_op("lda 0x%0x",addr);
    set_reg8(dat,reg_A);
    return;
  }

  case 0x3f: // CCF
  {
    qkz80_uint8 a_val = get_reg8(reg_A);
    regs.set_flags_from_ccf(a_val);
    trace->asm_op("ccf");
    return;
  }

  // MOV - Move register to register
  // (opcode & 0xc0) == 0x40: 0x40-0x7f, but 0x76 is HLT
  // Note: 0x76 (HLT) is handled separately below
  case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
  case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
  case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
  case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
  case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
  case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
  case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: /* 0x76=HLT */ case 0x77:
  case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
  {
    qkz80_uint8 src(opcode & 0x07);
    qkz80_uint8 dst((opcode >> 3) & 0x07);

    // Special handling for indexed memory operations (IX+d) or (IY+d)
    if ((has_dd_prefix || has_fd_prefix) && (src == reg_M || dst == reg_M)) {
      qkz80_int8 offset = (qkz80_int8)pull_byte_from_opcode_stream();
      qkz80_uint16 addr = get_reg16(active_hl) + offset;

      if (src == reg_M && dst == reg_M) {
        // LD (IX+d),(IX+d) is illegal - shouldn't happen
        qkz80_global_fatal("illegal MOV (IX+d),(IX+d)");
      } else if (src == reg_M) {
        // LD r,(IX+d) or LD r,(IY+d)
        qkz80_uint8 dat = mem->fetch_mem(addr);
        set_reg8(dat, dst);
        if (has_dd_prefix)
          trace->asm_op("ld %s,(ix%+d)", name_reg8(dst), offset);
        else
          trace->asm_op("ld %s,(iy%+d)", name_reg8(dst), offset);
      } else {
        // LD (IX+d),r or LD (IY+d),r
        qkz80_uint8 dat = get_reg8(src);
        mem->store_mem(addr, dat);
        if (has_dd_prefix)
          trace->asm_op("ld (ix%+d),%s", offset, name_reg8(src));
        else
          trace->asm_op("ld (iy%+d),%s", offset, name_reg8(src));
      }
      return;
    }

    // Special handling for IXH, IXL, IYH, IYL (undocumented Z80 instructions)
    if ((has_dd_prefix || has_fd_prefix) && (src == reg_H || src == reg_L || dst == reg_H || dst == reg_L)) {
      qkz80_uint8 dat;

      // Get source value
      if (src == reg_H) {
        dat = has_dd_prefix ? regs.IX.get_high() : regs.IY.get_high();
      } else if (src == reg_L) {
        dat = has_dd_prefix ? regs.IX.get_low() : regs.IY.get_low();
      } else {
        dat = get_reg8(src);
      }

      // Set destination value
      if (dst == reg_H) {
        if (has_dd_prefix) {
          regs.IX.set_high(dat);
          trace->asm_op("ld ixh,%s", src == reg_H ? "ixh" : (src == reg_L ? "ixl" : name_reg8(src)));
        } else {
          regs.IY.set_high(dat);
          trace->asm_op("ld iyh,%s", src == reg_H ? "iyh" : (src == reg_L ? "iyl" : name_reg8(src)));
        }
      } else if (dst == reg_L) {
        if (has_dd_prefix) {
          regs.IX.set_low(dat);
          trace->asm_op("ld ixl,%s", src == reg_H ? "ixh" : (src == reg_L ? "ixl" : name_reg8(src)));
        } else {
          regs.IY.set_low(dat);
          trace->asm_op("ld iyl,%s", src == reg_H ? "iyh" : (src == reg_L ? "iyl" : name_reg8(src)));
        }
      } else {
        set_reg8(dat, dst);
        if (src == reg_H) {
          trace->asm_op("ld %s,%s", name_reg8(dst), has_dd_prefix ? "ixh" : "iyh");
        } else {
          trace->asm_op("ld %s,%s", name_reg8(dst), has_dd_prefix ? "ixl" : "iyl");
        }
      }
      return;
    }

    qkz80_uint8 dat(get_reg8(src));
    set_reg8(dat,dst);
    trace->asm_op("mov %s,%s",name_reg8(dst),name_reg8(src));
    trace->add_reg8(src);
    return;
  }

  case 0x76: // HLT - halt instruction (in MOV m,m space)
    halt();
    return;

  // ADD - Add register to A
  // (opcode & 0xf8) == 0x80: 0x80-0x87
  case 0x80: case 0x81: case 0x82: case 0x83:
  case 0x84: case 0x85: case 0x86: case 0x87: {
    qkz80_uint8 reg_num(opcode & 0x7);
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 regb(get_reg8(reg_num));
    qkz80_big_uint sum(rega+regb);
    regs.set_flags_from_sum8(sum, rega, regb, 0);
    set_A(sum);
    trace->asm_op("add %s",name_reg8(reg_num));
    trace->add_reg8(reg_num);
    return;
  }

  // ADC - Add register to A with carry
  // (opcode & 0xf8) == 0x88: 0x88-0x8f
  case 0x88: case 0x89: case 0x8a: case 0x8b:
  case 0x8c: case 0x8d: case 0x8e: case 0x8f: {
    qkz80_uint8 reg_num(opcode & 0x7);
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 regb(get_reg8(reg_num));
    qkz80_uint16 carry(fetch_carry_as_int());
    qkz80_big_uint sum(rega+regb+carry);
    regs.set_flags_from_sum8(sum, rega, regb, carry);
    set_A(sum);
    trace->add_reg8(reg_num);
    trace->asm_op("adc %s",name_reg8(reg_num));
    return;
  }

  // SUB - Subtract register from A
  // (opcode & 0xf8) == 0x90: 0x90-0x97
  case 0x90: case 0x91: case 0x92: case 0x93:
  case 0x94: case 0x95: case 0x96: case 0x97: {
    qkz80_uint8 reg_num(opcode & 0x7);
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 regb(get_reg8(reg_num));
    qkz80_big_uint diff(rega-regb);
    regs.set_flags_from_diff8(diff, rega, regb, 0);
    set_A(diff);
    trace->asm_op("sub %s",name_reg8(reg_num));
    trace->add_reg8(reg_num);
    return;
  }

  // SBB - Subtract register from A with borrow
  // (opcode & 0xf8) == 0x98: 0x98-0x9f
  case 0x98: case 0x99: case 0x9a: case 0x9b:
  case 0x9c: case 0x9d: case 0x9e: case 0x9f: {
    qkz80_uint8 reg_num(opcode & 0x7);
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 regb(get_reg8(reg_num));
    qkz80_uint16 carry(fetch_carry_as_int());
    qkz80_big_uint diff(rega-regb-carry);
    regs.set_flags_from_diff8(diff, rega, regb, carry);
    set_A(diff);
    trace->asm_op("sbb %s",name_reg8(reg_num));
    trace->add_reg8(reg_num);
    return;
  }

  // ANA - AND register with A
  // (opcode & 0xf8) == 0xa0: 0xa0-0xa7
  case 0xa0: case 0xa1: case 0xa2: case 0xa3:
  case 0xa4: case 0xa5: case 0xa6: case 0xa7: {
    qkz80_uint8 src_reg(opcode & 0x07);
    qkz80_uint8 dat1(get_reg8(src_reg));
    qkz80_uint8 dat2(get_reg8(reg_A));
    qkz80_uint8 result(dat1 & dat2);
    set_reg8(result,reg_A);
    // Z80: H always 1, 8080: H = bit 3 of (op1 | op2)
    qkz80_uint8 hc = (cpu_mode == MODE_Z80) ? 1 : (((dat1 | dat2) & 0x08) != 0);
    regs.set_flags_from_logic8(result,0,hc);
    trace->asm_op("ana %s",name_reg8(src_reg));
    trace->add_reg8(src_reg);
    return;
  }

  // XRA - XOR register with A
  // (opcode & 0xf8) == 0xa8: 0xa8-0xaf
  case 0xa8: case 0xa9: case 0xaa: case 0xab:
  case 0xac: case 0xad: case 0xae: case 0xaf: {
    qkz80_uint8 src_reg(opcode & 0x07);
    qkz80_uint8 dat1(get_reg8(src_reg));
    qkz80_uint8 dat2(get_reg8(reg_A));
    qkz80_uint8 result(dat1 ^ dat2);
    set_reg8(result,reg_A);
    regs.set_flags_from_logic8(result,0,0);
    trace->asm_op("xra %s",name_reg8(src_reg));
    trace->add_reg8(src_reg);
    return;
  }

  // ORA - OR register with A
  // (opcode & 0xf8) == 0xb0: 0xb0-0xb7
  case 0xb0: case 0xb1: case 0xb2: case 0xb3:
  case 0xb4: case 0xb5: case 0xb6: case 0xb7: {
    qkz80_uint8 src_reg(opcode & 0x07);
    qkz80_uint8 dat1(get_reg8(src_reg));
    qkz80_uint8 dat2(get_reg8(reg_A));
    qkz80_uint8 result(dat1 | dat2);
    set_reg8(result,reg_A);
    regs.set_flags_from_logic8(result,0,0);
    trace->asm_op("ora %s",name_reg8(src_reg));
    trace->add_reg8(src_reg);
    return;
  }

  // CMP - Compare register with A
  // (opcode & 0xf8) == 0xb8: 0xb8-0xbf
  case 0xb8: case 0xb9: case 0xba: case 0xbb:
  case 0xbc: case 0xbd: case 0xbe: case 0xbf: {
    qkz80_uint8 reg_num(opcode & 0x7);
    qkz80_uint8 rega(get_reg8(reg_A));
    qkz80_uint8 regb(get_reg8(reg_num));
    qkz80_big_uint diff(rega-regb);
    regs.set_flags_from_diff8(diff, rega, regb, 0);
    // CP is special: X and Y flags come from the operand, not the result
    qkz80_uint8 flags = regs.get_flags();
    flags &= ~(qkz80_cpu_flags::X | qkz80_cpu_flags::Y);  // Clear X and Y
    if (regb & 0x08) flags |= qkz80_cpu_flags::X;          // Set X from bit 3 of operand
    if (regb & 0x20) flags |= qkz80_cpu_flags::Y;          // Set Y from bit 5 of operand
    regs.set_flags(flags);
    trace->asm_op("cmp %s",name_reg8(reg_num));
    trace->add_reg8(reg_num);
    return;
  }

  // Rxx - Conditional return
  // (opcode & 0xc7) == 0xc0: 0xc0, 0xc8, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8
  case 0xc0: case 0xc8: case 0xd0: case 0xd8:
  case 0xe0: case 0xe8: case 0xf0: case 0xf8: {
    qkz80_big_uint fl_code=(opcode>>3) & 0x7;
    trace->asm_op("r%s",name_condition_code(fl_code));
    if(regs.condition_code(fl_code,regs.get_flags())) {
      qkz80_uint16 addr(pop_word());
      regs.PC.set_pair16(addr);
      trace->comment("conditional ret taken");
    } else {
      trace->comment("conditional ret not taken");
    }
    return;
  }

  // POP - Pop register pair from stack
  // (opcode & 0xcf) == 0xc1: 0xc1, 0xd1, 0xe1, 0xf1
  case 0xc1: case 0xd1: case 0xe1: case 0xf1: {
    qkz80_uint8 rpair((opcode >> 4) & 0x3);
    // SP illegal for pop, that code 3 means AF
    if(rpair==regp_SP) {
      rpair=regp_AF;
    }
    // If DD/FD prefix and register pair is HL (2), use IX/IY instead
    if ((has_dd_prefix || has_fd_prefix) && rpair == regp_HL) {
      rpair = active_hl;
    }
    qkz80_uint16 pair_val(pop_word());
    set_reg16(pair_val,rpair);
    trace->asm_op("pop %s",name_reg16(rpair));
    trace->add_reg16(rpair);
    return;
  }

  // Jccc - Conditional jump
  // (opcode & 0xc7) == 0xc2: 0xc2, 0xca, 0xd2, 0xda, 0xe2, 0xea, 0xf2, 0xfa
  case 0xc2: case 0xca: case 0xd2: case 0xda:
  case 0xe2: case 0xea: case 0xf2: case 0xfa: {
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint8 cc_active((opcode >> 3) & 0x7);
    trace->asm_op("j%s 0x%x",name_condition_code(cc_active),addr);
    if(regs.condition_code(cc_active,regs.get_flags())) {
      regs.PC.set_pair16(addr);
      trace->comment("jump taken");
    } else {
      trace->comment("jump not taken");
    }
    return;
  }

  case 0xc3: // JMP
  {
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    regs.PC.set_pair16(addr);
    trace->asm_op("jmp 0x%0x",addr);
    return;
  }

  // Cccc - Conditional call
  // (opcode & 0xc7) == 0xc4: 0xc4, 0xcc, 0xd4, 0xdc, 0xe4, 0xec, 0xf4, 0xfc
  case 0xc4: case 0xcc: case 0xd4: case 0xdc:
  case 0xe4: case 0xec: case 0xf4: case 0xfc: {
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint8 cc_active((opcode >> 3) & 0x7);
    trace->asm_op("c%s 0x%x",name_condition_code(cc_active),addr);
    if(regs.condition_code(cc_active,regs.get_flags())) {
      const qkz80_uint16 pc=regs.PC.get_pair16();
      push_word(pc);
      regs.PC.set_pair16(addr);
      trace->comment("conditional call taken");
    } else {
      trace->comment("conditional call not taken");
    }
    return;
  }

  // PUSH - Push register pair to stack
  // (opcode & 0xcf) == 0xc5: 0xc5, 0xd5, 0xe5, 0xf5
  case 0xc5: case 0xd5: case 0xe5: case 0xf5: {
    qkz80_uint8 rpair((opcode >> 4) & 0x3);
    // SP illegal for push, that code 3 means AF
    if(rpair==regp_SP) {
      rpair=regp_AF;
    }
    // If DD/FD prefix and register pair is HL (2), use IX/IY instead
    if ((has_dd_prefix || has_fd_prefix) && rpair == regp_HL) {
      rpair = active_hl;
    }
    qkz80_uint16 val(get_reg16(rpair));
    push_word(val);
    trace->asm_op("push %s",name_reg16(rpair));
    trace->add_reg16(rpair);
    return;
  }

  case 0xc6: // ADI - Add immediate to A
  {
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 dat(pull_byte_from_opcode_stream());
    qkz80_big_uint sum(dat+rega);
    regs.set_flags_from_sum8(sum, rega, dat, 0);
    set_A(sum);
    trace->asm_op("adi 0x%0x",dat);
    return;
  }

  // RST - Restart
  // (opcode & 0xc7) == 0xc7: 0xc7, 0xcf, 0xd7, 0xdf, 0xe7, 0xef, 0xf7, 0xff
  case 0xc7: case 0xcf: case 0xd7: case 0xdf:
  case 0xe7: case 0xef: case 0xf7: case 0xff: {
    qkz80_uint16 rst_num((opcode>>3)&0x7);
    const qkz80_uint16 pc=regs.PC.get_pair16();
    push_word(pc);
    qkz80_uint16 addr(rst_num*8);
    regs.PC.set_pair16(addr);
    trace->asm_op("rst %d",rst_num);
    return;
  }

  case 0xc9: // RET
  {
    qkz80_uint16 addr(pop_word());
    regs.PC.set_pair16(addr);
    trace->asm_op("ret");
    return;
  }

  case 0xcd: // CALL
  {
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    const qkz80_uint16 pc=regs.PC.get_pair16();
    push_word(pc);
    regs.PC.set_pair16(addr);
    trace->asm_op("call %0x",addr);
    return;
  }

  case 0xce: // ACI - Add immediate to A with carry
  {
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 dat(pull_byte_from_opcode_stream());
    qkz80_uint16 cy(fetch_carry_as_int());
    qkz80_big_uint sum(dat+rega+cy);
    regs.set_flags_from_sum8(sum, rega, dat, cy);
    set_A(sum);
    trace->asm_op("aci 0x%0x",dat);
    return;
  }

  case 0xd3: // OUT
  {
    qkz80_uint8 port(pull_byte_from_opcode_stream());
    qkz80_uint8 rega(get_reg8(reg_A));
    port_out(port, rega);
    trace->asm_op("out 0x%0x",port);
    trace->add_reg8(reg_A);
    return;
  }

  case 0xd6: // SUI - Subtract immediate from A
  {
    qkz80_uint16 dat(pull_byte_from_opcode_stream());
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_big_uint diff(rega-dat);
    regs.set_flags_from_diff8(diff, rega, dat, 0);
    set_A(diff);
    trace->asm_op("sui 0x%0x",dat);
    return;
  }

  case 0xd9: // EXX - exchange BC,DE,HL with alternates (Z80 only)
  {
    if (cpu_mode == MODE_8080)
      return;
    qkz80_uint16 bc = regs.BC.get_pair16();
    qkz80_uint16 de = regs.DE.get_pair16();
    qkz80_uint16 hl = regs.HL.get_pair16();
    regs.BC.set_pair16(regs.BC_.get_pair16());
    regs.DE.set_pair16(regs.DE_.get_pair16());
    regs.HL.set_pair16(regs.HL_.get_pair16());
    regs.BC_.set_pair16(bc);
    regs.DE_.set_pair16(de);
    regs.HL_.set_pair16(hl);
    trace->asm_op("exx");
    return;
  }

  case 0xdb: // IN
  {
    qkz80_uint8 port(pull_byte_from_opcode_stream());
    trace->asm_op("in 0x%0x",port);
    qkz80_uint8 dat = port_in(port);
    set_reg8(dat,reg_A);
    return;
  }

  case 0xde: // SBI - Subtract immediate from A with borrow
  {
    qkz80_uint16 dat(pull_byte_from_opcode_stream());
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 carry(fetch_carry_as_int());
    qkz80_big_uint diff(rega-dat-carry);
    regs.set_flags_from_diff8(diff, rega, dat, carry);
    set_A(diff);
    trace->asm_op("sbi 0x%0x",dat);
    return;
  }

  case 0xe3: // EX (SP),HL/IX/IY - xthl
  {
    qkz80_uint16 addr(get_reg16(regp_SP));
    qkz80_uint16 dat(mem->fetch_mem16(addr));
    qkz80_uint16 hl(get_reg16(active_hl));
    set_reg16(dat,active_hl);
    mem->store_mem16(addr,hl);
    if (has_dd_prefix) trace->asm_op("ex (sp),ix");
    else if (has_fd_prefix) trace->asm_op("ex (sp),iy");
    else trace->asm_op("xthl");
    return;
  }

  case 0xe6: // ANI - AND immediate with A
  {
    qkz80_uint8 dat1(get_reg8(reg_A));
    qkz80_uint8 dat2(pull_byte_from_opcode_stream());
    qkz80_uint8 result(dat1 & dat2);
    set_reg8(result,reg_A);
    // Z80: H always 1, 8080: H = bit 3 of (op1 | op2)
    qkz80_uint8 hc = (cpu_mode == MODE_Z80) ? 1 : (((dat1 | dat2) & 0x08) != 0);
    regs.set_flags_from_logic8(result,0,hc);
    trace->asm_op("ani 0x%0x",dat2);
    return;
  }

  case 0xe9: // JP (HL/IX/IY) - pchl
  {
    qkz80_uint16 addr(get_reg16(active_hl));
    regs.PC.set_pair16(addr);
    if (has_dd_prefix) trace->asm_op("jp (ix)");
    else if (has_fd_prefix) trace->asm_op("jp (iy)");
    else trace->asm_op("pchl");
    return;
  }

  case 0xeb: // XCHG (EX DE,HL/IX/IY)
  {
    qkz80_uint16 a(get_reg16(regp_DE));
    qkz80_uint16 b(get_reg16(active_hl));
    set_reg16(a,active_hl);
    set_reg16(b,regp_DE);
    if (has_dd_prefix) trace->asm_op("ex de,ix");
    else if (has_fd_prefix) trace->asm_op("ex de,iy");
    else trace->asm_op("xchg");
    return;
  }

  case 0xee: // XRI - XOR immediate with A
  {
    qkz80_uint8 dat1(get_reg8(reg_A));
    qkz80_uint8 dat2(pull_byte_from_opcode_stream());
    qkz80_uint8 result(dat1 ^ dat2);
    set_reg8(result,reg_A);
    regs.set_flags_from_logic8(result,0,0);
    trace->asm_op("xri 0x%0x",dat2);
    return;
  }

  case 0xf3: // DI
    trace->asm_op("di");
    return;

  case 0xf6: // ORI - OR immediate with A
  {
    qkz80_uint8 dat1(get_reg8(reg_A));
    qkz80_uint8 dat2(pull_byte_from_opcode_stream());
    qkz80_uint8 result(dat1 | dat2);
    set_reg8(result,reg_A);
    regs.set_flags_from_logic8(result,0,0);
    trace->asm_op("ori 0x%0x",dat2);
    return;
  }

  case 0xf9: // LD SP,HL/IX/IY - sphl
  {
    qkz80_uint16 addr(get_reg16(active_hl));
    set_reg16(addr,regp_SP);
    if (has_dd_prefix) trace->asm_op("ld sp,ix");
    else if (has_fd_prefix) trace->asm_op("ld sp,iy");
    else trace->asm_op("sphl");
    return;
  }

  case 0xfb: // EI
    trace->asm_op("ei");
    return;

  case 0xfe: // CPI - Compare immediate with A
  {
    qkz80_uint16 dat(pull_byte_from_opcode_stream());
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_big_uint diff(rega-dat);
    regs.set_flags_from_diff8(diff, rega, dat, 0);
    // CP is special: X and Y flags come from the operand, not the result
    qkz80_uint8 flags = regs.get_flags();
    flags &= ~(qkz80_cpu_flags::X | qkz80_cpu_flags::Y);  // Clear X and Y
    if (dat & 0x08) flags |= qkz80_cpu_flags::X;           // Set X from bit 3 of operand
    if (dat & 0x20) flags |= qkz80_cpu_flags::Y;           // Set Y from bit 5 of operand
    regs.set_flags(flags);
    trace->asm_op("cpi 0x%0x",dat);
    trace->add_reg8(reg_A);
    return;
  }

  default:
    unimplemented_opcode(opcode, regs.PC.get_pair16());
    break;
  } // end switch(opcode)
}

// Z80 rotate/shift helper functions
qkz80_uint8 qkz80::do_rlc(qkz80_uint8 val) {
  qkz80_uint8 carry = (val & 0x80) ? 1 : 0;
  qkz80_uint8 result = (val << 1) | carry;
  regs.set_flags_from_rotate8(result, carry);
  return result;
}

qkz80_uint8 qkz80::do_rrc(qkz80_uint8 val) {
  qkz80_uint8 carry = val & 0x01;
  qkz80_uint8 result = (val >> 1) | (carry << 7);
  regs.set_flags_from_rotate8(result, carry);
  return result;
}

qkz80_uint8 qkz80::do_rl(qkz80_uint8 val) {
  qkz80_uint8 old_carry = regs.get_carry_as_int();
  qkz80_uint8 new_carry = (val & 0x80) ? 1 : 0;
  qkz80_uint8 result = (val << 1) | old_carry;
  regs.set_flags_from_rotate8(result, new_carry);
  return result;
}

qkz80_uint8 qkz80::do_rr(qkz80_uint8 val) {
  qkz80_uint8 old_carry = regs.get_carry_as_int();
  qkz80_uint8 new_carry = val & 0x01;
  qkz80_uint8 result = (val >> 1) | (old_carry << 7);
  regs.set_flags_from_rotate8(result, new_carry);
  return result;
}

qkz80_uint8 qkz80::do_sla(qkz80_uint8 val) {
  qkz80_uint8 carry = (val & 0x80) ? 1 : 0;
  qkz80_uint8 result = val << 1;
  regs.set_flags_from_rotate8(result, carry);
  return result;
}

qkz80_uint8 qkz80::do_sra(qkz80_uint8 val) {
  qkz80_uint8 carry = val & 0x01;
  qkz80_uint8 result = (val >> 1) | (val & 0x80);  // preserve sign bit
  regs.set_flags_from_rotate8(result, carry);
  return result;
}

qkz80_uint8 qkz80::do_sll(qkz80_uint8 val) {
  // Undocumented: shift left, bit 0 becomes 1
  qkz80_uint8 carry = (val & 0x80) ? 1 : 0;
  qkz80_uint8 result = (val << 1) | 0x01;
  regs.set_flags_from_rotate8(result, carry);
  return result;
}

qkz80_uint8 qkz80::do_srl(qkz80_uint8 val) {
  qkz80_uint8 carry = val & 0x01;
  qkz80_uint8 result = val >> 1;
  regs.set_flags_from_rotate8(result, carry);
  return result;
}

// Default I/O port implementations - override for machine-specific behavior
void qkz80::port_out(qkz80_uint8 port, qkz80_uint8 value) {
  (void)port;
  (void)value;
  // No-op by default - subclass provides machine-specific I/O
}

qkz80_uint8 qkz80::port_in(qkz80_uint8 port) {
  (void)port;
  // Return 0xFF (floating bus) by default
  return 0xFF;
}

