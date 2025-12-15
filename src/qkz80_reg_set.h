#ifndef I8080_REG_SET
#define I8080_REG_SET

// all of the CPU register for an 8080 emulator

#include "qkz80_reg_pair.h"

class qkz80_reg_set {
 public:
  enum CPUMode {
    MODE_8080,  // Intel 8080 compatibility mode
    MODE_Z80    // Zilog Z80 mode
  };

  qkz80_reg_pair AF;
  qkz80_reg_pair BC;
  qkz80_reg_pair DE;
  qkz80_reg_pair HL;
  qkz80_reg_pair SP;
  qkz80_reg_pair PC;

  // Z80-specific registers
  qkz80_reg_pair IX;  // Index register X
  qkz80_reg_pair IY;  // Index register Y
  qkz80_reg_pair AF_; // Alternate AF
  qkz80_reg_pair BC_; // Alternate BC
  qkz80_reg_pair DE_; // Alternate DE
  qkz80_reg_pair HL_; // Alternate HL
  qkz80_uint8 I;      // Interrupt vector base
  qkz80_uint8 R;      // Memory refresh counter
  qkz80_uint8 IFF1;   // Interrupt flip-flop 1
  qkz80_uint8 IFF2;   // Interrupt flip-flop 2
  qkz80_uint8 IM;     // Interrupt mode (0, 1, or 2)

  CPUMode cpu_mode;   // CPU mode for flag calculations

  bool condition_code(qkz80_uint8 a,qkz80_uint8 cpu_flags) const;
  // Flag setting functions - now aware of CPU mode
  void set_flags_from_logic8(qkz80_big_uint a,
			     qkz80_uint8 new_carry,
			     qkz80_uint8 new_half_carry);
  void set_flags_from_rotate8(qkz80_uint8 result, qkz80_uint8 new_carry);
  void set_flags_from_sum8(qkz80_big_uint result, qkz80_uint8 val1, qkz80_uint8 val2, qkz80_uint8 carry);
  void set_flags_from_sum16(qkz80_big_uint a);
  void set_flags_from_diff8(qkz80_big_uint result, qkz80_uint8 val1, qkz80_uint8 val2, qkz80_uint8 carry);
  void set_flags_from_diff16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2, qkz80_big_uint carry);
  void set_flags_from_add16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2);
  void set_flags_from_adc16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2, qkz80_big_uint carry);
  void set_flags_from_sbc16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2, qkz80_big_uint carry);
  void set_zspa_from_inr(qkz80_uint8 a,qkz80_uint8 half_carry, bool is_increment=true);
  qkz80_uint8 get_flags(void) const;
  void set_flags(qkz80_uint8 new_flags);
  void set_flag_bits(qkz80_uint8 mask);    // Set specified flag bits
  void clear_flag_bits(qkz80_uint8 mask);  // Clear specified flag bits
  qkz80_uint8 fix_flags(qkz80_uint8 new_flags) const;

  void set_carry_from_int(qkz80_big_uint x);
  void set_flags_from_rotate_acc(qkz80_uint8 result_a, qkz80_uint8 new_carry);
  void set_flags_from_cpl(qkz80_uint8 result_a);
  void set_flags_from_scf(qkz80_uint8 a_val);
  void set_flags_from_ccf(qkz80_uint8 a_val);
  void set_flags_from_ld_a_ir(qkz80_uint8 loaded_val);
  void set_flags_from_block_ld(qkz80_uint8 a_val, qkz80_uint8 copied_byte, qkz80_uint16 bc_after);
  void set_flags_from_block_cp(qkz80_uint8 a_val, qkz80_uint8 mem_val, qkz80_uint16 bc_after);
  void set_flags_from_daa(qkz80_uint8 result, qkz80_uint8 n_flag, qkz80_uint8 half_carry, qkz80_uint8 carry);
  qkz80_uint8 get_carry_as_int(void);
};
#endif
