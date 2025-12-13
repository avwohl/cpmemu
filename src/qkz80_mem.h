#ifndef X_CPU_MEM
#define X_CPU_MEM 1
#include "qkz80_types.h"

class qkz80_cpu_mem {
  qkz80_uint8 *dat;
 public:
  virtual qkz80_uint8 *get_mem(void) {
    return dat;
  }
  qkz80_cpu_mem();
  virtual ~qkz80_cpu_mem();
  // fetch_mem: is_instruction=true when fetching opcode bytes
  virtual qkz80_uint8 fetch_mem(qkz80_uint16 addr, bool is_instruction = false);
  virtual void store_mem(qkz80_uint16 addr, qkz80_uint8 abyte);

  virtual qkz80_uint16 fetch_mem16(qkz80_uint16 addr);
  virtual void store_mem16(qkz80_uint16 addr, qkz80_uint16 aword);
};
#endif
