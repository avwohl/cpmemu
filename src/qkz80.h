#ifndef QKZ80_H
#define QKZ80_H

#include "qkz80_mem.h"
#include "qkz80_reg_set.h"
#include "qkz80_trace.h"

class qkz80 {
 public:
  enum CPUMode {
    MODE_8080,  // Intel 8080 compatibility mode
    MODE_Z80    // Zilog Z80 mode (default)
  };

  enum {
    regp_BC=0,
    regp_DE=1,
    regp_HL=2,
    regp_SP=3,
    regp_AF=4,
    regp_PC=5,
    regp_IX=6,  // Z80 index register X
    regp_IY=7,  // Z80 index register Y
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
  qkz80_cpu_mem *mem;  // Pointer to memory (allows subclassing)
  qkz80_trace *trace;
  bool qkz80_debug;
  CPUMode cpu_mode;  // 8080 or Z80 mode

  // Cycle counting for interrupt timing
  unsigned long long cycles;  // Total cycles executed

  // Constructor takes a memory object pointer
  qkz80(qkz80_cpu_mem *memory);
  virtual ~qkz80() = default;

  virtual void block_io(qkz80_uint8 opcode) {
    trace->asm_op("ED %02x (block I/O - not implemented)", opcode);
  }
    
  virtual void set_debug(bool new_debug) {
    qkz80_debug=new_debug;
  }

  virtual void set_cpu_mode(CPUMode mode) {
    cpu_mode = mode;
    regs.cpu_mode = (mode == MODE_8080) ? qkz80_reg_set::MODE_8080 : qkz80_reg_set::MODE_Z80;
  }

  virtual CPUMode get_cpu_mode() const {
    return cpu_mode;
  }

  virtual  qkz80_uint8 *get_mem(void) {
    return mem->get_mem();
  }

  virtual void set_trace(qkz80_trace *new_trace) {
    trace=new_trace;
  }

  // I/O port operations - override in subclass to intercept
  virtual void port_out(qkz80_uint8 port, qkz80_uint8 value);
  virtual qkz80_uint8 port_in(qkz80_uint8 port);

  // HALT instruction - override in subclass to customize behavior
  virtual void halt(void);

  // Unimplemented opcode handler - override in subclass to customize behavior
  virtual void unimplemented_opcode(qkz80_uint8 opcode, qkz80_uint16 pc);

  void cpm_setup_memory(void);

  qkz80_uint8 compute_sum_half_carry(qkz80_uint16 rega,
					       qkz80_uint16 dat,
					       qkz80_uint16 carry);

  qkz80_uint8 compute_subtract_half_carry(qkz80_uint16 rega,
						    qkz80_uint16 diff,
						    qkz80_uint16 dat,
						    qkz80_uint16 carry);
  const char *name_condition_code(qkz80_uint8 cond);
  const char *name_reg8(qkz80_uint8 reg8);
  const char *name_reg16(qkz80_uint8 rpair);

  qkz80_uint8 peek_byte_from_opcode_stream(void);
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
  virtual void execute(void);
  virtual void debug_dump_regs(const char* label);

  // Helper functions for Z80 bit operations
  qkz80_uint8 do_rlc(qkz80_uint8 val);
  qkz80_uint8 do_rrc(qkz80_uint8 val);
  qkz80_uint8 do_rl(qkz80_uint8 val);
  qkz80_uint8 do_rr(qkz80_uint8 val);
  qkz80_uint8 do_sla(qkz80_uint8 val);
  qkz80_uint8 do_sra(qkz80_uint8 val);
  qkz80_uint8 do_sll(qkz80_uint8 val);  // undocumented
  qkz80_uint8 do_srl(qkz80_uint8 val);
};

#endif // QKZ80_H
