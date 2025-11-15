#include "qkz80.h"
#include "qkz80_cpu_flags.h"
#include <iostream>

static qkz80_trace dummy_trace;
qkz80::qkz80():
  qkz80_debug(false),
  trace(&dummy_trace)
{
}

#define LOW_NIBBLE(xx_foo) ((xx_foo)&0x0f)

void qkz80::cpm_setup_memory(void) {
  qkz80_uint16 start_offset(0x0100);
  regs.PC.set_pair16(start_offset);
  // starting stack
  regs.SP.set_pair16(0xfff0);
  // set ret for each restart
  for(qkz80_uint16 i(1);i<8;i++) {
    qkz80_uint16 addr(i*20);
    qkz80_uint8 return_instruction(0xc9);
    mem.store_mem(addr,return_instruction);
  }
}

qkz80_uint8 qkz80::compute_sum_half_carry(qkz80_uint16 rega,
						      qkz80_uint16 dat,
						      qkz80_uint16 carry) {
  qkz80_uint16 rega_low(LOW_NIBBLE(rega));
  qkz80_uint16 dat_low(LOW_NIBBLE(dat));
  qkz80_uint16 carry_low(LOW_NIBBLE(carry));
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

void qkz80::halt(void) {
  std::cerr
    << "halt"
    << std::endl;
  exit(1);
}

qkz80_uint16 qkz80::read_word(qkz80_uint16 addr) {
  qkz80_uint8 low(mem.fetch_mem(addr));
  qkz80_uint8 high(mem.fetch_mem(addr+1));
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
  case reg_B: return "b";
  case reg_C: return "c";
  case reg_D: return "d";
  case reg_E: return "e";
  case reg_H: return "h";
  case reg_L: return "l";
  case reg_M: return "m";
  case reg_A: return "a";
  }
  return "?";
}

const char *qkz80::name_reg16(qkz80_uint8 rpair) {
  switch(rpair) {
  case regp_BC: return "bc";
  case regp_DE: return "de";
  case regp_HL: return "hl";
  case regp_SP: return "sp";
  case regp_AF: return "psw";
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
  case regp_AF:
    {
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
  default:
    qkz80_global_fatal("set_reg16 bad selector rp=%d",int(rp));
  }
}

void qkz80::write_2_bytes(qkz80_uint16 store_me,qkz80_uint16 location) {
  qkz80_uint8 low(qkz80_GET_CLEAN8(store_me));
  qkz80_uint8 high(qkz80_GET_HIGH8(store_me));
  mem.store_mem(location,low);
  mem.store_mem(location+1,high);
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
    return qkz80_MK_INT16(regs.get_flags(),get_reg8(reg_A));
  case regp_PC:
    return regs.PC.get_pair16();
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
    return mem.fetch_mem(regs.HL.get_pair16());
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
    mem.store_mem(regs.HL.get_pair16(),dat);
    break;
  case reg_A:
    regs.AF.set_high(dat);
    break;
  default:
    qkz80_global_fatal("invalid register reg=%d",int(rnum));
  }
}

