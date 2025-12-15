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
    for(unsigned int i=0; i<256; i++) {
      parity[i]=even_parity(i);
    }
  }
};

static parity_info_type parity_info;

// Forward declarations of helper functions for bit-by-bit flag simulation
static qkz80_uint8 add8_bitwise(qkz80_uint8 s1, qkz80_uint8 s2, int carry_in,
                                qkz80_uint8& flag_h, qkz80_uint8& flag_c, qkz80_uint8& flag_v,
                                qkz80_uint8& flag_x, qkz80_uint8& flag_y,
                                qkz80_uint8& flag_z, qkz80_uint8& flag_s);
static qkz80_uint8 sub8_bitwise(qkz80_uint8 minuend, qkz80_uint8 subtrahend, int borrow_in,
                                qkz80_uint8& flag_h, qkz80_uint8& flag_c, qkz80_uint8& flag_v,
                                qkz80_uint8& flag_x, qkz80_uint8& flag_y,
                                qkz80_uint8& flag_z, qkz80_uint8& flag_s);

// Note: This is now a member function (const), not static, so it can access cpu_mode
qkz80_uint8 qkz80_reg_set::fix_flags(qkz80_uint8 new_flags) const {
  if (cpu_mode == MODE_8080) {
    // 8080 mode: Clear bits 3,5 (always 0 in 8080), force bit 1 to 1 (always 1 in 8080)
    new_flags &= ~(qkz80_cpu_flags::UNUSED2 | qkz80_cpu_flags::UNUSED3);
    new_flags |= qkz80_cpu_flags::UNUSED1;
  }
  // Z80 mode: Don't modify flags - N, X, Y are all meaningful
  return new_flags;
}

qkz80_uint8 qkz80_reg_set::get_flags(void) const {
  return fix_flags(AF.get_low());  // Always return properly fixed flags
}

void qkz80_reg_set::set_flags(qkz80_uint8 new_flags) {
  return AF.set_low(fix_flags(new_flags));  // Always fix flag bits when storing in 8080 mode
}

void qkz80_reg_set::set_flag_bits(qkz80_uint8 mask) {
  set_flags(get_flags() | mask);
}

void qkz80_reg_set::clear_flag_bits(qkz80_uint8 mask) {
  set_flags(get_flags() & ~mask);
}

void qkz80_reg_set::set_flags_from_logic8(qkz80_big_uint a,
    qkz80_uint8 new_carry,
    qkz80_uint8 new_half_carry) {
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

  // N flag is cleared for logical operations
  // (already 0 since we started with fix_flags(0))

  // X and Y flags (Z80 only) - copy bits 3 and 5 of result
  if(cpu_mode == MODE_Z80) {
    if (sum8bit & 0x08)
      new_flags |= qkz80_cpu_flags::X;  // Copy bit 3
    if (sum8bit & 0x20)
      new_flags |= qkz80_cpu_flags::Y;  // Copy bit 5
  }

  set_flags(new_flags);
}

// Flag setting for CB-prefixed rotate/shift instructions (RLC, RRC, RL, RR, SLA, SRA, SLL, SRL)
// These differ from logical operations in that H is always 0 (not calculated from operands)
void qkz80_reg_set::set_flags_from_rotate8(qkz80_uint8 result, qkz80_uint8 new_carry) {
  qkz80_uint8 new_flags = 0;

  // Set carry flag
  if (new_carry)
    new_flags |= qkz80_cpu_flags::CY;

  // H flag is always cleared for rotate/shift (NOT set based on calculation)
  // N flag is always cleared for rotate/shift
  // (Both already 0)

  // Set Z flag
  if (result == 0)
    new_flags |= qkz80_cpu_flags::Z;

  // Set S flag
  if (result & 0x80)
    new_flags |= qkz80_cpu_flags::S;

  // Set P/V flag as parity
  if (parity_info.get_parity_of_byte(result))
    new_flags |= qkz80_cpu_flags::P;

  // X and Y flags (Z80 only) - copy bits 3 and 5 of result
  if (cpu_mode == MODE_Z80) {
    if (result & 0x08)
      new_flags |= qkz80_cpu_flags::X;  // Copy bit 3
    if (result & 0x20)
      new_flags |= qkz80_cpu_flags::Y;  // Copy bit 5
  }

  set_flags(new_flags);
}

