#ifndef X_CPU_MEM
#define X_CPU_MEM 1
#include "qkz80_types.h"

class qkz80_cpu_mem {
  qkz80_uint8 *dat;
 public:
  char *get_mem(void) {
    return (char *)dat;
  }
  qkz80_cpu_mem();
  ~qkz80_cpu_mem();
  qkz80_uint8 fetch_mem(qkz80_uint16 addr);
  void store_mem(qkz80_uint16 addr,qkz80_uint8 abyte);

  qkz80_uint16 fetch_mem16(qkz80_uint16 addr);
  void store_mem16(qkz80_uint16 addr,qkz80_uint16 aword);
};
#endif
