# The GDB stub and the backtrace

cpmemu can wait for a debugger before the program starts. The debugger talks
the GDB remote serial protocol (RSP) over a TCP socket on 127.0.0.1. The
program keeps the console and all of its 64K. The debugger keeps its symbols
and source files on the host. The design is in
[debugger_plan.md](debugger_plan.md).

## What you need

- **cpmemu 4.11.0 or newer.** An older release has no `--gdb`.
- **um80 0.4.0 or newer:** `pip install "um80>=0.4.0"`. The package holds
  `um80` and `ul80` with `--debug-info`, udb80, `dbg2elf` and the GDB Python
  scripts.
- **A compiler that writes the debug markers**, for a C program:
  `pip install "uc80>=0.8.0"`, then compile with `uc80 -g`. An assembler
  program needs only um80.
- **GDB with the Z80 target**, but only if you use GDB. udb80 needs no GDB.
  See [Connect GDB](#connect-gdb).

The source-level steps (the ELF file, the unwinder, variables) are in the
um80_and_friends documents
[gdb.md](https://github.com/avwohl/um80_and_friends/blob/main/docs/gdb.md) and
[udb80.md](https://github.com/avwohl/um80_and_friends/blob/main/docs/udb80.md).

A check on 2026-11-05 started from a clean Python 3.12 venv with uc80 0.8.0
and um80 0.4.0 from PyPI. The cpmemu was first a build of this tree, then
the binary of the 4.11.0 macOS release tarball. A recursive C
program went through `uc80 -g`, `um80 --debug-info`, `ul80 --debug-info` and
`dbg2elf`. In udb80 and in GDB 18.1, `break rs.c:4` stopped in the base case.
`bt` showed 5 frames of `sum` and the frame of `main`.

## Start

```
cpmemu --gdb prog.com
cpmemu --gdb=4000 prog.com
cpmemu --gdb=0 --gdb-port-file=port.txt prog.com
cpmemu --gdb --gdb-listen=0.0.0.0 prog.com
```

`--gdb` listens on 127.0.0.1 port 1234. `--gdb=PORT` chooses the port. Port 0
lets the system choose, and `--gdb-port-file=FILE` writes the chosen port to
FILE. A test can then start many cpmemu processes at the same time.
`--gdb-listen=ADDR` listens on a different IPv4 address. See
[Debug from another machine](#debug-from-another-machine).

cpmemu prints `GDB stub listening on 127.0.0.1:PORT` and waits. When the
debugger connects, cpmemu stops before the instruction at 0100H. While cpmemu
waits, ^C on the console ends cpmemu. Other keys typed on a terminal during
the wait are lost. Input that is not a terminal (a pipe or a file) is not read
during the wait, so the program gets all of that input after GDB connects.

## Connect GDB

GDB has had a Z80 target since GDB 11.1. A GDB that is built with
`--enable-targets=all` has the target. The Homebrew formula `gdb` uses that
option.

A GDB 18.1 built with the configure options of the formula (2026-11-05,
macOS 27 arm64) listed z80 in `set architecture` and ran the session above.
The Homebrew bottle itself was not installed for that test. Other packages
were not checked.

To build GDB from source, use this option:

```
configure --target=z80-unknown-elf
make all-gdb
```

The full build steps are in
[gdb.md](https://github.com/avwohl/um80_and_friends/blob/main/docs/gdb.md#build-gdb-181-for-the-z80).

Then connect:

```
gdb
(gdb) set architecture z80
(gdb) target remote :1234
(gdb) stepi
(gdb) info registers
```

With a program file that has DWARF line tables, GDB shows source lines. Start
GDB with `gdb prog.elf` or use the `file` command. The `break FILE:LINE`,
`continue`, `step`, `next`, `watch`, `bt` and `info registers` commands work.
GDB writes nothing into guest memory, because the stub keeps the breakpoints.

A local run on 2026-11-04 used GDB 18.1 and the ELF file of the
`dbg2elf_proto` prototype in um80_and_friends. `break sumto.c:14`,
`continue`, `info registers`, `step`, `next`, `print total`, `bt`,
`watch total`, `tbreak` and `stepi` worked, and `continue` ended with
`exited normally`.

## Debug from another machine

By default the stub listens on 127.0.0.1 only, and a debugger on a different
machine cannot connect. The stub has no password, and the debugger can write
guest memory. A guest can write host files through BDOS. Thus a stub on the
network lets any user of the network write files as you.

There are two ways. An SSH tunnel keeps the stub on 127.0.0.1 and is the
safer way. `--gdb-listen` puts the stub on the network.

### An SSH tunnel

On the machine with the debugger:

```
ssh -N -L 1234:127.0.0.1:1234 USER@EMULATOR-HOST
```

On the emulator machine, start `cpmemu --gdb prog.com`. Then connect the
debugger to port 1234 on its own machine:

```
gdb prog.elf
(gdb) target remote :1234
```

```
udb80 prog.dbg --connect 127.0.0.1:1234
```

Keep `prog.dbg`, `prog.elf`, `prog.com` and the source files on the machine
with the debugger. udb80 and dbg2elf check the program image against
`prog.dbg`. The `ssh` client of Windows 10 and later takes the same
command.

Measured on 2026-11-05 (macOS 27 arm64): the cpmemu 4.11.0 release listened
on 127.0.0.1:4711, and a connection to the LAN address on port 4711 was
refused. `ssh -L 5711:127.0.0.1:4711` to the LAN address of the same Mac
forwarded the port. GDB 18.1 on port 5711 stopped at `break rs.c:4`, and
`bt` showed 6 frames. A tunnel between two different machines was not
measured.

### --gdb-listen

`--gdb-listen=ADDR` makes the stub listen on the IPv4 address ADDR.
`0.0.0.0` is every interface. The address of one interface, for example
`192.168.1.5`, is that interface only. A name such as `localhost` is not
accepted. The option needs `--gdb`.

```
cpmemu --gdb --gdb-listen=0.0.0.0 prog.com
```

On the machine with the debugger:

```
gdb prog.elf
(gdb) target remote EMULATOR-HOST:1234
```

```
udb80 prog.dbg --connect EMULATOR-HOST:1234
```

For an address outside 127.0.0.0/8, cpmemu prints a warning before
`GDB stub listening on ADDR:PORT`. Use the option only on a network that you
trust, and only while you debug. A firewall can also block the port.

`tests/gdb_stub_test.py` checks the option:

- A bad address is refused.
- Without the option, the stub refuses the LAN address of the host.
- `0.0.0.0` takes a client on 127.0.0.1 and a client on the LAN address.
- The address of one interface refuses 127.0.0.1.

Measured on 2026-11-05 (macOS 27 arm64): with `--gdb-listen=0.0.0.0`, GDB
18.1 on the same Mac connected to the LAN address 10.3.0.132 and showed a
6-frame `bt`. A debugger on a different machine was not measured.

## What the stub does

- The stub keeps the breakpoints (`Z0` and `Z1`) in a table. It tests the table
  before each instruction. It never writes a breakpoint byte into guest memory.
  The stop reply is `T05swbreak:;` when the debugger said `swbreak+` in
  `qSupported`, as GDB and udb80 do. Otherwise the reply is `T05`.
- A target can add a rule break (`Stub::set_rule_break`), for example the
  `wait-program` rule of romwbw_emu. No debugger set that breakpoint, so its
  stop reply is `T05`, with no `swbreak`. GDB then reports a SIGTRAP stop.
  With `swbreak`, GDB would continue past a breakpoint that GDB does not
  know. udb80 0.4.2 and later take both replies.
- A step at a BDOS or BIOS trap address (0FD00H, 0FF00H to 0FF1FH) runs the
  trap once and stops at the return address.
- `vCont` with range stepping (`r`) steps until PC leaves the range.
- With `--int-cycles`, a step does not go into the interrupt routine. The stub
  runs the routine to its end, then steps the instruction that it interrupted.
- A HLT that no interrupt can end stops the program with `T05`. Without a
  debugger the same HLT ends the run. A `continue` after the stop runs on
  past the HLT.
- ^C from the debugger (byte 03H) stops the program with `T02`. The stub looks
  for the byte every 4096 instructions. It also looks while the program waits
  for a key, so ^C reaches a program that waits in BDOS 1, BDOS 10 or BIOS
  CONIN. PC stays at the BDOS trap, and a `continue` starts the call again.
  A BDOS 10 call that already took some keys loses them.
- `Z2`, `Z3` and `Z4` set write, read and access watchpoints. The stop reply
  is `T05watch:ADDR;`, `T05rwatch:ADDR;` or `T05awatch:ADDR;`. The stub sees
  the reads and writes that the CPU makes. It does not see the accesses that
  BDOS and BIOS calls make for the program, such as a disk read into the DMA
  buffer.
- `QCatchSyscalls` stops the program at a BDOS call. The stop replies are
  `T05syscall_entry:N;` before the call and `T05syscall_return:N;` after
  the call. N is the BDOS function number in hex. The stub takes
  `QCatchSyscalls:1` for all calls and `QCatchSyscalls:1;N;N` for a list.
  GDB 18.1 cannot use the packet on a Z80: `catch syscall 2` answers `The
  feature 'catch syscall' is not supported on this architecture yet`
  (measured). A client that sends the packet itself can use the packet.
- An address at 10000H or above gets `E01`. A read that starts below 10000H
  and runs past it returns the bytes up to 0FFFFH.
- The stub serves `target.xml` with GDB's 13 Z80 registers: af, bc, de, hl, sp,
  pc, ix, iy, af', bc', de', hl' and ir. Each register is 16 bits. The `g`
  reply is 26 bytes. GDB's own `z80-cpu.xml` gives pc 32 bits, but GDB 18.1
  sizes pc by the Z80 pointer size and refuses a 28-byte `g` reply.
- A program that ends sends `W` and the exit status. A program that a signal
  ends sends `X` and the signal number. `k` ends cpmemu with exit status 1
  and sends no reply. `D` lets the program run on with no debugger.
- The stub answers an empty packet to each packet that it does not know.

A breakpoint at the first address (0100H) is not reported by the first `c`,
because the first instruction always runs. GDB steps over a breakpoint at the
current address itself. A client that does not do this misses the breakpoint.

On Windows the stub looks at the console and the socket in slices of 10 ms.
The slice is the time that a ^C from the debugger needs to reach a program that
waits for a key.

`vCont?` answers `vCont;c;C;s;S;r`. GDB 18.1 turns `vCont` off when `C` is
missing from the list.

## monitor bt

`monitor bt` in GDB prints the shadow call stack. Without GDB, send the
`qRcmd` packet with the hex-encoded text `bt`. Frame 0 is PC. Each other
frame is a return address, innermost first:

```
#0  0x0121
#1  0x0111  [shadow call]
```

The shadow call stack is a list of the calls that the CPU took and has not
returned from. The stub keeps the list only when a debugger is connected or
you give `--backtrace`. The rules:

1. A CALL, RST or interrupt that the CPU takes pushes an entry. The entry
   holds the return address, the stack address of the return address (the
   slot) and the kind.
2. A RET or RETI that the CPU takes reads SP first. The stub finds the
   innermost entry whose slot is SP and drops that entry and all entries
   above it. If no slot is SP, the stub finds the innermost entry with a
   slot below SP and the return address that the RET went to. A routine that
   removes its own arguments, as an uplm80 REENTRANT epilogue does, moves
   the return address up before the RET. If the stub finds no entry, the
   stub drops nothing and marks the stack "out of step".
3. A BDOS or BIOS trap returns with no RET instruction. The stub applies rule
   2 to the return of the trap.
4. SP that only rises does not drop entries.
5. The stub keeps 4096 entries and drops the oldest first.

A tail call (`jp`) hides one frame. A routine that returns with `jp (hl)`
keeps its entry until the caller returns. A task switch can make the stack
"out of step".

## --backtrace

`--backtrace` keeps the shadow call stack with no debugger. When the run ends
badly, cpmemu prints the stack to standard error as hex addresses:

```
Backtrace (shadow call stack, return addresses in hex):
#0  0x0121
#1  0x0111  [shadow call]
```

A run ends badly after a fatal BDOS or BIOS error, a HLT that nothing can end,
the instruction limit, five ^C or a signal. A normal end prints nothing. The
program is slower with `--backtrace`: a loop of 9.0e8 instructions took 15.0
seconds, and 9.9 seconds without the option.

## What the tests check

`tests/gdb_stub_test.py` checks the `W00` packet, the `W01` packet (a BIOS
disk error) and the `X0F` packet (SIGTERM). These exit paths are checked only
by reading the code: a write to a read-only drive, the instruction limit, five
^C, end of input, and `qkz80_global_fatal`. All of them go through the one
helper in `src/cpmemu.cc`.

## Speed

Without `--gdb` and `--backtrace` the run loop is the one that cpmemu always
had. It tests nothing for a debugger. A second loop runs when either option
is on.

## Limits

- The stub serves one debugger on one thread.
- The stub listens on 127.0.0.1 only.
- Memory above 0FFFFH does not exist, so GDB cannot see a banked address.
  romwbw_emu has banked memory and shows one bank at a time: see its
  `docs/GDB.md`.
- The stub does not offer `QStartNoAckMode`, `hwbreak` or no-stop mode.
