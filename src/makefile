all: cpmemu

CXXFLAGS = -std=c++11 -Wall -O2 -I.

# For static builds (better portability)
ifdef STATIC
LDFLAGS = -static
endif

SOURCES = qkz80.cc qkz80_errors.cc qkz80_mem.cc qkz80_reg_set.cc cpmemu.cc
OBJECTS = $(SOURCES:.cc=.o)
TARGET = cpmemu

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJECTS) -o $(TARGET)

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
	@rm -f cpmemu *.o *~

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
