#include "qkz80_mem.h"
#include "qkz80_reg_set.h"
#include "qkz80_trace.h"

class qkz80 {
 public:
  enum {
    regp_BC=0,
    regp_DE=1,
    regp_HL=2,
    regp_SP=3,
    regp_AF=4,
    regp_PC=5,
  };

  enum {
    reg_B=0,
    reg_C=1,
    reg_D=2,
    reg_E=3,
    reg_H=4,
    reg_L=5,
    reg_M=6,
    reg_A=7,
    reg_FLAGS=8,
  };

  qkz80_reg_set regs;
  qkz80_cpu_mem mem;
  qkz80_trace *trace;
  bool qkz80_debug;

  void set_debug(bool new_debug) {
    qkz80_debug=new_debug;
  }
  
  char *get_mem(void) {
    return mem.get_mem();
  }

  void set_trace(qkz80_trace *new_trace) {
    trace=new_trace;
  }
  
  qkz80();

  void cpm_setup_memory(void);

  qkz80_uint8 compute_sum_half_carry(qkz80_uint16 rega,
					       qkz80_uint16 dat,
					       qkz80_uint16 carry);

  qkz80_uint8 compute_subtract_half_carry(qkz80_uint16 rega,
						    qkz80_uint16 diff,
						    qkz80_uint16 dat,
						    qkz80_uint16 carry);

  void halt(void);
  const char *name_condition_code(qkz80_uint8 cond);
  const char *name_reg8(qkz80_uint8 reg8);
  const char *name_reg16(qkz80_uint8 rpair);

  qkz80_uint8 pull_byte_from_opcode_stream(void);
  qkz80_uint16 pull_word_from_opcode_stream(void);
  void setup_parity(void);
  void push_word(qkz80_uint16 aword); 
  qkz80_uint16 read_word(qkz80_uint16 addr);
  qkz80_uint16 pop_word(void);

  qkz80_uint8 fetch_carry_as_int(void);
  qkz80_uint8 get_reg8(qkz80_uint8 a);
  qkz80_uint16 get_reg16(qkz80_uint8 a);
  void set_reg16(qkz80_uint16 a,qkz80_uint8 rp);
  void set_reg8(qkz80_uint8 dat,qkz80_uint8 rnum);
  void set_A(qkz80_uint8 dat) {
    set_reg8(dat,reg_A);
  }

  void write_2_bytes(qkz80_uint16 store_me,qkz80_uint16 location);
  void execute(void);
};

