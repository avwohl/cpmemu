all: cpmemu altair_emu cpm_bios

CXXFLAGS = -std=c++11 -Wall -O2 -I.
SOURCES = qkz80.cc  qkz80_errors.cc  qkz80_mem.cc  qkz80_reg_set.cc cpmemu.cc
OBJECTS = $(SOURCES:.cc=.o)
TARGET = cpmemu

# Common object files for all emulators
COMMON_OBJS = qkz80.o qkz80_errors.o qkz80_mem.o qkz80_reg_set.o console_io.o

# Disk image handling
DISK_OBJS = disk_image.o

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)

altair_emu: altair_emu.o $(COMMON_OBJS)
	$(CXX) $(CXXFLAGS) altair_emu.o $(COMMON_OBJS) -o altair_emu

# BIOS-level CP/M emulator (boots from disk images)
cpm_bios: cpm_bios.o $(COMMON_OBJS) $(DISK_OBJS)
	$(CXX) $(CXXFLAGS) cpm_bios.o $(COMMON_OBJS) $(DISK_OBJS) -o cpm_bios

# Pattern rule to compile .cc files into .o object files
# $< is the first dependency (the .cc file)
# $@ is the target (the .o file)
%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: cpmemu
	@echo "Running quick tests..."
	@echo "Simple console (should print ABC):"
	@./cpmemu ../tests/simple_con.com 2>&1 | sed -n 's/.*Loaded.*//; s/Program exit.*//; /./p'
	@echo ""
	@echo "DJNZ test (should print 321):"
	@./cpmemu ../tests/test_djnz.com 2>&1 | sed -n 's/.*Loaded.*//; s/Program exit.*//; /./p'
	@echo ""
	@echo "Flag test (should print: 94 51 10 3E):"
	@./cpmemu ../tests/tflags.com 2>&1 | sed -n 's/.*Loaded.*//; s/Program exit.*//; /./p'
	@echo ""
	@echo "All tests completed!"

clean:
	@rm -f qkz80 cpm_emulator cpmemu altair_emu *.o *.lst *.ihx *.com *.cdb *.rel *.map *~

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
