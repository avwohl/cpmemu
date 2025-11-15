#include "qkz80_reg_set.h"
#include "qkz80_cpu_flags.h"
#include <iostream>

class parity_info_type {
private:
  qkz80_uint8 parity[256];  

  qkz80_uint8 even_parity(unsigned int i) const {
    i&=0x0ff;
    int bits_on(0);
    while(i!=0) {
      if((i&1) !=0)
	bits_on++;
      i=i>>1;
    }    
    if ((bits_on & 1) ==0)
      return 1;
    return 0;
  }

public:
   qkz80_uint8 get_parity_of_byte(qkz80_uint8 abyte) {
    return parity[0x0ff & abyte];
   }

  parity_info_type() {
    for(unsigned int i=0;i<256;i++) {
      parity[i]=even_parity(i);
    }
  }
};

static parity_info_type parity_info;

// Note: This is now a member function (const), not static, so it can access cpu_mode
qkz80_uint8 qkz80_reg_set::fix_flags(qkz80_uint8 new_flags) const {
  if (cpu_mode == MODE_8080) {
    // 8080 mode: Clear undefined bits, force bit 1 to 1
    new_flags &= ~(qkz80_cpu_flags::UNUSED2 | qkz80_cpu_flags::UNUSED3);
    new_flags |= qkz80_cpu_flags::UNUSED1;
  }
  // Z80 mode: Don't modify flags - N, X, Y are all meaningful
  return new_flags;
}

qkz80_uint8 qkz80_reg_set::get_flags(void) const {
  return fix_flags(AF.get_low());
}

void qkz80_reg_set::set_flags(qkz80_uint8 new_flags) {
  return AF.set_low(fix_flags(new_flags));
}

void qkz80_reg_set::set_flags_from_logic8(qkz80_big_uint a,
					    qkz80_uint8 new_carry,
					    qkz80_uint8 new_half_carry)
{
  qkz80_uint8 sum8bit(a & 0x0ff);
  qkz80_uint8 new_flags=fix_flags(0);

  if(new_carry)
    new_flags|=qkz80_cpu_flags::CY;

  if(new_half_carry!=0)
    new_flags|=qkz80_cpu_flags::AC;
    
  if(sum8bit==0) 
    new_flags|=qkz80_cpu_flags::Z;

  if((sum8bit & 0x080) != 0)
    new_flags|=qkz80_cpu_flags::S;

  if(parity_info.get_parity_of_byte(sum8bit))
    new_flags|=qkz80_cpu_flags::P;

  set_flags(new_flags);
}

void qkz80_reg_set::set_flags_from_diff8(qkz80_big_uint a,qkz80_uint8 new_half_carry) {
  set_flags_from_sum8(a,new_half_carry);

  // Z80: Set N flag for subtraction
  if(cpu_mode == MODE_Z80) {
    qkz80_uint8 flags = get_flags();
    flags |= qkz80_cpu_flags::N;
    set_flags(flags);
  }
}

void qkz80_reg_set::set_flags_from_sum8(qkz80_big_uint a,qkz80_uint8 new_half_carry) {
  set_flags_from_logic8(a,(a & 0x0100)!=0,new_half_carry);

  qkz80_uint8 new_flags(get_flags());

  // Clear N flag for addition (Z80 only, but harmless on 8080)
  new_flags &= ~qkz80_cpu_flags::N;

  if(cpu_mode == MODE_Z80) {
    // Z80: P flag is overflow for arithmetic operations
    // Overflow occurs if sign changes incorrectly
    // For now, use parity - TODO: implement proper overflow detection
    if(parity_info.get_parity_of_byte(a))
      new_flags|=qkz80_cpu_flags::P;
    else
      new_flags&= ~qkz80_cpu_flags::P;
  } else {
    // 8080: P is always parity
    if(parity_info.get_parity_of_byte(a))
      new_flags|=qkz80_cpu_flags::P;
    else
      new_flags&= ~qkz80_cpu_flags::P;
  }

  set_flags(new_flags);
}

void qkz80_reg_set::set_flags_from_sum16(qkz80_big_uint a) {
  qkz80_uint8 result(get_flags());
  if((a & 0x30000) != 0)
    result=result | qkz80_cpu_flags::CY;
  else
    result=result & ~qkz80_cpu_flags::CY;
  set_flags(result);
}

bool qkz80_reg_set::condition_code(qkz80_uint8 cond,qkz80_uint8 cpu_flags) const {
  switch(cond) {
  case 0: //NZ
    return (cpu_flags & qkz80_cpu_flags::Z)==0;
  case 1: //Z
    return (cpu_flags & qkz80_cpu_flags::Z)!=0;
  case 2: //NC
    return (cpu_flags & qkz80_cpu_flags::CY)==0;
  case 3: //C
    return (cpu_flags & qkz80_cpu_flags::CY)!=0;
  case 4: // PO (partity)
    return (cpu_flags & qkz80_cpu_flags::P)==0;
  case 5: // PE (partity)
    return (cpu_flags & qkz80_cpu_flags::P)!=0;
  case 6: // P (positive)
    return (cpu_flags & qkz80_cpu_flags::S)==0;
  case 7: // M (minus)
    return (cpu_flags & qkz80_cpu_flags::S)!=0;
  default:
    qkz80_global_fatal("invalid condition test");
  }
  return 0;
}

