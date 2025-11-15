#ifndef I8080_REG_SET
#define I8080_REG_SET

// all of the CPU register for an 8080 emulator

#include "qkz80_reg_pair.h"

class qkz80_reg_set {
 public:
  qkz80_reg_pair AF;
  qkz80_reg_pair BC;
  qkz80_reg_pair DE;
  qkz80_reg_pair HL;
  qkz80_reg_pair SP;
  qkz80_reg_pair PC;

  bool condition_code(qkz80_uint8 a,qkz80_uint8 cpu_flags) const;
  void set_flags_from_logic8(qkz80_big_uint a,
			     qkz80_uint8 new_carry,
			     qkz80_uint8 new_half_carry);
  void set_flags_from_sum8(qkz80_big_uint a,qkz80_uint8 new_half_carry);
  void set_flags_from_sum16(qkz80_big_uint a);
  void set_flags_from_diff8(qkz80_big_uint a,qkz80_uint8 half_carry);
  void set_zspa_from_inr(qkz80_uint8 a,qkz80_uint8 half_carry);
  qkz80_uint8 get_flags(void) const;
  void set_flags(qkz80_uint8 new_flags);

  void set_carry_from_int(qkz80_big_uint x);
  qkz80_uint8 get_carry_as_int(void);
};
#endif