qkz80_uint8 qkz80::pull_byte_from_opcode_stream(void) {
  qkz80_uint16 pc=regs.PC.get_pair16();
  qkz80_uint8 opcode_byte(mem.fetch_mem(pc));
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
  const qkz80_uint8 opcode(pull_byte_from_opcode_stream());

  // halt opcoded is coded as mov m,m
  // so check for hlt before mov
  if (opcode == 0x76) { // HLT
    halt();
  }

 if (opcode == 0xcd) { // CALL
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    const qkz80_uint16 pc=regs.PC.get_pair16();
    push_word(pc);
    regs.PC.set_pair16(addr);
    trace->asm_op("call %0x",addr);
    return;
 }

 if ((opcode & 0xc7) == 0xc4) { // Cccc conditional call
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

  if (opcode == 0x2a) { // LHLD
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint16 pair_val(read_word(addr));
    set_reg16(pair_val,regp_HL);
    trace->asm_op("lhld 0x%0x",addr);
    return;
  }

  if ((opcode & 0xcf) == 0x01) { // LXI
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint8 rpair = ((opcode >> 4) & 0x03);
    set_reg16(addr,rpair);
    trace->asm_op("lxi %s,0x%0x",name_reg16(rpair),addr);
    trace->add_reg16(rpair);
    return;
  }

  if ((opcode & 0xc0) == 0x40) { // MOV
    qkz80_uint8 src(opcode & 0x07);
    qkz80_uint8 dst((opcode >> 3) & 0x07);
    qkz80_uint8 dat(get_reg8(src));
    set_reg8(dat,dst);
    trace->asm_op("mov %s,%s",name_reg8(dst),name_reg8(src));
    trace->add_reg8(src);
    return;
  }

  if ((opcode & 0xc7) == 0x06) { // MVI
    qkz80_uint8 dst((opcode >> 3) & 0x07);
    qkz80_uint8 dat(pull_byte_from_opcode_stream());
    set_reg8(dat,dst);
    trace->asm_op("mvi %s,0x%0x",name_reg8(dst),dat);
    trace->add_reg8(dst);
    return;
  }

  if (opcode == 0x3a) { // LDA
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint8 dat(mem.fetch_mem(addr));
    trace->asm_op("lda 0x%0x",addr);
    set_reg8(dat,reg_A);
    return;
  }

  if (opcode == 0x32) { // STA
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint8 rega(get_reg8(reg_A));
    mem.store_mem(addr,rega);
    trace->asm_op("sta 0x%0x",addr);
    return;
  } 

  if (opcode == 0x22) { // SHLD
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    qkz80_uint16 aword(get_reg16(regp_HL));
    write_2_bytes(aword,addr);
    trace->asm_op("shld 0x%0x",addr);
    trace->add_reg16(regp_HL);
    return;
  }

  if ((opcode & 0xcf) == 0x0a) { // LDAX
    qkz80_uint8 rp((opcode >> 4) & 0x03);
    qkz80_uint16 pair(get_reg16(rp));
    qkz80_uint8 dat(mem.fetch_mem(pair));
    trace->add_reg16(rp);
    set_reg8(dat,reg_A);
    trace->asm_op("ldax %s",name_reg16(rp));
    return;
  }

  if ((opcode & 0xcf) == 0x02) { // STAX
    qkz80_uint8 rp((opcode >> 4) & 0x03);
    qkz80_uint16 pair(get_reg16(rp));
    qkz80_uint8 rega(get_reg8(reg_A));
    trace->add_reg16(rp);
    mem.store_mem(pair,rega);
    trace->asm_op("stax %s",name_reg16(rp));
    return;
  }

  if (opcode == 0xeb) { //XCHG
    qkz80_uint16 a(get_reg16(regp_DE));
    qkz80_uint16 b(get_reg16(regp_HL));
    set_reg16(a,regp_HL);
    set_reg16(b,regp_DE);
    trace->asm_op("xchg"); 
    return;
  }

  if ((opcode & 0xf8) == 0x88 ) { // ADC
    qkz80_uint8 reg_num(opcode & 0x7);
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 regb(get_reg8(reg_num));
    qkz80_uint16 carry(fetch_carry_as_int());
    qkz80_big_uint sum(rega+regb+carry);
    qkz80_uint8 hc(compute_sum_half_carry(rega,regb,carry));
    regs.set_flags_from_sum8(sum,hc);
    set_A(sum);
    trace->add_reg8(reg_num);
    trace->asm_op("add %s",name_reg8(reg_num));
    return;
  }

  if ((opcode & 0xf8) == 0x80 ) { // ADD
    qkz80_uint8 reg_num(opcode & 0x7);
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 regb(get_reg8(reg_num));
    qkz80_big_uint sum(rega+regb);
    qkz80_uint8 hc(compute_sum_half_carry(rega,regb,0));
    regs.set_flags_from_sum8(sum,hc);
    set_A(sum);
    trace->asm_op("add %s",name_reg8(reg_num));
    trace->add_reg8(reg_num);
    return;
  }

  if (opcode == 0xc6 ) { // ADI
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 dat(pull_byte_from_opcode_stream());
    qkz80_big_uint sum(dat+rega);
    qkz80_uint8 hc(compute_sum_half_carry(rega,dat,0));
    regs.set_flags_from_sum8(sum,hc);
    set_A(sum);
    trace->asm_op("adi 0x%0x",dat);
    return;
  }

  if (opcode == 0xce ) { // ACI
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 dat(pull_byte_from_opcode_stream());
    qkz80_uint16 cy(fetch_carry_as_int());
    qkz80_big_uint sum(dat+rega+cy);
    qkz80_uint8 hc(compute_sum_half_carry(rega,dat,cy));
    regs.set_flags_from_sum8(sum,hc);
    set_A(sum);
    trace->asm_op("aci 0x%0x",dat);
    return;
  }

  if ((opcode & 0xf8) == 0x90 ) { // SUB
    qkz80_uint8 reg_num(opcode & 0x7);
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 regb(get_reg8(reg_num));
    qkz80_big_uint diff(rega-regb);
    qkz80_uint8 hc(compute_subtract_half_carry(rega,diff,regb,0));
    regs.set_flags_from_diff8(diff,hc);
    set_A(diff);
    trace->asm_op("sub %s",name_reg8(reg_num));
    trace->add_reg8(reg_num);
    return;
  }

  if ((opcode & 0xf8) == 0xb8 ) { // CMP
    qkz80_uint8 reg_num(opcode & 0x7);
    qkz80_uint8 rega(get_reg8(reg_A));
    qkz80_uint8 regb(get_reg8(reg_num));
    qkz80_big_uint diff(rega-regb);
    qkz80_uint8 hc(compute_subtract_half_carry(rega,diff,regb,0));
    regs.set_flags_from_diff8(diff,hc);
    trace->asm_op("cmp %s",name_reg8(reg_num));
    trace->add_reg8(reg_num);
    return;
  }

  if (opcode == 0xfe ) { // CPI
    qkz80_uint16 dat(pull_byte_from_opcode_stream());
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_big_uint diff(rega-dat);
    qkz80_uint8 hc(compute_subtract_half_carry(rega,diff,dat,0));
    regs.set_flags_from_diff8(diff,hc);
    trace->asm_op("cpi 0x%0x",dat);
    trace->add_reg8(reg_A);
    return;
  }

  if (opcode == 0xd6 ) { // SUI
    qkz80_uint16 dat(pull_byte_from_opcode_stream());
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_big_uint diff(rega-dat);
    qkz80_uint8 hc(compute_subtract_half_carry(rega,diff,dat,0));
    regs.set_flags_from_diff8(diff,hc);
    set_A(diff);
    trace->asm_op("cpi 0x%0x",dat);
    return;
  }
 if ((opcode & 0xf8) == 0x98 ) { // SBB
    qkz80_uint8 reg_num(opcode & 0x7);
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 regb(get_reg8(reg_num));
    qkz80_uint16 carry(fetch_carry_as_int());
    qkz80_big_uint diff(rega-regb-carry);
    qkz80_uint8 hc(compute_subtract_half_carry(rega,diff,regb,carry));
    regs.set_flags_from_diff8(diff,hc);
    set_A(diff);
    trace->asm_op("sbb %s",name_reg8(reg_num));
    trace->add_reg8(reg_num);
    return;
 }

 if (opcode == 0xde ) { // SBI
    qkz80_uint16 dat(pull_byte_from_opcode_stream());
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint16 carry(fetch_carry_as_int());
    qkz80_big_uint diff(rega-dat-carry);
    qkz80_uint8 hc(compute_subtract_half_carry(rega,diff,dat,carry));
    regs.set_flags_from_diff8(diff,hc);
    set_A(diff);
    trace->asm_op("sbi 0x%0x",dat);
    return;
 }
 
 if ((opcode & 0xc7) == 0x04 ) { // INR
    qkz80_uint8 reg_num((opcode>>3) & 0x7);
    qkz80_uint8 num(get_reg8(reg_num));
    num++;
    set_reg8(num,reg_num);
    qkz80_uint8 hc((num & 0xf) == 0);
    regs.set_zspa_from_inr(num,hc);
    trace->asm_op("inr %s",name_reg8(reg_num));
    return;
 }
 
 if ((opcode & 0xc7) == 0x05 ) { // DCR
    qkz80_uint8 reg_num((opcode>>3) & 0x7);
    qkz80_uint8 num(get_reg8(reg_num));
    num--;
    set_reg8(num,reg_num);
    qkz80_uint8 hc(!((num & 0xf) == 0xf));
    regs.set_zspa_from_inr(num,hc);
    trace->asm_op("dcr %s",name_reg8(reg_num));
    return;
  }

 if ((opcode & 0xcf) == 0x03 ) { // INX
    qkz80_uint8 rp((opcode >> 4) & 0x03);
    qkz80_uint16 pair_val(get_reg16(rp));
    pair_val++;
    set_reg16(pair_val,rp);
    trace->asm_op("inx %s",name_reg16(rp));
    return;
  }

 if ((opcode & 0xcf) == 0x0b ) { // DCX
    qkz80_uint8 rp((opcode >> 4) & 0x03);
    qkz80_uint16 pair_val(get_reg16(rp));
    pair_val--;
    set_reg16(pair_val,rp);
    trace->asm_op("dcx %s",name_reg16(rp));
    return;
 }

 if ((opcode & 0xcf) == 0x09 ) { //DAD RP
    qkz80_uint8 rp((opcode >> 4) & 0x03);
    qkz80_big_uint pair1(get_reg16(rp));
    qkz80_big_uint pair2(get_reg16(regp_HL));
    qkz80_big_uint sum(pair1+pair2);
    set_reg16(sum,regp_HL);
    regs.set_carry_from_int((sum& ~0x0ffff)!=0);
    trace->asm_op("dad %s",name_reg16(rp));
    trace->add_reg16(rp);
    return;
 }

 if ((opcode & 0xf8) == 0xa0 ) { // ANA S
    qkz80_uint8 src_reg(opcode & 0x07);
    qkz80_uint8 dat1(get_reg8(src_reg));
    qkz80_uint8 dat2(get_reg8(reg_A));
    qkz80_uint8 result(dat1 & dat2);
    set_reg8(result,reg_A);
    qkz80_uint8 hc(((dat1 | dat2) & 0x08) != 0);
    regs.set_flags_from_logic8(result,0,hc);
    trace->asm_op("ana %s",name_reg8(src_reg));
    trace->add_reg8(src_reg);
    return;
  } 

 if (opcode == 0xe6) { // ANI
    qkz80_uint8 dat1(get_reg8(reg_A));
    qkz80_uint8 dat2(pull_byte_from_opcode_stream());
    qkz80_uint8 result(dat1 & dat2);
    set_reg8(result,reg_A);
    qkz80_uint8 hc(((dat1 | dat2) & 0x08) != 0);
    regs.set_flags_from_logic8(result,0,hc);
    trace->asm_op("ani 0x%0x",dat2);
    return;
  }

 if ((opcode & 0xf8) == 0xb0 ) { // ORA S
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

 if (opcode == 0xf6) { // ORI
    qkz80_uint8 dat1(get_reg8(reg_A));
    qkz80_uint8 dat2(pull_byte_from_opcode_stream());
    qkz80_uint8 result(dat1 | dat2);
    set_reg8(result,reg_A);
    regs.set_flags_from_logic8(result,0,0);
    trace->asm_op("ori 0x%0x",dat2);
    return;
  }

 if ((opcode & 0xf8) == 0xA8 ) { //XRA S
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

 if (opcode == 0xee) { // XRI
    qkz80_uint8 dat1(get_reg8(reg_A));
    qkz80_uint8 dat2(pull_byte_from_opcode_stream());
    qkz80_uint8 result(dat1 ^ dat2);
    set_reg8(result,reg_A);
    regs.set_flags_from_logic8(result,0,0);
    trace->asm_op("xri 0x%0x",dat2);
    return;
 }

 if (opcode == 0x2f) { // CMA
    qkz80_uint8 result(get_reg8(reg_A));
    result=result ^ -1;
    set_reg8(result,reg_A);
    trace->asm_op("cmc");
    return;
 }

 if (opcode == 0x3f) { // CMC
   qkz80_uint8 old_carry(regs.get_carry_as_int());
   regs.set_carry_from_int((~old_carry) & 1);
   trace->asm_op("cmc");
   return;
 }

 if (opcode == 0x37) { // STC
   regs.set_carry_from_int(1);
   trace->asm_op("stc");
   return;
  }

 if (opcode == 0xc3) { // JMP
    qkz80_uint16 addr(pull_word_from_opcode_stream());
    regs.PC.set_pair16(addr);
    trace->asm_op("jmp 0x%0x",addr);
    return;
  }

 if ((opcode & 0xc7) == 0xc2) { // Jccc conditional jump
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


 if (opcode == 0) { // NOP
    trace->asm_op("nop");
    return;
 }

 if (opcode == 0xfb) { // EI
    trace->asm_op("ei");
    return;
 }

 if (opcode == 0xf3) { // DI
    trace->asm_op("di");
    return;
 }

 if (opcode == 0xc9) { // RET
    qkz80_uint16 addr(pop_word());
    regs.PC.set_pair16(addr);
    trace->asm_op("ret");
    return;
 }

 if ((opcode & 0xc7) == 0xc0) { // Rxx
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

 if (opcode == 0xe9) { // pchl
    qkz80_uint16 addr(get_reg16(regp_HL));
    regs.PC.set_pair16(addr);
    trace->asm_op("pchl");
    return;
 }

 if (opcode == 0xf9) { // sphl
    qkz80_uint16 addr(get_reg16(regp_HL));
    set_reg16(addr,regp_SP);
    trace->asm_op("sphl");
    return;
  }

 if (opcode == 0xe3) { // xthl
    qkz80_uint16 addr(get_reg16(regp_SP));
    qkz80_uint16 dat(mem.fetch_mem16(addr));
    qkz80_uint16 hl(get_reg16(regp_HL));
    set_reg16(dat,regp_HL);
    mem.store_mem16(addr,hl);
    trace->asm_op("xthl");
    return;
  }

 if (opcode == 0x07) { // RLC
    qkz80_big_uint dat1(get_reg8(reg_A));
    qkz80_big_uint cy(0);
    if((dat1 & 0x080)!=0) {
      cy=1;
    }
    dat1=(dat1<<1) | cy;
    set_reg8(dat1,reg_A);
    regs.set_carry_from_int(cy);
    trace->asm_op("rlc");
    return;
 }
 if (opcode == 0x0f) { // RRC
    qkz80_big_uint dat1(get_reg8(reg_A));
    qkz80_uint8 high_bit(0);
    qkz80_uint8 low_bit(dat1 & 0x1);
    if(low_bit!=0) {
      high_bit=0x80;
    }
    dat1=(dat1>>1) | high_bit;
    set_reg8(dat1,reg_A);
    regs.set_carry_from_int(low_bit);
    trace->asm_op("rrc");
    return;
 }

 if (opcode == 0x17) { // RAL
    qkz80_big_uint a_val(get_reg8(reg_A));
    qkz80_uint8 new_carry(0);
    if((a_val&0x80)!=0)
      new_carry=1;
    qkz80_uint8 old_carry(regs.get_carry_as_int());
    regs.set_carry_from_int(new_carry);
    a_val=(a_val<<1) | old_carry;;
    set_reg8(a_val,reg_A);
    trace->asm_op("ral");
    return;
 }

 if (opcode == 0x1f) { // RAR
    qkz80_big_uint a_val(get_reg8(reg_A));
    qkz80_uint8 new_carry(a_val&1);
    qkz80_uint8 old_carry(regs.get_carry_as_int());
    regs.set_carry_from_int(new_carry);
    a_val=a_val>>1;
    if(old_carry)
      a_val|=0x80;
    else
      a_val&=0x7f;
    set_reg8(a_val,reg_A);
    trace->asm_op("rar");
    return;
  }

 if ((opcode & 0xcf) == 0xc1) { // POP
    qkz80_uint8 rpair((opcode >> 4) & 0x3);
    // SP illegal for push, that code 3 means AF
    if(rpair==regp_SP) {
      rpair=regp_AF;
    }
    qkz80_uint16 pair_val(pop_word());
    set_reg16(pair_val,rpair);
    trace->asm_op("pop %s",name_reg16(rpair));
    trace->add_reg16(rpair);
    return;
  } 

 if ((opcode & 0xcf) == 0xc5) { // PUSH
    qkz80_uint8 rpair((opcode >> 4) & 0x3);
    // SP illegal for push, that code 3 means AF
    if(rpair==regp_SP) {
      rpair=regp_AF;
    }
    qkz80_uint16 val(get_reg16(rpair));
    push_word(val);
    trace->asm_op("push %s",name_reg16(rpair));
    trace->add_reg16(rpair);
    return;
  }

 if (opcode == 0xdb) { // IN
    qkz80_uint8 port(pull_byte_from_opcode_stream());
    // std::cout << "input from port=" << std::hex << int(port) << std::endl;
    //    std::cout << "input from port=" << std::dec << int(port) << std::endl;
    trace->asm_op("in 0x%0x",port);
    // find input byte for 2sio
    qkz80_uint8 dat(0);
    if(port == 0x10)
      dat=2; // transmit buffer empty
    set_reg8(dat,reg_A);    
    return;
  }

 if (opcode == 0xd3) { // OUT
    qkz80_uint8 port(pull_byte_from_opcode_stream());
    qkz80_uint8 rega(get_reg8(reg_A));
    rega&=0x7f;
    if (port == 0x11) {
      char ch(rega);
      std::cout << ch << std::flush;
    } else {
      std::cout << "output to port=" << std::hex << int(port) << " data= " << std::hex << int(rega) << std::endl;
    }
    //    std::cout << "output to port=" << std::dec << int(port) << " data= " << std::hex << int(rega) << std::endl;
    trace->asm_op("port 0x%0x",port);
    trace->add_reg8(reg_A);
    return;
  } 

 if (opcode == 0x27) { // DAA
    qkz80_uint16 rega(get_reg8(reg_A));
    qkz80_uint8 flags(regs.get_flags());
    qkz80_uint8 low_a_nibble(LOW_NIBBLE(rega));
    qkz80_uint8 high_a_nibble(LOW_NIBBLE(rega>>4));
    qkz80_uint8 carry(fetch_carry_as_int());
    qkz80_uint8 half_carry((flags&qkz80_cpu_flags::AC)!=0);
    qkz80_uint16 correction(0);

    qkz80_uint8 high_nib_is_high(high_a_nibble>9);
    qkz80_uint8 low_nib_is_high(low_a_nibble>9);

    if(half_carry || low_nib_is_high)
      correction|=6;

    if(carry || high_nib_is_high || ((high_a_nibble>=9) && low_nib_is_high )) {
      correction|=0x60;
      carry=1;
    }

    qkz80_uint8 sum(rega+correction);
    set_reg8(sum,reg_A);
    qkz80_uint8 new_half_carry(compute_sum_half_carry(rega,correction,0));
    regs.set_zspa_from_inr(sum,new_half_carry);
    regs.set_carry_from_int(carry);
    return;
  }

 if((opcode&0xc7)==0xc7) { // RST x
   qkz80_uint16 rst_num((opcode>>3)&0x7);
   const qkz80_uint16 pc=regs.PC.get_pair16();
   push_word(pc);
   qkz80_uint16 addr(rst_num*8);
   regs.PC.set_pair16(addr);
   trace->asm_op("rst %d",rst_num);
   return;
 }

 {
     const qkz80_uint16 pc=regs.PC.get_pair16();
     printf("unimplemented opcode opcode=%#02x pc=%#04x\n",opcode,pc);
     exit(1);
 }
 return;
}