qkz80_uint8 qkz80_reg_set::get_carry_as_int(void) {
  qkz80_uint8 flags(get_flags());
  if((flags & qkz80_cpu_flags::CY)!=0)
    return 1;
  return 0;
}

void qkz80_reg_set::set_carry_from_int(qkz80_big_uint x) {
  qkz80_uint8 result(get_flags());
  result&=  ~qkz80_cpu_flags::CY;
  if(x&1) {
    result|= qkz80_cpu_flags::CY;
  }
  set_flags(result);
}

void qkz80_reg_set::set_zspa_from_inr(qkz80_uint8 a,qkz80_uint8 half_carry) {
  a&=0x0ff;
  qkz80_uint8 result(get_flags());
  if(half_carry)
    result|=qkz80_cpu_flags::AC;
  else
    result&= ~qkz80_cpu_flags::AC;

  if(a==0)
    result|=qkz80_cpu_flags::Z;
  else
    result&= ~qkz80_cpu_flags::Z;

  // sign
  if((a&0x80)!=0)
    result|=qkz80_cpu_flags::S;
  else
    result&= ~qkz80_cpu_flags::S;

  if(parity_info.get_parity_of_byte(a))
    result|=qkz80_cpu_flags::P;
  else
    result&= ~qkz80_cpu_flags::P;


  result=fix_flags(result);

  set_flags(result);
}

// Helper: Bit-by-bit 16-bit addition with carry (based on tnylpo)
// Returns result and sets all flags via bit-simulation
static qkz80_uint16 add16_bitwise(qkz80_uint16 s1, qkz80_uint16 s2, int carry_in,
                                   qkz80_uint8& flag_h, qkz80_uint8& flag_c, qkz80_uint8& flag_v,
                                   qkz80_uint8& flag_x, qkz80_uint8& flag_y,
                                   qkz80_uint8& flag_z, qkz80_uint8& flag_s) {
  qkz80_uint16 result = 0;
  qkz80_big_uint cy = carry_in ? 1 : 0;
  qkz80_big_uint ma = 1;
  int c14 = 0;

  for (int i = 0; i < 16; i++) {
    // XOR to get result bit
    result |= (s1 ^ s2 ^ cy) & ma;
    // Calculate carry out: (s2 & cy) | (s1 & (s2 | cy))
    cy = ((s2 & cy) | (s1 & (s2 | cy))) & ma;

    if (i == 11) flag_h = (cy != 0) ? 1 : 0;  // Half-carry from bit 11
    if (i == 14) c14 = (cy != 0) ? 1 : 0;      // Save carry from bit 14
    if (i == 15) flag_c = (cy != 0) ? 1 : 0;   // Carry from bit 15

    cy <<= 1;
    ma <<= 1;
  }

  // Overflow = carry_out_bit15 XOR carry_out_bit14
  flag_v = flag_c ^ c14;

  // Undocumented X and Y flags from bits 11 and 13 of result
  flag_x = (result & 0x0800) ? 1 : 0;  // Bit 11
  flag_y = (result & 0x2000) ? 1 : 0;  // Bit 13

  // Zero and sign flags
  flag_z = (result == 0) ? 1 : 0;
  flag_s = (result & 0x8000) ? 1 : 0;

  return result;
}

// Helper: Bit-by-bit 16-bit subtraction with borrow (based on tnylpo)
static qkz80_uint16 sub16_bitwise(qkz80_uint16 minuend, qkz80_uint16 subtrahend, int borrow_in,
                                   qkz80_uint8& flag_h, qkz80_uint8& flag_c, qkz80_uint8& flag_v,
                                   qkz80_uint8& flag_x, qkz80_uint8& flag_y,
                                   qkz80_uint8& flag_z, qkz80_uint8& flag_s) {
  qkz80_uint16 result = 0;
  qkz80_big_uint cy = borrow_in ? 1 : 0;
  qkz80_big_uint ma = 1;
  int c14 = 0;

  for (int i = 0; i < 16; i++) {
    // XOR to get result bit
    result |= (minuend ^ subtrahend ^ cy) & ma;
    // Calculate borrow: (subtrahend & cy) | (~minuend & (subtrahend | cy))
    cy = ((subtrahend & cy) | (~minuend & (subtrahend | cy))) & ma;

    if (i == 11) flag_h = (cy != 0) ? 1 : 0;  // Half-borrow from bit 11
    if (i == 14) c14 = (cy != 0) ? 1 : 0;      // Save borrow from bit 14
    if (i == 15) flag_c = (cy != 0) ? 1 : 0;   // Borrow from bit 15

    cy <<= 1;
    ma <<= 1;
  }

  // Overflow = borrow_out_bit15 XOR borrow_out_bit14
  flag_v = flag_c ^ c14;

  // Undocumented X and Y flags from bits 11 and 13 of result
  flag_x = (result & 0x0800) ? 1 : 0;  // Bit 11
  flag_y = (result & 0x2000) ? 1 : 0;  // Bit 13

  // Zero and sign flags
  flag_z = (result == 0) ? 1 : 0;
  flag_s = (result & 0x8000) ? 1 : 0;

  return result;
}

