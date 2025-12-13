#ifndef I8080_REG_PAIR
#define I8080_REG_PAIR 1
// two 8bit regs
// settable as a 16bit word
// does not depend on compiler/runtime big/little end
// binary bits not portable (as in don't write this raw data to a binary file)

#include "qkz80_types.h"

class qkz80_reg_pair {
 protected:
  qkz80_uint16 dat;
 public:
  qkz80_reg_pair():
    dat(0) {
  }
  qkz80_reg_pair(const qkz80_reg_pair& rp): dat(rp.dat) {
  }
  qkz80_uint8 get_low(void) const {
    return qkz80_GET_CLEAN8(dat);
  }
  qkz80_uint8 get_high(void) const {
    return qkz80_GET_HIGH8(dat);
  }
  qkz80_uint16 get_pair16(void) const {
    return dat;
  }
  void set_pair16(qkz80_uint16 a) {
    dat=a;
  }
  void set_low(qkz80_uint8 a) {
    set_pair16(qkz80_MK_INT16(a,get_high()));
  }
  void set_high(qkz80_uint8 a) {
    set_pair16(qkz80_MK_INT16(get_low(),a));
  }
  void set_pair(qkz80_uint8 a,qkz80_uint8 b) {
    set_low(a);
    set_high(b);
  }
};
#endif
