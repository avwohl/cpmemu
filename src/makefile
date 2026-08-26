all: cpmemu

# -Wextra and -Wshorten-64-to-32 are not decoration: between them they found the
# unused detect_file_mode parameter and two 64-to-32 narrowings, one of which was
# the bug that reported any file of 256 GiB or more to the guest as empty.
# -Wconversion is deliberately NOT here - it adds 125 more, all of them the Z80
# core's intended mod-256 wraps.
#
# -Wshorten-64-to-32 is Clang-only, and GCC does not ignore an unknown -W flag,
# it fails the compile.  Probing for it is not tidiness: hardcoded, it stopped
# every GCC build on the first object file, release.yml's ubuntu-latest and
# ubuntu-24.04-arm jobs included.
SHORTEN_64_TO_32 := $(shell $(CXX) -Wshorten-64-to-32 -x c++ -fsyntax-only /dev/null 2>/dev/null && echo -Wshorten-64-to-32)
CXXFLAGS = -std=c++11 -Wall -Wextra $(SHORTEN_64_TO_32) -O2 -I.
CXXFLAGS_PIC = $(CXXFLAGS) -fPIC

# For static builds (better portability)
ifdef STATIC
LDFLAGS = -static
endif

# Library sources and objects
LIB_SOURCES = qkz80.cc qkz80_errors.cc qkz80_mem.cc qkz80_reg_set.cc
LIB_OBJECTS = $(LIB_SOURCES:.cc=.o)
LIB_OBJECTS_PIC = $(LIB_SOURCES:.cc=.pic.o)

# Platform-specific source
PLATFORM_SOURCE = os/linux/platform.cc
PLATFORM_OBJECT = platform.o

# Library names
LIB_NAME = qkz80
LIB_STATIC = lib$(LIB_NAME).a
LIB_SHARED = lib$(LIB_NAME).so

# Public headers to install
LIB_HEADERS = qkz80.h qkz80_cpu_flags.h qkz80_mem.h qkz80_reg_pair.h \
              qkz80_reg_set.h qkz80_trace.h qkz80_types.h

# Application
APP_SOURCES = cpmemu.cc
APP_OBJECTS = $(APP_SOURCES:.cc=.o)
TARGET = cpmemu

# Build platform object
$(PLATFORM_OBJECT): $(PLATFORM_SOURCE)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Build static library
$(LIB_STATIC): $(LIB_OBJECTS)
	$(AR) rcs $@ $(LIB_OBJECTS)

# Build shared library
$(LIB_SHARED): $(LIB_OBJECTS_PIC)
	$(CXX) -shared -o $@ $(LIB_OBJECTS_PIC)

# Build cpmemu linking against static library
$(TARGET): $(APP_OBJECTS) $(PLATFORM_OBJECT) $(LIB_STATIC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(APP_OBJECTS) $(PLATFORM_OBJECT) $(LIB_STATIC) -o $(TARGET)

# Regular object files
%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Position-independent object files for shared library
%.pic.o: %.cc
	$(CXX) $(CXXFLAGS_PIC) -c $< -o $@

# Convenience targets
lib: $(LIB_STATIC)
shared: $(LIB_SHARED)
libs: $(LIB_STATIC) $(LIB_SHARED)

# Unit tests that link the CPU core directly rather than running a CP/M guest,
# so a failure names the instruction, the operands and the flag bit instead of a
# CRC over sixty thousand cases.  There used to be fifteen ad-hoc programs in
# tests/ for this and no build file mentioned any of them; they had rotted past
# compiling against qkz80's own constructor and every one of them returned 0
# whatever the CPU did.  tests/run_tests.sh builds and runs this too.
UNIT_TESTS = unit_8080

unit_8080: ../tests/unit_8080.cc $(LIB_STATIC)
	$(CXX) $(CXXFLAGS) $< $(LIB_STATIC) -o $@

unit: $(UNIT_TESTS)
	@for t in $(UNIT_TESTS); do ./$$t || exit 1; done

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
	@rm -f cpmemu $(UNIT_TESTS) *.o *.pic.o *.a *.so *.pc *~

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include/qkz80
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig

# Generate pkg-config file
qkz80.pc: qkz80.pc.in
	sed 's|@PREFIX@|$(PREFIX)|g' $< > $@

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -m 755 ../util/cpm_disk.py $(DESTDIR)$(BINDIR)/cpm_disk

install-lib: $(LIB_STATIC) $(LIB_SHARED) qkz80.pc
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -d $(DESTDIR)$(PKGCONFIGDIR)
	install -m 644 $(LIB_STATIC) $(DESTDIR)$(LIBDIR)/
	install -m 755 $(LIB_SHARED) $(DESTDIR)$(LIBDIR)/
	install -m 644 $(LIB_HEADERS) $(DESTDIR)$(INCLUDEDIR)/
	install -m 644 qkz80.pc $(DESTDIR)$(PKGCONFIGDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(BINDIR)/cpm_disk

uninstall-lib:
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_STATIC)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED)
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/qkz80.pc
	rm -rf $(DESTDIR)$(INCLUDEDIR)
