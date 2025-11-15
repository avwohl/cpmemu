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
}

void qkz80_reg_set::set_flags_from_sum8(qkz80_big_uint a,qkz80_uint8 new_half_carry) {
  set_flags_from_logic8(a,(a & 0x0100)!=0,new_half_carry);

  qkz80_uint8 new_flags(get_flags());

  if(parity_info.get_parity_of_byte(a))
    new_flags|=qkz80_cpu_flags::P;
  else
    new_flags&= ~qkz80_cpu_flags::P;

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

