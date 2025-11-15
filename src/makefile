qkz80.x:	qkz80.cc qkz80.h main2.cc qkz80_mem.cc qkz80_errors.cc \
	  qkz80.h qkz80_mem.h qkz80_types.h qkz80_reg_pair.h qkz80_reg_set.h
	g++ -g -O2 -o qkz80 main.cc qkz80.cc qkz80_mem.cc qkz80_errors.cc \
           qkz80_reg_set.cc
	sdasz80 -l -o test1.s
	sdldz80 -i test1.ihx test1.rel
	objcopy --input-target=ihex --output-target=binary test1.ihx test1.com
	sdasz80 -l -o test2.s
	sdldz80 -i test2.ihx test2.rel
	objcopy --input-target=ihex --output-target=binary test2.ihx test2.com


clean:	.
	@rm -f qkz80 *.o *.lst *.ihx *.com *.cdb *.rel *.map *~
