typedef unsigned char qkz80_uint8;
typedef signed char qkz80_int8;
typedef unsigned short int qkz80_uint16;
typedef signed short int qkz80_int16;
// int long enough to have 16bits plus at least one bit more for carry
typedef unsigned int qkz80_big_uint;

void qkz80_global_fatal(const char *fmt,...);
#define qkz80_GET_CLEAN8(xx_a) ((xx_a) & 0x0ff)
#define qkz80_GET_HIGH8(xx_a) qkz80_GET_CLEAN8((xx_a) >> 8)
#define qkz80_MK_INT16(xx_low,xx_high) ((qkz80_uint16(qkz80_GET_CLEAN8(xx_high))<<8) | qkz80_uint16(qkz80_GET_CLEAN8(xx_low)))