// Z80-specific: 16-bit ADD (ADD HL,ss / ADD IX,ss / ADD IY,ss)
// Affects: H, N (cleared), C, X, Y (undocumented)
// Does NOT affect: S, Z, P/V (these are preserved)
void qkz80_reg_set::set_flags_from_add16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2) {
  qkz80_uint8 flags = get_flags();

  // Preserve S, Z, P/V flags (ADD HL doesn't modify them!)
  qkz80_uint8 preserve_mask = qkz80_cpu_flags::S | qkz80_cpu_flags::Z | qkz80_cpu_flags::P;
  qkz80_uint8 preserved = flags & preserve_mask;

  // Use bit-by-bit simulation to get exact flag values
  qkz80_uint8 flag_h, flag_c, flag_v, flag_x, flag_y, flag_z, flag_s;
  add16_bitwise(val1 & 0xFFFF, val2 & 0xFFFF, 0, flag_h, flag_c, flag_v, flag_x, flag_y, flag_z, flag_s);

  // Clear N flag (this is addition)
  flags &= ~qkz80_cpu_flags::N;

  // Set carry flag
  if (flag_c)
    flags |= qkz80_cpu_flags::CY;
  else
    flags &= ~qkz80_cpu_flags::CY;

  // Set half-carry flag
  if (flag_h)
    flags |= qkz80_cpu_flags::H;
  else
    flags &= ~qkz80_cpu_flags::H;

  // Set undocumented X and Y flags from result
  if (flag_x)
    flags |= qkz80_cpu_flags::X;
  else
    flags &= ~qkz80_cpu_flags::X;

  if (flag_y)
    flags |= qkz80_cpu_flags::Y;
  else
    flags &= ~qkz80_cpu_flags::Y;

  // Restore preserved flags (S, Z, P/V)
  flags = (flags & ~preserve_mask) | preserved;

  set_flags(flags);
}

// Z80-specific: 16-bit ADC HL,ss
// Affects: S, Z, H, P/V (overflow), N (cleared), C, X, Y (undocumented)
void qkz80_reg_set::set_flags_from_adc16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2, qkz80_big_uint carry) {
  // Use bit-by-bit simulation to get exact flag values (addition with carry)
  qkz80_uint8 flag_h, flag_c, flag_v, flag_x, flag_y, flag_z, flag_s;
  add16_bitwise(val1 & 0xFFFF, val2 & 0xFFFF, carry, flag_h, flag_c, flag_v, flag_x, flag_y, flag_z, flag_s);

  qkz80_uint8 flags = 0;

  // Clear N flag (this is addition)
  // N already 0, no need to clear

  // Set all flags from bit-by-bit simulation
  if (flag_c) flags |= qkz80_cpu_flags::CY;
  if (flag_h) flags |= qkz80_cpu_flags::H;
  if (flag_v) flags |= qkz80_cpu_flags::P;
  if (flag_z) flags |= qkz80_cpu_flags::Z;
  if (flag_s) flags |= qkz80_cpu_flags::S;
  if (flag_x) flags |= qkz80_cpu_flags::X;
  if (flag_y) flags |= qkz80_cpu_flags::Y;

  set_flags(fix_flags(flags));
}

// Z80-specific: 16-bit SBC HL,ss
// Affects: S, Z, H, P/V (overflow), N (set), C, X, Y (undocumented)
void qkz80_reg_set::set_flags_from_sbc16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2, qkz80_big_uint carry) {
  // Use bit-by-bit simulation to get exact flag values (subtraction with borrow)
  qkz80_uint8 flag_h, flag_c, flag_v, flag_x, flag_y, flag_z, flag_s;
  sub16_bitwise(val1 & 0xFFFF, val2 & 0xFFFF, carry, flag_h, flag_c, flag_v, flag_x, flag_y, flag_z, flag_s);

  qkz80_uint8 flags = 0;

  // Set N flag (this is subtraction)
  flags |= qkz80_cpu_flags::N;

  // Set all flags from bit-by-bit simulation
  if (flag_c) flags |= qkz80_cpu_flags::CY;
  if (flag_h) flags |= qkz80_cpu_flags::H;
  if (flag_v) flags |= qkz80_cpu_flags::P;
  if (flag_z) flags |= qkz80_cpu_flags::Z;
  if (flag_s) flags |= qkz80_cpu_flags::S;
  if (flag_x) flags |= qkz80_cpu_flags::X;
  if (flag_y) flags |= qkz80_cpu_flags::Y;

  set_flags(fix_flags(flags));
}

// Z80-specific: 16-bit ADC/SBC (ADC HL,ss / SBC HL,ss)
// Kept for backward compatibility - redirects to appropriate function
void qkz80_reg_set::set_flags_from_diff16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2, qkz80_big_uint carry) {
  // Default to SBC behavior (subtraction)
  set_flags_from_sbc16(result, val1, val2, carry);
}