// 8-bit addition (ADD, ADC) - uses bit-by-bit simulation for exact flag calculation
void qkz80_reg_set::set_flags_from_sum8(qkz80_big_uint result, qkz80_uint8 val1, qkz80_uint8 val2, qkz80_uint8 carry) {
  // Use bit-by-bit simulation to get exact flag values
  qkz80_uint8 flag_h(0), flag_c, flag_v, flag_x, flag_y, flag_z, flag_s;
  add8_bitwise(val1, val2, carry, flag_h, flag_c, flag_v, flag_x, flag_y, flag_z, flag_s);

  qkz80_uint8 flags = 0;

  // Set all flags from bit-by-bit simulation
  if (flag_c) flags |= qkz80_cpu_flags::CY;
  if (flag_h) flags |= qkz80_cpu_flags::H;
  if (flag_z) flags |= qkz80_cpu_flags::Z;
  if (flag_s) flags |= qkz80_cpu_flags::S;
  if (flag_x) flags |= qkz80_cpu_flags::X;
  if (flag_y) flags |= qkz80_cpu_flags::Y;

  // N flag is cleared for addition
  // (already 0 since we started with flags = 0)

  if (cpu_mode == MODE_Z80) {
    // Z80: P/V flag is overflow for arithmetic operations
    if (flag_v) flags |= qkz80_cpu_flags::P;
  } else {
    // 8080: P is always parity
    if (parity_info.get_parity_of_byte(result & 0xFF))
      flags |= qkz80_cpu_flags::P;
  }

  set_flags(flags);
}

