# 8080 Test Files

Source: https://github.com/superzazu/8080/tree/master/cpu_tests

These test files were copied from superzazu's 8080 emulator repository.

## Test Files

- **8080exer.com** - 8080 instruction exerciser (executable)
- **8080exer.mac** - Source code for 8080exer.com
- **8080exm.com** - 8080 instruction exerciser, modified version (executable)
- **8080exm.mac** - Source code for 8080exm.com
- **8080pre.com** - Preliminary 8080 test (executable)
- **8080pre.mac** - Source code for 8080pre.com

Both 8080exer and 8080exm are modified from Frank D. Cringle's Z80 instruction exerciser,
converted to 8080 by Ian Bartholomew in February 2009.

The tests work by executing all variations of an instruction with different operands
and computing a CRC over the resulting machine state. The CRC is then compared against
the expected value from a real 8080.
