// The version of cpmemu and of the qkz80 library, in one place.
//
// cpmemu --version prints it.  src/makefile reads it for LIB_VERSION (the
// library's version and qkz80.pc), and src/CMakeLists.txt reads it for
// project(... VERSION ...), which CPack uses.  A release changes this line.
// LIB_VERSION_MAJOR in src/makefile is a separate number, the ABI version of
// libqkz80, and moves only when that ABI breaks.  In 4.15.0 and earlier the
// version was written twice, in src/makefile and src/CMakeLists.txt, and the
// binary did not carry it at all.
#ifndef CPMEMU_VERSION_H
#define CPMEMU_VERSION_H

#define CPMEMU_VERSION "4.16.0"

#endif
