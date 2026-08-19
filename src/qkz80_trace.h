#ifndef QKZ80_TRACE
#define QKZ80_TRACE 1

// QKZ80_TRACE_CALL - wrapper for every "trace->..." call in the CPU core.
//
// Write   QKZ80_TRACE_CALL(asm_op("nop"));
// instead of   trace->asm_op("nop");
// The macro takes the part of the call that follows "trace->".
//
// Tracing is compiled in by default, so nothing changes for existing users.
// Define QKZ80_NO_TRACE on the compiler command line (-DQKZ80_NO_TRACE) to
// compile the trace calls out of the instruction decoder.  Embedders that
// never call set_trace() - the Android port, for example - otherwise pay a
// virtual call for every traced event.
//
// The expansion is always a single void expression.  That keeps it legal in
// expression position and, just as important, keeps it a complete statement
// when it is the unbraced body of an if or an else:
//     if (cond) QKZ80_TRACE_CALL(asm_op("a"));
//     else      QKZ80_TRACE_CALL(asm_op("b"));
// still parses the same way with tracing on or off.
//
// With QKZ80_NO_TRACE the call sits in the dead arm of a constant
// conditional.  The arguments are still parsed and type checked, so no
// variable or helper becomes unused, but they are never evaluated and the
// compiler drops the whole arm.
#ifdef QKZ80_NO_TRACE
#define QKZ80_TRACE_CALL(...) ((void)(false ? (void)((trace)->__VA_ARGS__) : (void)0))
#else
#define QKZ80_TRACE_CALL(...) ((void)((trace)->__VA_ARGS__))
#endif

class qkz80_trace {
 public:
  virtual void comment(const char *fmt,...) {
    (void)fmt;
  }

  virtual void asm_op(const char *fmt,...) {
    (void)fmt;
  }
  
  virtual void flush(void) {
  }

  virtual void fetch(qkz80_uint8 opstream_byte,qkz80_uint16 pc) {
    (void)opstream_byte;
    (void)pc;
  }

  virtual void add_reg8(qkz80_uint8 areg) {
    (void)areg;
  }

  virtual void add_reg16(qkz80_uint16 areg) {
    (void)areg;
  }
  
};

#endif

