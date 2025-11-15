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

static inline qkz80_uint8 fix_flags(qkz80_uint8 new_flags) {
  new_flags&=  ~(qkz80_cpu_flags::UNUSED2|qkz80_cpu_flags::UNUSED3);
  new_flags|=qkz80_cpu_flags::UNUSED1;
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

// Z80-specific: 16-bit ADD (ADD HL,ss / ADD IX,ss / ADD IY,ss)
// Only affects: H (half carry from bit 11), N (reset), C (carry from bit 15)
// Does NOT affect: S, Z, P/V
void qkz80_reg_set::set_flags_from_add16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2) {
  qkz80_uint8 flags = get_flags();

  // Clear N flag (this is addition)
  flags &= ~qkz80_cpu_flags::N;

  // Set carry flag if bit 16 is set
  if (result & 0x10000)
    flags |= qkz80_cpu_flags::CY;
  else
    flags &= ~qkz80_cpu_flags::CY;

  // Set half-carry if carry from bit 11 to bit 12
  // Check if (val1 & 0xFFF) + (val2 & 0xFFF) > 0xFFF
  if (((val1 & 0x0FFF) + (val2 & 0x0FFF)) & 0x1000)
    flags |= qkz80_cpu_flags::H;
  else
    flags &= ~qkz80_cpu_flags::H;

  set_flags(flags);
}

// Z80-specific: 16-bit ADC/SBC (ADC HL,ss / SBC HL,ss)
// Affects: S, Z, H, P/V (overflow), N, C
void qkz80_reg_set::set_flags_from_diff16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2, qkz80_big_uint carry) {
  qkz80_uint8 flags = 0;
  qkz80_uint16 result16 = result & 0xFFFF;

  // Set N flag (this is subtraction)
  flags |= qkz80_cpu_flags::N;

  // Carry flag
  if (result & 0x10000)
    flags |= qkz80_cpu_flags::CY;

  // Zero flag (16-bit result is zero)
  if (result16 == 0)
    flags |= qkz80_cpu_flags::Z;

  // Sign flag (bit 15 of result)
  if (result16 & 0x8000)
    flags |= qkz80_cpu_flags::S;

  // Half-carry from bit 11
  if (((val1 & 0x0FFF) - (val2 & 0x0FFF) - carry) & 0x1000)
    flags |= qkz80_cpu_flags::H;

  // Overflow: set if signs of operands differ from sign of result incorrectly
  // For subtraction: overflow if (val1 and result have different signs) AND (val1 and val2 have different signs)
  qkz80_uint8 val1_sign = (val1 >> 15) & 1;
  qkz80_uint8 val2_sign = (val2 >> 15) & 1;
  qkz80_uint8 res_sign = (result16 >> 15) & 1;
  if ((val1_sign != res_sign) && (val1_sign != val2_sign))
    flags |= qkz80_cpu_flags::P;

  set_flags(fix_flags(flags));
}

