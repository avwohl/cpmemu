#ifndef I8080_TRACE
#define I8080_TRACE 1

class qkz80_trace {
 public:
  virtual void comment(const char *fmt,...) {
  }

  virtual void asm_op(const char *fmt,...) {
  }
  
  virtual void flush(void) {
  }

  virtual void fetch(qkz80_uint8 opstream_byte,qkz80_uint16 pc) {
  }

  virtual void add_reg8(qkz80_uint8 areg) {
  }

  virtual void add_reg16(qkz80_uint16 areg) {
  }
  
};

#endif

