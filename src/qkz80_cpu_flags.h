#ifndef I8080_CPU_FLAGS
#define I8080_CPU_FLAGS 1

  class qkz80_cpu_flags {
  public:
    enum {
      CY=1,         // Carry flag
      UNUSED1=2,    // N flag on Z80 (subtract - set if last operation was subtract)
      P=4,          // Parity/Overflow flag
      UNUSED2=8,    // X flag on Z80 (undocumented)
      AC=16,        // Auxiliary carry / Half carry (H on Z80)
      UNUSED3=32,   // Y flag on Z80 (undocumented)
      Z=64,         // Zero flag
      S=128,        // Sign flag
      // Z80 flag aliases
      N=UNUSED1,    // Subtract flag
      H=AC,         // Half carry
      X=UNUSED2,    // Undocumented X flag
      Y=UNUSED3,    // Undocumented Y flag
    };
  };
#endif
