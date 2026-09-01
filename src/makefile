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

# Which host this is.  The shared library, the static link and the pkg-config
# file all differ between the two, and each difference below says why.
UNAME_S := $(shell uname -s)

# For static builds (better portability)
#
# There is no such build on macOS and pretending otherwise is worse than
# refusing.  The SDK ships no crt0.o, no libc.a, no libSystem.a and no
# libc++.a, so -static stops at "ld: library 'crt0.o' not found" - measured
# here on macOS 27 arm64 with Apple clang 21.  -static-libstdc++ is accepted
# and silently ignored by the same compiler, so quietly dropping the flag
# would hand whoever asked for STATIC=1 a dynamically linked binary they
# believe is static, and they would find out from a user's machine.
#
# What does buy portability on macOS is MACOSX_DEPLOYMENT_TARGET, which sets
# how old a system the dynamic binary will still run on, so the message names
# it.  Linux is untouched: -static there links as it always did.
ifdef STATIC
ifeq ($(UNAME_S),Darwin)
$(error STATIC=1 cannot work on macOS: the SDK has no crt0.o and no static \
libc or libc++, and -static-libstdc++ is silently ignored, so there is no \
static build to be had here.  Build without STATIC; to control how old a macOS \
the binary will still run on, set MACOSX_DEPLOYMENT_TARGET, for example \
`make MACOSX_DEPLOYMENT_TARGET=12.0`)
endif
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
#
# Darwin gets a Mach-O dylib under a Mach-O name.  Writing one as libqkz80.so
# was not merely a cosmetic mismatch: Apple's linker does search for a .so and
# prefers it over the libqkz80.a sitting beside it in the same -L directory
# (verified with -Wl,-t), and the file it found carried the bare install name
# "libqkz80.so", which dyld cannot resolve from any directory it looks in.  A
# consumer therefore linked with exit 0 and aborted at exec with
# "Library not loaded: libqkz80.so", exit 134.
#
# Versioned name plus an unversioned symlink, as every other dylib on the
# system is laid out: -lqkz80 finds the symlink, the install name inside the
# file names the versioned one, so what a consumer records is the version it
# was built against.  Only the major number is in the name - that is what an
# ABI break moves - and the full version rides along as the current version.
LIB_NAME = qkz80
LIB_STATIC = lib$(LIB_NAME).a
# One place for the version on this side of the build.  src/CMakeLists.txt
# carries its own copy in project(... VERSION ...) and the two have to be moved
# together; qkz80.pc no longer does, it is filled in from here.
LIB_VERSION = 4.7.0
LIB_VERSION_MAJOR = 4
ifeq ($(UNAME_S),Darwin)
LIB_SHARED = lib$(LIB_NAME).$(LIB_VERSION_MAJOR).dylib
LIB_SHARED_LINK = lib$(LIB_NAME).dylib
else
LIB_SHARED = lib$(LIB_NAME).so
LIB_SHARED_LINK =
endif

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
ifeq ($(UNAME_S),Darwin)
# -install_name is the path dyld will be told to load, and it has to be an
# absolute one.  It is stamped here with the LIBDIR this build knows about and
# stamped again by install-lib, because the link happens before a PREFIX given
# only on the install command line is known; -headerpad_max_install_names is
# what leaves install_name_tool room to write a longer path in afterwards.
$(LIB_SHARED): $(LIB_OBJECTS_PIC)
	$(CXX) -dynamiclib -install_name $(LIBDIR)/$(LIB_SHARED) \
		-compatibility_version $(LIB_VERSION_MAJOR).0.0 \
		-current_version $(LIB_VERSION) \
		-Wl,-headerpad_max_install_names -o $@ $(LIB_OBJECTS_PIC)

$(LIB_SHARED_LINK): $(LIB_SHARED)
	ln -sf $(LIB_SHARED) $@
else
$(LIB_SHARED): $(LIB_OBJECTS_PIC)
	$(CXX) -shared -o $@ $(LIB_OBJECTS_PIC)
endif

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
shared: $(LIB_SHARED) $(LIB_SHARED_LINK)
libs: $(LIB_STATIC) $(LIB_SHARED) $(LIB_SHARED_LINK)

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
	@rm -f cpmemu $(UNIT_TESTS) *.o *.pic.o *.a *.so *.dylib *.pc *~

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include/qkz80
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig

# The C++ runtime a C driver has to be told about.  Everything in the archive
# is C++, so a final link done with cc rather than c++ has no C++ runtime under
# it and fails on "vtable for __cxxabiv1::__class_type_info", operator
# new/delete and ___gxx_personality_v0.  That is exactly what Libs.private is
# for, and the .pc had none, so `pkg-config --static --libs qkz80` produced a
# link that could not work.  Only --static reads it, so the dynamic link is
# unchanged on both platforms.
ifeq ($(UNAME_S),Darwin)
LIBS_PRIVATE = -lc++
else
LIBS_PRIVATE = -lstdc++
endif

# Generate pkg-config file
#
# The directories come from the variables the install itself uses.  Hardcoded
# in qkz80.pc.in as ${prefix}/lib and ${prefix}/include/qkz80, they named the
# place the files are not as soon as LIBDIR or INCLUDEDIR was moved.
#
# Phony because the output depends on PREFIX/LIBDIR/INCLUDEDIR and not only on
# qkz80.pc.in: an up-to-date qkz80.pc left over from an install into one prefix
# would otherwise be installed unchanged into the next one.
.PHONY: qkz80.pc
qkz80.pc: qkz80.pc.in
	sed -e 's|@PREFIX@|$(PREFIX)|g' \
	    -e 's|@LIBDIR@|$(LIBDIR)|g' \
	    -e 's|@INCLUDEDIR@|$(INCLUDEDIR)|g' \
	    -e 's|@LIBS_PRIVATE@|$(LIBS_PRIVATE)|g' \
	    -e 's|@VERSION@|$(LIB_VERSION)|g' $< > $@

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -m 755 ../util/cpm_disk.py $(DESTDIR)$(BINDIR)/cpm_disk

install-lib: $(LIB_STATIC) $(LIB_SHARED) $(LIB_SHARED_LINK) qkz80.pc
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -d $(DESTDIR)$(PKGCONFIGDIR)
	install -m 644 $(LIB_STATIC) $(DESTDIR)$(LIBDIR)/
	install -m 755 $(LIB_SHARED) $(DESTDIR)$(LIBDIR)/
ifeq ($(UNAME_S),Darwin)
# The install name written at link time was the LIBDIR the build knew about,
# which is not the one an install with PREFIX on the command line is going to.
# Re-stamp it here, where the real destination is known, and put the
# unversioned symlink beside it so -lqkz80 resolves.  DESTDIR is a staging
# root and deliberately not part of the recorded path.
	install_name_tool -id $(LIBDIR)/$(LIB_SHARED) $(DESTDIR)$(LIBDIR)/$(LIB_SHARED)
	ln -sf $(LIB_SHARED) $(DESTDIR)$(LIBDIR)/$(LIB_SHARED_LINK)
endif
	install -m 644 $(LIB_HEADERS) $(DESTDIR)$(INCLUDEDIR)/
	install -m 644 qkz80.pc $(DESTDIR)$(PKGCONFIGDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(BINDIR)/cpm_disk

uninstall-lib:
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_STATIC)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED)
ifeq ($(UNAME_S),Darwin)
	rm -f $(DESTDIR)$(LIBDIR)/$(LIB_SHARED_LINK)
endif
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/qkz80.pc
	rm -rf $(DESTDIR)$(INCLUDEDIR)
