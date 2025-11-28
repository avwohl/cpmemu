# Makefile for CP/M WebAssembly build

EMCC = emcc
CFLAGS = -O2 -std=c++11
LDFLAGS = -s WASM=1 \
          -s EXPORTED_FUNCTIONS='["_main","_cpm_key_input","_cpm_load_system","_cpm_load_disk","_cpm_start","_cpm_stop","_cpm_get_disk_data","_cpm_get_disk_size","_cpm_autostart","_malloc","_free"]' \
          -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS"]' \
          -s ALLOW_MEMORY_GROWTH=1 \
          -s INITIAL_MEMORY=16777216 \
          --preload-file ../cpm22.sys@/cpm22.sys \
          --preload-file ../drivea.img@/drivea

SRCS = cpm_web.cc \
       ../src/qkz80.cc \
       ../src/qkz80_mem.cc \
       ../src/qkz80_reg_set.cc \
       ../src/qkz80_errors.cc

all: cpm.js

cpm.js: $(SRCS) ../cpm22.sys ../drivea.img
	$(EMCC) $(CFLAGS) $(LDFLAGS) -o cpm.js $(SRCS)

clean:
	rm -f cpm.js cpm.wasm cpm.data

serve: cpm.js
	python3 -m http.server 8080

.PHONY: all clean serve