// 8-bit subtraction (SUB, SBC, CP) - uses bit-by-bit simulation for exact flag calculation
void qkz80_reg_set::set_flags_from_diff8(qkz80_big_uint result, qkz80_uint8 val1, qkz80_uint8 val2, qkz80_uint8 carry) {
  // Use bit-by-bit simulation to get exact flag values
  qkz80_uint8 flag_h(0), flag_c, flag_v, flag_x, flag_y, flag_z, flag_s;
  sub8_bitwise(val1, val2, carry, flag_h, flag_c, flag_v, flag_x, flag_y, flag_z, flag_s);

  qkz80_uint8 flags = 0;

  // Set all flags from bit-by-bit simulation
  if (flag_c) flags |= qkz80_cpu_flags::CY;

  // Half-carry calculation differs between Z80 and 8080
  if (cpu_mode == MODE_Z80) {
    // Z80: Use borrow from bit-by-bit simulation
    if (flag_h) flags |= qkz80_cpu_flags::H;
  } else {
    // 8080: Half-carry uses special formula (not borrow!)
    // Formula from 8080: ~(a ^ result ^ b) & 0x10
    // This matches real 8080 behavior
    qkz80_uint8 result8 = result & 0xFF;
    if ((~(val1 ^ result8 ^ val2) & 0x10) != 0) {
      flags |= qkz80_cpu_flags::H;
    }
  }
  if (flag_z) flags |= qkz80_cpu_flags::Z;
  if (flag_s) flags |= qkz80_cpu_flags::S;
  if (flag_x) flags |= qkz80_cpu_flags::X;
  if (flag_y) flags |= qkz80_cpu_flags::Y;

  // N flag is set for subtraction
  flags |= qkz80_cpu_flags::N;

  if (cpu_mode == MODE_Z80) {
    // Z80: P/V flag is overflow for arithmetic operations
    if (flag_v) flags |= qkz80_cpu_flags::P;
  } else {
    // 8080: P is always parity
    if (parity_info.get_parity_of_byte(result & 0xFF))
      flags |= qkz80_cpu_flags::P;
  }

  set_flags(flags);
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

// Set flags for accumulator rotate/shift instructions (RLCA, RRCA, RLA, RRA)
// These instructions only affect: C, H (cleared), N (cleared), X, Y (from result)
// S, Z, P/V are preserved
void qkz80_reg_set::set_flags_from_rotate_acc(qkz80_uint8 result_a, qkz80_uint8 new_carry) {
  qkz80_uint8 flags = get_flags();

  // Set carry
  if (new_carry)
    flags |= qkz80_cpu_flags::CY;
  else
    flags &= ~qkz80_cpu_flags::CY;

  // Clear N and H flags (Z80 only)
  // In 8080 mode, rotate instructions don't affect H flag
  if (cpu_mode == MODE_Z80) {
    flags &= ~(qkz80_cpu_flags::N | qkz80_cpu_flags::H);
  }

  // Set X and Y flags from result (Z80 only)
  if (cpu_mode == MODE_Z80) {
    if (result_a & 0x08)
      flags |= qkz80_cpu_flags::X;
    else
      flags &= ~qkz80_cpu_flags::X;

    if (result_a & 0x20)
      flags |= qkz80_cpu_flags::Y;
    else
      flags &= ~qkz80_cpu_flags::Y;
  }

  set_flags(flags);
}

// CPL - Complement accumulator
// Sets N=1, H=1, X and Y from result, preserves S, Z, P/V, C
void qkz80_reg_set::set_flags_from_cpl(qkz80_uint8 result_a) {
  qkz80_uint8 flags = get_flags();

  // Set N and H flags
  flags |= (qkz80_cpu_flags::N | qkz80_cpu_flags::H);

  // Set X and Y flags from result (Z80 only)
  if (cpu_mode == MODE_Z80) {
    if (result_a & 0x08)
      flags |= qkz80_cpu_flags::X;
    else
      flags &= ~qkz80_cpu_flags::X;

    if (result_a & 0x20)
      flags |= qkz80_cpu_flags::Y;
    else
      flags &= ~qkz80_cpu_flags::Y;
  }

  set_flags(flags);
}

// SCF - Set carry flag
// Sets C=1, N=0, H=0, X and Y from A, preserves S, Z, P/V
void qkz80_reg_set::set_flags_from_scf(qkz80_uint8 a_val) {
  qkz80_uint8 flags = get_flags();

  // Set carry
  flags |= qkz80_cpu_flags::CY;

  // Clear N and H
  flags &= ~(qkz80_cpu_flags::N | qkz80_cpu_flags::H);

  // Set X and Y from A (Z80 only)
  if (cpu_mode == MODE_Z80) {
    if (a_val & 0x08)
      flags |= qkz80_cpu_flags::X;
    else
      flags &= ~qkz80_cpu_flags::X;

    if (a_val & 0x20)
      flags |= qkz80_cpu_flags::Y;
    else
      flags &= ~qkz80_cpu_flags::Y;
  }

  set_flags(flags);
}

// CCF - Complement carry flag
// Sets C=NOT(C), N=0, H=old C, X and Y from A, preserves S, Z, P/V
void qkz80_reg_set::set_flags_from_ccf(qkz80_uint8 a_val) {
  qkz80_uint8 flags = get_flags();
  qkz80_uint8 old_carry = (flags & qkz80_cpu_flags::CY) ? 1 : 0;

  // Complement carry
  if (old_carry)
    flags &= ~qkz80_cpu_flags::CY;
  else
    flags |= qkz80_cpu_flags::CY;

  // Clear N
  flags &= ~qkz80_cpu_flags::N;

  // H gets old carry value
  if (old_carry)
    flags |= qkz80_cpu_flags::H;
  else
    flags &= ~qkz80_cpu_flags::H;

  // Set X and Y from A (Z80 only)
  if (cpu_mode == MODE_Z80) {
    if (a_val & 0x08)
      flags |= qkz80_cpu_flags::X;
    else
      flags &= ~qkz80_cpu_flags::X;

    if (a_val & 0x20)
      flags |= qkz80_cpu_flags::Y;
    else
      flags &= ~qkz80_cpu_flags::Y;
  }

  set_flags(flags);
}

// LD A,I and LD A,R - Special LD instructions that affect flags
// Sets S, Z, H=0, N=0, P/V=IFF2, preserves C, X and Y from loaded value
void qkz80_reg_set::set_flags_from_ld_a_ir(qkz80_uint8 loaded_val) {
  qkz80_uint8 flags = get_flags();

  // Preserve carry
  qkz80_uint8 old_carry = flags & qkz80_cpu_flags::CY;

  // Start fresh
  flags = old_carry;

  // Set S flag (sign)
  if (loaded_val & 0x80)
    flags |= qkz80_cpu_flags::S;

  // Set Z flag (zero)
  if (loaded_val == 0)
    flags |= qkz80_cpu_flags::Z;

  // H and N are cleared (already 0)

  // P/V flag = IFF2 (interrupt flip-flop 2 state)
  if (IFF2)
    flags |= qkz80_cpu_flags::P;

  // Set X and Y flags from loaded value (Z80 only)
  if (cpu_mode == MODE_Z80) {
    if (loaded_val & 0x08)
      flags |= qkz80_cpu_flags::X;
    if (loaded_val & 0x20)
      flags |= qkz80_cpu_flags::Y;
  }

  set_flags(flags);
}

// Block Load Instructions (LDI/LDIR/LDD/LDDR) - Flag updates
// H: Reset to 0
// N: Reset to 0
// P/V: Set if BC != 0 after decrement, reset if BC = 0
// S, Z, C: Preserved (unchanged)
// X, Y (undocumented): From bits 3 and 5 of (A + copied_byte)
void qkz80_reg_set::set_flags_from_block_ld(qkz80_uint8 a_val, qkz80_uint8 copied_byte, qkz80_uint16 bc_after) {
  qkz80_uint8 flags = get_flags();

  // Preserve S, Z, C flags
  qkz80_uint8 preserved = flags & (qkz80_cpu_flags::S | qkz80_cpu_flags::Z | qkz80_cpu_flags::CY);
  flags = preserved;

  // Clear H and N
  // (already cleared since we started with just preserved flags)

  // P/V flag: Set if BC != 0 after decrement
  if (bc_after != 0)
    flags |= qkz80_cpu_flags::P;

  // X and Y flags (Z80 only): From A + copied_byte
  if (cpu_mode == MODE_Z80) {
    qkz80_uint8 n = a_val + copied_byte;
    if (n & 0x08)
      flags |= qkz80_cpu_flags::X;
    if (n & 0x02)  // Note: Y flag is bit 1 of (A + byte), not bit 5!
      flags |= qkz80_cpu_flags::Y;
  }

  set_flags(flags);
}

// Block Compare Instructions (CPI/CPIR/CPD/CPDR) - Flag updates
// Performs A - (HL) comparison and sets flags accordingly
// S, Z, H: From comparison (A - (HL))
// N: Set to 1 (subtraction)
// P/V: Set if BC != 0 after decrement (NOT overflow!)
// C: Preserved (unchanged)
// X, Y (undocumented): From (A - (HL) - H) where H is the half-carry
void qkz80_reg_set::set_flags_from_block_cp(qkz80_uint8 a_val, qkz80_uint8 mem_val, qkz80_uint16 bc_after) {
  // First do normal subtraction to get S, Z, H, N flags
  qkz80_uint8 flag_h(0), flag_c, flag_v, flag_x, flag_y, flag_z, flag_s;
  sub8_bitwise(a_val, mem_val, 0, flag_h, flag_c, flag_v, flag_x, flag_y, flag_z, flag_s);

  qkz80_uint8 flags = get_flags();

  // Preserve carry flag
  qkz80_uint8 old_carry = flags & qkz80_cpu_flags::CY;

  // Build new flags
  flags = old_carry;  // Start with preserved carry

  // Set flags from subtraction
  if (flag_s) flags |= qkz80_cpu_flags::S;
  if (flag_z) flags |= qkz80_cpu_flags::Z;
  if (flag_h) flags |= qkz80_cpu_flags::H;
  flags |= qkz80_cpu_flags::N;  // N is always set (subtraction)

  // P/V flag: Set if BC != 0 after decrement (NOT overflow!)
  if (bc_after != 0)
    flags |= qkz80_cpu_flags::P;

  // X and Y flags (Z80 only): From (A - (HL) - H)
  if (cpu_mode == MODE_Z80) {
    qkz80_uint8 result = a_val - mem_val;
    qkz80_uint8 n = result - (flag_h ? 1 : 0);  // Subtract half-carry
    if (n & 0x08)
      flags |= qkz80_cpu_flags::X;
    if (n & 0x02)  // Y is bit 1 of the adjusted result
      flags |= qkz80_cpu_flags::Y;
  }

  set_flags(flags);
}

// Flag setting for DAA instruction
// DAA must preserve the N flag from the previous operation
void qkz80_reg_set::set_flags_from_daa(qkz80_uint8 result, qkz80_uint8 n_flag, qkz80_uint8 half_carry, qkz80_uint8 carry) {
  qkz80_uint8 flags = 0;

  // Set carry flag
  if (carry)
    flags |= qkz80_cpu_flags::CY;

  // Set half-carry flag
  if (half_carry)
    flags |= qkz80_cpu_flags::H;

  // Set zero flag
  if (result == 0)
    flags |= qkz80_cpu_flags::Z;

  // Set sign flag
  if (result & 0x80)
    flags |= qkz80_cpu_flags::S;

  // PRESERVE N flag from previous operation (passed as parameter)
  if (n_flag)
    flags |= qkz80_cpu_flags::N;

  // Set P/V flag as parity (not overflow)
  if (parity_info.get_parity_of_byte(result))
    flags |= qkz80_cpu_flags::P;

  // Set X and Y flags from result (Z80 only)
  if (cpu_mode == MODE_Z80) {
    if (result & 0x08)
      flags |= qkz80_cpu_flags::X;
    if (result & 0x20)
      flags |= qkz80_cpu_flags::Y;
  }

  set_flags(flags);
}

void qkz80_reg_set::set_zspa_from_inr(qkz80_uint8 a,qkz80_uint8 half_carry, bool is_increment) {
  a&=0x0ff;
  qkz80_uint8 result(get_flags());

  // Half carry (H/AC)
  if(half_carry)
    result|=qkz80_cpu_flags::AC;
  else
    result&= ~qkz80_cpu_flags::AC;

  // Zero flag
  if(a==0)
    result|=qkz80_cpu_flags::Z;
  else
    result&= ~qkz80_cpu_flags::Z;

  // Sign flag
  if((a&0x80)!=0)
    result|=qkz80_cpu_flags::S;
  else
    result&= ~qkz80_cpu_flags::S;

  // N flag (Z80 only) - subtract flag
  if(cpu_mode == MODE_Z80) {
    if (is_increment) {
      result &= ~qkz80_cpu_flags::N;  // Clear N for INC (addition)
    } else {
      result |= qkz80_cpu_flags::N;   // Set N for DEC (subtraction)
    }
  }

  // P/V flag
  if(cpu_mode == MODE_Z80) {
    // Z80: P/V is overflow for INC/DEC
    // For INC: overflow when 0x7F -> 0x80 (positive to negative)
    // For DEC: overflow when 0x80 -> 0x7F (negative to positive)
    bool overflow = false;
    if (is_increment) {
      overflow = (a == 0x80);  // Just incremented to 0x80
    } else {
      overflow = (a == 0x7F);  // Just decremented to 0x7F
    }

    if (overflow)
      result|=qkz80_cpu_flags::P;
    else
      result&= ~qkz80_cpu_flags::P;
  } else {
    // 8080: P is always parity
    if(parity_info.get_parity_of_byte(a))
      result|=qkz80_cpu_flags::P;
    else
      result&= ~qkz80_cpu_flags::P;
  }

  // X and Y flags (Z80 only) - undocumented flags copy bits 3 and 5
  if(cpu_mode == MODE_Z80) {
    if (a & 0x08)
      result |= qkz80_cpu_flags::X;  // Copy bit 3
    else
      result &= ~qkz80_cpu_flags::X;

    if (a & 0x20)
      result |= qkz80_cpu_flags::Y;  // Copy bit 5
    else
      result &= ~qkz80_cpu_flags::Y;
  }

  set_flags(result);
}

// Helper: Bit-by-bit 8-bit addition with carry (based on tnylpo)
// Returns result and sets all flags via bit-simulation
static qkz80_uint8 add8_bitwise(qkz80_uint8 s1, qkz80_uint8 s2, int carry_in,
                                qkz80_uint8& flag_h, qkz80_uint8& flag_c, qkz80_uint8& flag_v,
                                qkz80_uint8& flag_x, qkz80_uint8& flag_y,
                                qkz80_uint8& flag_z, qkz80_uint8& flag_s) {
  qkz80_uint8 result = 0;
  qkz80_uint16 cy = carry_in ? 1 : 0;
  qkz80_uint16 ma = 1;
  int c6 = 0;  // Carry out of bit 6 (for overflow calculation)

  for (int i = 0; i < 8; i++) {
    // XOR to get result bit
    result |= (s1 ^ s2 ^ cy) & ma;
    // Calculate carry out: (s2 & cy) | (s1 & (s2 | cy))
    cy = ((s2 & cy) | (s1 & (s2 | cy))) & ma;

    if (i == 3) flag_h = (cy != 0) ? 1 : 0;  // Half-carry from bit 3
    if (i == 6) c6 = (cy != 0) ? 1 : 0;      // Save carry from bit 6
    if (i == 7) flag_c = (cy != 0) ? 1 : 0;  // Carry from bit 7

    // Shift carry for next iteration (like tnylpo)
    cy <<= 1;
    ma <<= 1;
  }

  // Overflow = carry_out_bit7 XOR carry_out_bit6
  flag_v = flag_c ^ c6;

  // Undocumented X and Y flags from bits 3 and 5 of result
  flag_x = (result & 0x08) ? 1 : 0;  // Bit 3
  flag_y = (result & 0x20) ? 1 : 0;  // Bit 5

  // Zero and sign flags
  flag_z = (result == 0) ? 1 : 0;
  flag_s = (result & 0x80) ? 1 : 0;

  return result;
}

// Helper: Bit-by-bit 8-bit subtraction with borrow (based on tnylpo)
static qkz80_uint8 sub8_bitwise(qkz80_uint8 minuend, qkz80_uint8 subtrahend, int borrow_in,
                                qkz80_uint8& flag_h, qkz80_uint8& flag_c, qkz80_uint8& flag_v,
                                qkz80_uint8& flag_x, qkz80_uint8& flag_y,
                                qkz80_uint8& flag_z, qkz80_uint8& flag_s) {
  qkz80_uint8 result = 0;
  qkz80_uint16 cy = borrow_in ? 1 : 0;
  qkz80_uint16 ma = 1;
  int c6 = 0;  // Borrow out of bit 6 (for overflow calculation)

  for (int i = 0; i < 8; i++) {
    // XOR to get result bit
    result |= (minuend ^ subtrahend ^ cy) & ma;
    // Calculate borrow: (subtrahend & cy) | (~minuend & (subtrahend | cy))
    cy = ((subtrahend & cy) | (~minuend & (subtrahend | cy))) & ma;

    if (i == 3) flag_h = (cy != 0) ? 1 : 0;  // Half-borrow from bit 3
    if (i == 6) c6 = (cy != 0) ? 1 : 0;      // Save borrow from bit 6
    if (i == 7) flag_c = (cy != 0) ? 1 : 0;  // Borrow from bit 7

    cy <<= 1;
    ma <<= 1;
  }

  // Overflow = borrow_out_bit7 XOR borrow_out_bit6
  flag_v = flag_c ^ c6;

  // Undocumented X and Y flags from bits 3 and 5 of result
  flag_x = (result & 0x08) ? 1 : 0;  // Bit 3
  flag_y = (result & 0x20) ? 1 : 0;  // Bit 5

  // Zero and sign flags
  flag_z = (result == 0) ? 1 : 0;
  flag_s = (result & 0x80) ? 1 : 0;

  return result;
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
  qkz80_uint8 flag_h(0), flag_c, flag_v, flag_x, flag_y, flag_z, flag_s;
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
  qkz80_uint8 flag_h(0), flag_c, flag_v, flag_x, flag_y, flag_z, flag_s;
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

  set_flags(flags);  // set_flags() already calls fix_flags(), no need to double-call
}

// Z80-specific: 16-bit SBC HL,ss
// Affects: S, Z, H, P/V (overflow), N (set), C, X, Y (undocumented)
void qkz80_reg_set::set_flags_from_sbc16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2, qkz80_big_uint carry) {
  // Use bit-by-bit simulation to get exact flag values (subtraction with borrow)
  qkz80_uint8 flag_h(0), flag_c, flag_v, flag_x, flag_y, flag_z, flag_s;
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

  set_flags(flags);  // set_flags() already calls fix_flags(), no need to double-call
}

// Z80-specific: 16-bit ADC/SBC (ADC HL,ss / SBC HL,ss)
// Kept for backward compatibility - redirects to appropriate function
void qkz80_reg_set::set_flags_from_diff16(qkz80_big_uint result, qkz80_big_uint val1, qkz80_big_uint val2, qkz80_big_uint carry) {
  // Default to SBC behavior (subtraction)
  set_flags_from_sbc16(result, val1, val2, carry);
}

