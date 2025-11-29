#include "qkz80_mem.h"

#include <string.h>

#define MEM_SIZE (0x010000)
qkz80_cpu_mem::qkz80_cpu_mem():dat(0) {
  dat=(qkz80_uint8 *)new char[MEM_SIZE];
  memset(dat,0,MEM_SIZE);
};

qkz80_cpu_mem::~qkz80_cpu_mem() {
  delete dat;
  dat=0;
}

void qkz80_cpu_mem::store_mem(qkz80_uint16 addr, qkz80_uint8 abyte) {
  addr = 0x0FFFF & addr;
  dat[ addr ] = abyte;
}

qkz80_uint8 qkz80_cpu_mem::fetch_mem(qkz80_uint16 addr, bool is_instruction) {
  (void)is_instruction;  // Unused in base class
  return (dat[0x0FFFF & addr]) & 0x0ff;
}

qkz80_uint16 qkz80_cpu_mem::fetch_mem16(qkz80_uint16 addr) {
  return fetch_mem(addr) | (fetch_mem(addr+1) << 8);
}

void qkz80_cpu_mem::store_mem16(qkz80_uint16 addr, qkz80_uint16 aword) {
  store_mem(addr,aword);
  store_mem(addr+1,aword >> 8);
}
