all: cpmemu

CXXFLAGS = -std=c++11 -Wall -O2 -I.
SOURCES = qkz80.cc  qkz80_errors.cc  qkz80_mem.cc  qkz80_reg_set.cc cpmemu.cc
OBJECTS = $(SOURCES:.cc=.o)
TARGET = cpmemu

all: $(TARGET)

# Rule to link object files into the executable
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)

# Pattern rule to compile .cc files into .o object files
# $< is the first dependency (the .cc file)
# $@ is the target (the .o file)
%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

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
	@rm -f qkz80 cpm_emulator cpmemu *.o *.lst *.ihx *.com *.cdb *.rel *.map *~
