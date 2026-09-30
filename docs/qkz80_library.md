# The qkz80 library

The CPU core is a library as well as being linked into `cpmemu`. The `.deb` and
the `.rpm` install it beside the emulator; there is no separate `-dev` package.

```
/usr/lib/libqkz80.a
/usr/lib/libqkz80.so
/usr/lib/pkgconfig/qkz80.pc
/usr/include/qkz80/        seven headers, qkz80.h first
```

The macOS tarball carries `libqkz80.a` and the headers and no dylib: a dylib's
install name is an absolute path, so one unpacked wherever the user likes would
be a library dyld cannot find.

From source, `make -C src libs` builds both and `sudo make -C src install-lib`
installs them, the headers and `qkz80.pc` under `PREFIX` (`/usr/local`);
`LIBDIR`, `INCLUDEDIR`, `PKGCONFIGDIR` and `DESTDIR` are honoured, and
`qkz80.pc` is generated from whichever of them the install used.
`make -C src uninstall-lib` removes them.

```bash
c++ -std=c++11 $(pkg-config --cflags qkz80) prog.cc $(pkg-config --libs qkz80)
c++ -std=c++11 $(pkg-config --cflags qkz80) prog.cc $(pkg-config --static --libs qkz80)
```

`--static` adds the `-lstdc++` (`-lc++` on macOS) that `Libs.private` names,
because everything in the archive is C++ and a final link driven by `cc` fails
without it.

A minimal consumer:

```c++
#include "qkz80.h"

qkz80_cpu_mem mem;                    // plain 64K; subclass it for banking or I/O
qkz80 cpu(&mem);
cpu.set_cpu_mode(qkz80::MODE_8080);   // MODE_Z80 is the default
cpu.get_mem()[0x100] = 0x76;          // HALT
cpu.set_reg16(0x100, qkz80::regp_PC);
cpu.execute();                        // one instruction
```

`set_trace()` takes a `qkz80_trace` subclass. `int_pending`, `nmi_pending` and
`int_vector` are what `check_interrupts()` reads, and the caller has to call it
at instruction boundaries - `execute()` runs one instruction and delivers
nothing by itself. `cycles` counts a flat five per instruction, which is for
interrupt timing and is not a cycle-accurate figure. See
[qkz80_interrupts.md](qkz80_interrupts.md).

Compiling the core with `-DQKZ80_NO_TRACE` compiles every trace call out of the
instruction decoder and roughly halves its text. It has to be defined when
`qkz80.cc` itself is compiled, so the shipped libraries are built with tracing
in.

## Who else compiles qkz80

`src/qkz80*.{cc,h}` is the CPU core, and four sibling projects consume it out of
a neighbouring working tree rather than depending on a cpmemu *release* - three
by compiling the sources, and one linking the archive for the `.cc` files while
compiling the headers like everybody else. An edit to `qkz80.cc` lands in three
of them on their next build and in romwbw_emu once `libqkz80.a` is next built;
an edit to a header lands in all four on their next compile. No notification,
and no version gate:

- **ioscpm** - 11 symlinks in `iOSCPM/Core/` pointing at
  `../../../cpmemu/src/qkz80*`. Built as Objective-C++ for iOS at
  `c++17`/`gnu++20`; its `Tests/run_tests.sh` compiles the same files with
  `-std=c++11 -Wall`.
- **z80cpmw** - `z80cpmw/z80cpmw.vcxproj` compiles the four
  `$(SolutionDir)..\cpmemu\src\qkz80*.cc` in place, MSVC at `/W3` and
  `/std:c++17`, with C4244 disabled on those files.
- **cpmdroid** - `app/src/main/cpp/CMakeLists.txt` compiles the same four
  `${CPMEMU_SRC}/qkz80*.cc` in place, under the Android NDK.
- **romwbw_emu** - the odd one out for the `.cc` files only: it links
  `libqkz80.a` rather than compiling them, so an edit to `qkz80.cc` reaches it
  once that archive is rebuilt. A header edit does not wait for the archive.
  `QKZ80_CFLAGS` puts `-I` on a qkz80 source tree and `qkz80_reg_pair.h`'s
  inline `set_low()`/`set_high()` bodies compile into every one of its object
  files, which is why its own build reports `qkz80_reg_pair.h:32` and `:35`. It
  compiles at `-std=c++11 -Wall`, plus `-Wimplicit-int-conversion` and
  `-Wshorten-64-to-32` where the compiler accepts them - both probed, because
  GCC fails on an unknown `-W` rather than ignoring it. `src/makefile` resolves
  `QKZ80_CFLAGS` and `QKZ80_LIBS` four ways each - the caller, pkg-config, the
  sibling checkout, `/usr/local` - and `make qkz80-source` prints which it
  took. Only the caller and the sibling checkout reach this working tree; the
  other two name an install prefix, and an installed qkz80 - the `.pc` file
  pkg-config reads included - waits for `make install-lib`.

All four follow this repository's `main` rather than any tag, so a commit
pushed here is what their next build takes. **Check them before changing
qkz80's public surface.**
