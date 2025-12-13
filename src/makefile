all: cpmemu

CXXFLAGS = -std=c++11 -Wall -O2 -I.
CXXFLAGS_PIC = $(CXXFLAGS) -fPIC

# For static builds (better portability)
ifdef STATIC
LDFLAGS = -static
endif

# Library sources and objects
LIB_SOURCES = qkz80.cc qkz80_errors.cc qkz80_mem.cc qkz80_reg_set.cc
LIB_OBJECTS = $(LIB_SOURCES:.cc=.o)
LIB_OBJECTS_PIC = $(LIB_SOURCES:.cc=.pic.o)

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

# Build static library
$(LIB_STATIC): $(LIB_OBJECTS)
	$(AR) rcs $@ $(LIB_OBJECTS)

# Build shared library
$(LIB_SHARED): $(LIB_OBJECTS_PIC)
	$(CXX) -shared -o $@ $(LIB_OBJECTS_PIC)

# Build cpmemu linking against static library
$(TARGET): $(APP_OBJECTS) $(LIB_STATIC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(APP_OBJECTS) $(LIB_STATIC) -o $(TARGET)

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
	@rm -f cpmemu *.o *.pic.o *.a *.so *~

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include/qkz80

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

install-lib: $(LIB_STATIC) $(LIB_SHARED)
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -m 644 $(LIB_STATIC) $(DESTDIR)$(LIBDIR)/
	install -m 755 $(LIB_SHARED) $(DESTDIR)$(LIBDIR)/
	install -m 644 $(LIB_HEADERS) $(DESTDIR)$(INCLUDEDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall-lib:
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_STATIC)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED)
	rm -rf $(DESTDIR)$(INCLUDEDIR)
