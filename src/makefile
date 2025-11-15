all: qkz80 cpm_emulator

qkz80:	qkz80.cc qkz80.h main.cc qkz80_mem.cc qkz80_errors.cc \
	  qkz80_mem.h qkz80_types.h qkz80_reg_pair.h qkz80_reg_set.h qkz80_reg_set.cc
	g++ -g -O2 -o qkz80 main.cc qkz80.cc qkz80_mem.cc qkz80_errors.cc \
           qkz80_reg_set.cc

cpm_emulator: cpm_emulator.cc qkz80.cc qkz80_mem.cc qkz80_errors.cc qkz80_reg_set.cc \
	  qkz80.h qkz80_mem.h qkz80_types.h qkz80_reg_pair.h qkz80_reg_set.h
	g++ -g -O2 -o cpm_emulator cpm_emulator.cc qkz80.cc qkz80_mem.cc qkz80_errors.cc \
           qkz80_reg_set.cc

test: cpm_emulator
	@echo "Running quick tests..."
	@echo "Simple console (should print ABC):"
	@./cpm_emulator ../tests/simple_con.com 2>&1 | sed -n 's/.*Loaded.*//; s/Program exit.*//; /./p'
	@echo ""
	@echo "DJNZ test (should print 321):"
	@./cpm_emulator ../tests/test_djnz.com 2>&1 | sed -n 's/.*Loaded.*//; s/Program exit.*//; /./p'
	@echo ""
	@echo "Flag test (should print: 94 51 10 3E):"
	@./cpm_emulator ../tests/tflags.com 2>&1 | sed -n 's/.*Loaded.*//; s/Program exit.*//; /./p'
	@echo ""
	@echo "All tests completed!"

clean:	.
	@rm -f qkz80 cpm_emulator *.o *.lst *.ihx *.com *.cdb *.rel *.map *~
