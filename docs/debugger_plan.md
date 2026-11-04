# Plan: source debugging and backtrace

This plan adds two features to the Z80 tool family:

- **Source debugging.** Set a breakpoint on a line of C, PL/M, Ada,
  Cowgol, BASIC or assembler source. Step by source line. See the source
  line for any address.
- **Backtrace.** Show the chain of calls that led to the current address,
  with a routine name and a source line for each frame.

The owner listed eleven projects: uc80, um80_and_friends, uc_core, uplm80,
upeepz80, uada80, z80cpmw, ioscpm, cpmdroid, cpmemu and z80fpga. The plan
adds four more: ucow (a fourth compiler), romwbw_emu (the RomWBW
emulator that the three GUI clients share code with), mbasic (for BASIC
lines) and 80mcp (a later user of the debug stub). cpmemu holds the plan
because cpmemu gets the first runtime support.

## Where the facts come from

The facts come from a survey on 2026-11-04 and a review of a first draft
on the same day. The survey read these commits:

```
uc80 47c7679   uc_core 3afb47b   upeepz80 a48d725   um80_and_friends 048b3ef
uada80 d40d22e   cpmemu f7b534f   romwbw_emu bd91530   ioscpm 67cd100
z80cpmw 83c088e   cpmdroid d0b0909   uplm80 be01e43   ucow 5b9e488
z80fpga b1de2dc   mbasic b017c6c   GDB (sourceware binutils-gdb) 29dda2b
```

Line numbers in this plan are for those commits. Each fact has a tag:
"measured" (a command ran and gave the result), "read in code", or
"quoted" (from a project's own documentation). The survey did not build
ioscpm, z80cpmw, cpmdroid or z80fpga. The ioscpm checkout was 37 commits
behind `origin/main`, so the ioscpm line numbers can be out of date.

## The owner's request

The request, from `todo.txt` on 2026-11-04, in short:

1. The compilers write Z80 source. The compilers must also write the
   source file and line that each instruction comes from.
2. Optimization makes the map hard. One instruction can come from more
   than one line, for example after common subexpression elimination
   (CSE).
3. A compiler can need a switch that turns optimization off for one
   routine. A global switch can make the program too big for 64K.
4. The assembler and linker must keep, for each byte of output, the source
   line that made the byte. Older debuggers only matched an address to
   the nearest symbol.
5. cpmemu and z80fpga get the runtime support first. The Mac, iOS,
   Android, Linux and Windows clients follow.
6. The debugger user interface can be Python. A GDB integration is
   better, if a GDB integration is possible.

An older note of the owner's, `docs/archive/debug.txt`, adds four points:

- SID and ZSID shared the 64K address space with the program, so a big
  program could not be debugged.
- The map must have more than one level, for example BASIC to C to
  assembler.
- A shim program such as `usid.com` could trap out to the emulator for
  the symbols.
- The functions of SID and ZSID are a guide to the commands.

## What exists today

| Tool | Source positions inside | Written to the output | Debug switches |
|---|---|---|---|
| uc80 + uc_core | Each token has file, line and column. uc80's `srcmap.py` maps through `#include` and `#line` (read in code). | Only `; Function NAME` comments (measured). | `-O0` turns off the text peephole, loop rotation, dead-test folding and empty-call removal (read in code). 84 `--no-*` switches (measured). No `-g`. |
| uplm80 | `pos.origin` on each AST node, set in `note_origins` (`frontend.py:246-275`, read in code). | Only `; Procedure NAME` comments (measured). | `-O 0..3`. `--debug` prints compiler phases (read in code). |
| uada80 | `SourceSpan` on each AST node. The IR has no position field (read in code). | Free-text comments such as `; y := ...` (measured). | `-O0` turns off the AST optimizer and the peephole. The peephole is skipped above 3000 lines. `pragma Optimize` is ignored (read in code). |
| ucow | `SourceLocation` on each AST node (read in code). | Only `; Subroutine NAME` comments (read in code). | `-O0`, `--no-post-opt` (read in code). |
| upeepz80 | None. A line is a plain string (read in code). | Whole-line comments survive. A trailing comment on a rewritten line is lost (measured). | None. |
| um80 | A line counter that also counted macro expansion lines (measured, fixed in `af233b7`). | `.PRN` listing with segment offsets. No line data in the `.REL` (read in code). | `-l` listing. `-g` makes all labels PUBLIC (read in code). |
| ul80 | None. | `.SYM`: one `ADDR NAME` line for each global, plus `__BSS_START`, `__BSS_END` and `__END__` (measured). | `-s`, `-S FILE`. |
| mbasic | BASIC line numbers. | C in the uc80 dialect (the default) or the z88dk dialect, with no `#line` (read in code). | Not applicable. |
| cpmemu | Not applicable. | Not applicable. | `CPM_DEBUG*` logs of BDOS and BIOS calls. No breakpoint, step or disassembler (read in code). |
| romwbw_emu | Not applicable. | Not applicable. | `sim>` prompt: PC breakpoints, step, dumps, a flat symbol file (read in code). |
| ioscpm, z80cpmw, cpmdroid | Not applicable. | Not applicable. | A log switch only (read in code). |
| z80fpga | Not applicable. | Not applicable. | No debug hardware. `clk_en` can freeze the core (read in code). |

Five measured facts decide much of the design.

**GDB has had a Z80 target since GDB 11.1 (2021).** The tag
`gdb-10.1-release` has no `gdb/z80-tdep.c`, and `gdb-11.1-release` and
`gdb-18.1-release` (2026-09-25, the newest release) have the file
(measured). The `NEWS` file lists `z80-unknown-*` under "Changes in GDB
10", but no GDB 10 release has the code. The port has 56 commits from
2021-07-17 to 2026-09-22. Most are changes across all of GDB, and a few
are fixes from z80 users: instruction sizes (2025), a missing symbol
table (2026) and an endless loop in the unwinder (2026) (measured).

The owner's belief that the GDB port is old and not supported is out of date.
`legumbre/gdb-z80`, cited in `docs/archive/debug-research.md`, is an
older fork that the mainline port replaces.

But the mainline port is basic. `gdb/z80-tdep.c` on the GDB default
branch has 1481 lines (measured). Its only unwinder scans for SDCC
prologue patterns, and the port registers no DWARF unwinder and no DWARF
register numbers (read in code). Homebrew has no Z80 GDB formula
(measured with `brew search`), so a user must build GDB from source.

**Real LINK-80 refuses new data in a `.REL` file.** LINK-80 3.44 accepts
one extension item kind of the 256 possible first bytes: `35H`, the
COBOL overlay item (measured). Each other kind gives `?Loading Error`,
before the code bytes or just before the end of the module. Bytes after
the end-of-file item pass, but LINK-80 then copies some of them into the
padding of the `.COM` (measured).

The test method: write a module of 12 code bytes (`MVI C,9`,
`LXI D,msg`, `CALL 5`, `RET` and a `$` string) with `RELWriter` from
`um80.relformat`. Put an extension item (special item 4) before a code
byte, with the kind byte `L` and 2 or 6 data bytes. Write a second module
with no item.

Run `cpmemu l80.com NAME,NAME/N/E` with standard input from
`/dev/null`, and stop cpmemu after a few seconds. LINK-80 waits at its
`*` prompt after an error. LINK-80 3.44 is in
`~/src/external/tools/dcc-v3.6.0/l80.com`.

**A whole-line comment survives upeepz80, and a trailing comment does
not.** upeepz80 passes over comment lines when it matches a pattern. A
fused instruction therefore lands after all the comment lines that the
pattern passed over. For example, `ld hl,5 / ;L6 / push hl / ;L7 / pop de`
became `ld hl,5 / ;L6 / ;L7 / ld d,h / ld e,l` (measured).

**Comment lines change the code of two uc80 passes.** A probe put a
comment line after each instruction of 116 uc80 programs. The probe then
compared the instructions out of each text pass with and without the
comments (measured).

upeepz80, `asm_dce`, `peep_local`, `reglocals` and `iyptr`
gave the same instructions. `peep_flow` changed 2 of 130 calls: its
`_ins_after` (`peep_flow.py:870`) stops at a comment line, so the
`_ex_load` step did not fire. `looprot` changed 1 of 116: its head-test
scan (`looprot.py:254`) stops at a comment line, so one loop was not
rotated.

**um80 line numbers are wrong after a macro.** The line counter counts
expanded lines. um80 reported an error on physical line 9 as line 15,
after two 3-line macro calls (measured). um80_and_friends `af233b7`
fixed this bug on 2026-11-04.

## The design in one view

```
 C / PL/M / Ada / Cowgol source      (BASIC -> mbasic -> C with #line)
        |  compiler -g: marker comments ;#file ;#line ;#func ...
        v
   .mac text --(upeepz80, ucow postopt, uc80 text passes keep markers)--> .mac
        |  um80 --debug-info
        v
   foo.rel (unchanged)  +  foo.dbm (module debug info, JSON)
        |  ulib80 stores foo.dbm with the module; ul80 --debug-info relocates
        v
   prog.com (unchanged)  +  prog.dbg (linked debug info, JSON)
        |                                   |
        |                          dbg2elf (optional)
        |                                   v
        |                        prog.elf (DWARF) --> GDB (z80 target)
        v                                               |
   udb80 (Python CLI, later DAP for VS Code) <----------+
        |  GDB remote serial protocol (RSP) over TCP
        v
   cpmemu --gdb=PORT  |  romwbw_emu  |  GUI clients  |  z80fpga via a bridge
```

## Design decisions

Each decision has a reason. The decisions that only the owner can make are
in [Decisions for the owner](#decisions-for-the-owner), with a
recommendation.

### D1. The emulators speak one protocol: GDB RSP

Each emulator implements a GDB remote serial protocol (RSP) stub, and
nothing else. Both front ends use the same stub: real GDB, and the Python
debugger `udb80`. RSP is small, documented and stable. 80mcp can use the
same stub for its proposed `x80_step` and `x80_breakpoints` tools.

The stub listens on a TCP port on 127.0.0.1. The guest owns the console in
raw mode, and ^C reaches the guest as byte 03H. A stub on the console
would fight the guest for the keyboard.

The symbols and line tables stay in udb80 or GDB on the host. The guest
program keeps all of its 64K, which removes the SID and ZSID limit. This
design also replaces the `usid.com` shim of `debug.txt`: a shim runs in
the guest, and so uses guest memory.

### D2. `-g` never changes the code

A `-g` build gives the same `.COM` bytes as a build without `-g`. The
`-g` switch only adds marker comments and debug files. Each compiler
suite gets a test that compiles its programs with and without `-g` and
compares the `.COM` files. The test compares the `.COM` and not the
`.mac`, because label numbers in the `.mac` can differ. Without this rule,
a bug can go away in the debug build.

Two uc80 passes break the rule today (measured, see above). uada80 also
breaks the rule: its 3000-line limit for the peephole counts the marker
lines (`compiler.py:293` and `:489`, read in code). Each such pass must
skip marker lines.

If a pass cannot skip marker lines, the bytes win over
the lines. The pass then runs on the text with no markers, and the
markers go back at the nearest instruction. The line is then possibly
wrong, but the program is the same.

Code that is easy to debug is a separate request. The switch for that
request is "optnone", for one routine or for all (see D6).

### D3. Markers are whole-line comments with a fixed form

The compilers write markers as whole-line comments. A marker is `;`, then
optional blanks, then `#`. uplm80's `AsmLine` writes `"; " + comment`
(`codegen.py:402-407`, read in code), so a reader must accept the blanks.

A comment line is legal MACRO-80, so real M80 still assembles the output.
upeepz80 keeps whole-line comments in place (measured). A marker is
never a trailing comment, because upeepz80 drops the trailing comment of
each line that upeepz80 rewrites (measured). uc80's own `; RLn` and
`; RYn` tags are trailing comments today, so upeepz80 can drop those tags
too.

A marker is not a label. A label stops the peephole patterns, and so
changes the code (against D2).

The marker set:

```
;#file 1 "src/sumto.c"          file table entry: number and path
;#line 1 12                     the bytes that follow come from file 1, line 12
;#line 1 12 nostmt              same, but not a step point (hoisted code)
;#inline 1 34 sq                the code that follows is sq, inlined at file 1, line 34
;#endinline                     back to the position that was current at ;#inline
;#func _sumto 1 10 frame=none pops=0
;#endfunc _sumto
;#noopt begin                   no optimizer pass changes this region
;#noopt end
```

`pops` is the number of stack bytes that the routine removes from the
caller's stack. In uplm80 the first two arguments go in BC and DE, so
`pops` counts the third and later arguments only.

The marker grammar and the meaning of each field go in one file,
`um80_and_friends/docs/debug_info.md`. All producers and readers follow
that file.

### D4. A marker is a state, and adjacent markers merge

um80 reads markers the way GNU `as` reads `.loc`. A `;#line` marker sets
the current source position. Each byte that follows gets that position, up
to the next marker.

Two or more `;#line` markers with no byte between them all belong to the
next instruction. The line table row for that instruction lists all the
lines, in source order. The last line is the primary line. The merge rule
gives the owner's "more than one source line" case for the text
optimizers. A fused instruction lands after the markers of all the lines
that the fused instruction replaced (measured, D3).

A statement that makes no code also leaves its marker next to the marker
of the next statement. An opening brace and a declaration are examples. The last marker is then the line that made the code. GDB also
shows the last row at an address as the line of that address (measured in
phase 0). A first draft of this plan made the first line primary. In GDB
that rule put `add` on its opening brace, and line 6, the `return`, had
"no code" (measured).

Three rules use the list:

- `break file:LINE` stops at each address whose row lists LINE, primary
  or not.
- `step` runs until PC leaves the address ranges of the current line and
  arrives at a step point. A step point is a row that is not `nostmt`.
  The test uses address ranges, not line numbers, so a one-line loop such
  as `while (x--);` stops after one pass.
- dbg2elf writes each line of the group as a DWARF row at the same
  address with `is_stmt` on, in source order. GDB sets no breakpoint on a
  row with `is_stmt` off: with the lines that are not primary written as
  `is_stmt` off, `break` on such a line said "No compiled code" (measured).
  Only a `nostmt` marker gives `is_stmt` off.

The other kinds of rewrite give these results:

| Rewrite | Result for the line table |
|---|---|
| Delete (dead store, dead code) | The line has no code. `break` on that line uses the next row and says so. |
| `jp` to `jr`, jump threading | No marker moves. Pass 2 of um80 records the final offsets. |
| `call X / ret` to `jp X` | The line stays right. The backtrace loses a frame (D7). |
| Loop rotation copies the test | The copy must carry the markers of the test. |
| Loop invariant motion | The hoisted code goes after `;#line F L nostmt`, with L the line of the original statement. |
| Shared epilogue `jp @name_ret` | Each `jp` has the line of its `return`. The epilogue has the line of the closing brace. |
| An instruction moves across a marker | Not allowed, unless the pass moves a copy of the marker too. |

### D5. Debug info travels in a sidecar file, not in the `.REL`

um80 writes `foo.dbm` next to `foo.rel`, and the `.rel` stays the same
byte for byte. LINK-80 refuses each new kind of extension item
(measured). Data in the `.REL` would therefore break the M80 and L80
compatibility that um80 keeps. Kind `35H` passes LINK-80, but `35H` is
the COBOL overlay item, so the plan does not use `35H`. Bytes after the
end of the file pass LINK-80, but change the padding of the `.COM`
(measured).

ulib80 has its own container format (`ULIB`), so ulib80 can store the
`.dbm` with each module (see the ulib80 work below).

### D6. Optimization off for one routine, in each compiler

Each compiler gets "optnone" for one routine. Each compiler uses the form
that the language already has, if the language has one:

| Compiler | Source form | Command line |
|---|---|---|
| uc80 | `#pragma uc80 optnone` before a function, `[[uc80::optnone]]`, and GCC `__attribute__((optimize("O0")))` | `--optnone=f,g` |
| uplm80 | a new `$` control (name to choose) | `--optnone=PROC,...` |
| uada80 | `pragma Optimize (Off)` in the subprogram (standard Ada, ignored today) | `--optnone=name,...` |
| ucow | none, because Cowgol has no pragma | `--optnone=name,...` |

A routine under optnone:

- is not inlined, and gets no inlined calls
- gets no CSE, copy propagation, dead store removal, loop invariant
  motion or loop unrolling
- keeps its frame (uc80 keeps the IX frame)
- keeps each `call X / ret`. upeepz80 has a `tail_call` pattern for this
  change (`peephole.py:1866`), and uc80 `peep_local` rule 3 makes the same
  change (read in code)
- sits inside `;#noopt begin` and `;#noopt end`, so that no text pass
  changes the routine
- gets a step point for each statement

`--optnone=all` is the global form. Use the global form while the program
fits in 64K. When the program does not fit, use the per-routine form.
`udb80 sizes prog.dbg` lists the bytes of each routine, so the user can
choose the routines. uc80 `-O0` keeps its current meaning.

### D7. Backtrace has three sources, best first

**1. Shadow call stack in the emulator (exact).** When a debugger is
attached, the stub records each call that the CPU takes. A shadow stack
needs no help from the compiler. A shadow stack therefore works for PL/M
procedures with static locals, for hand-written assembler and for library
code. The rules:

1. On a CALL, RST or interrupt that the CPU takes, push an entry: the
   return address, the slot (the SP after the push) and the kind (`call`,
   `rst` or `irq`).
2. On a RET or RETI that the CPU takes, the stub reads the SP before the
   return. Pop the entry whose slot is that SP. If no entry has that slot,
   pop the innermost entry whose return address is the RET target (the
   uplm80 REENTRANT epilogue moves the return address, measured), and drop
   the entries above it: they come from a longjmp, an Ada exception, or a
   routine that pops its own return address. If no entry matches, mark the
   stack "out of step". (As built in phase 1. The first text of this rule
   dropped each entry with a slot below SP before the match. Read
   literally, that drops the entry of the REENTRANT callee itself and
   contradicts rule 4.)
3. A BDOS or BIOS trap in cpmemu returns with no RET instruction. The
   stub applies rule 2 to the trap return.
4. Do not drop entries when SP only rises. The uplm80 REENTRANT epilogue
   puts SP above the slot while the return address is in DE (measured).
5. Keep at most 4096 entries, and drop the oldest first.

A tail call (`jp`) still hides one frame, as in any debugger. A routine
that reads data after its CALL with `pop hl`, and returns with `jp (hl)`,
also hides its own frame. A task switch in the Ada runtime changes the
stack, and makes the shadow stack "out of step". A shadow stack for each
task is later work.

**2. Frame records from the compiler (exact when present).** Each `;#func`
marker gives the frame kind and the `pops` count. For an IX frame, saved
IX is at IX+0 and the return address at IX+2. The survey measured the
layout in uc80, in uada80 and in uplm80 REENTRANT procedures.

IX holds the frame
only between the `add ix,sp` and the `ld sp,ix` of the routine, so the
routine record also gives that address range. Outside the range, and in
a routine with no frame, the debugger uses the SP offset at the PC. The
debugger calculates the offset from the routine's disassembly. GDB, a
crash dump and z80fpga have no shadow stack, so they use this source.

**3. Stack scan (a guess).** A word on the stack that points at the byte
after a CALL instruction in a code range is a possible return address. The
debugger marks such frames as guessed.

`bt` marks each frame with its source: `[shadow]`, `[frame]` or `[guess]`.

### D8. More than one level of line

Each byte gets an assembler position (the `.mac` file and line) from um80,
with or without markers. Bytes with markers also get a source position.
The debugger shows the source by default and can change to the assembler
view. Hand-written library code thus has assembler-level source debugging
with no compiler work.

A third level comes from `#line`. uc80's `srcmap` maps through `#line`
(read in code). mbasic writes C for uc80 with no `#line` today (read in
code).

With `#line` in mbasic's output, a BASIC program gets its own lines
in the debugger. The marker can then carry both the BASIC position and
the C position. The field is reserved in `debug_info.md` and is filled in
phase 4. A program that mbasic compiles with z88dk gets no such lines,
because z88dk does not read these markers.

### D9. The debug file names its program

`prog.dbg` holds the length and the SHA-256 of `prog.com`, as ul80 wrote
the file. CP/M pads a program to a multiple of 128 bytes when CP/M loads
the program. A check against memory therefore compares only the first
`length` bytes. The debugger refuses a `prog.dbg` that does not match,
because a stale debug file gives wrong lines with no error. The check is
for the build. The check does not look at memory again after the program
starts, so self-changing code is the user's problem.

## Components

### Compilers

The four compilers get the same work. The places come from the survey.

- **Markers at statement level.** Each statement gets one `;#line`. A
  `for` header gets one marker for each of its three parts.
  - uc80: `codegen.py` `gen_statement` (line 10699) and `gen_function`
    (line 7928). Take the file and line from the statement's first token
    through `srcmap.locate`. Do not use `pos.start_line`, because that
    value is a line of the preprocessed text (read in code).
  - uplm80: `codegen.py` `_gen_stmt` (line 3953) already records the
    statement node (read in code). The marker needs a raw line, not the
    `AsmLine` comment field (D3).
  - uada80: add a position next to `comment` in `IRInstr` (`ir.py:228`).
    Set the position in `lowering.py` `_lower_statement` (line 9942).
    Write the marker in the codegen `_emit` when the position changes.
  - ucow: `codegen.py` `gen_stmt` (line 1180).
- **`;#func` records.** Write the routine name, the frame kind (`ix`,
  `none` or `static`), the `pops` count and the range where IX holds the
  frame.
- **`;#inline` records.** uc80 inlines small functions, uplm80 inlines at
  `-O3`, and ucow has an inliner. uada80 inlines function calls to a
  `pragma Inline` function (`_can_inline`, `lowering.py:15626`). An Ada
  generic instance also names the instance.
- **Hoisted code.** Loop invariant motion and loop rotation write
  `nostmt` markers (D4 table).
- **CSE and the AST optimizers.** A CSE in the AST optimizer replaces a
  later use with the value of the first computation. The later use then has
  no code, and its line has no address. The debugger shows the line of the
  first computation. The AST optimizers of uc_core and uplm80 keep `pos`
  on rewritten nodes (uplm80: read in code). Each gets a test for this.
- **Size limits.** uada80 skips its peephole above 3000 lines of
  assembler. The count must leave out marker lines (D2).
- **Libraries.** Whole-program uc80 copies library assembler into the
  `.mac`. The library assembler has no markers, and gets assembler-level
  lines (D8).

### uc80 and uc_core

- uc80 runs `asm_dce` before upeepz80. After upeepz80 uc80 runs
  `peep_local`, `peep_flow`, `peep_local` again, then `reglocals`,
  `iyptr` and `looprot` (`main.py:2587-2667`, read in code). Each pass
  must skip `;#` lines and keep them in order.
- Before marker work starts, change `peep_flow._ins_after`
  (`peep_flow.py:870`) and the `looprot` head-test scan (`looprot.py:254`)
  to skip `;#` lines. Then run the probe above on all uc80 examples and
  test programs. The probe is the first D2 test.
- `looprot` copies the loop test, and must copy the markers of the test.
- `peep_local` rule 3 (`call X / ret` to `jp X`) needs a switch that
  optnone and `;#noopt` set.
- uc80 already cuts its text at inline assembler and at
  `BARRIER_BEGIN`/`BARRIER_END` (`_optimize_outside_asm`, `main.py:255`).
  The same cut can make a `;#noopt` region. But a cut hides the region
  from the liveness analysis of the pieces around the region, so the
  region must count as "reads and writes everything".
- uc_core is shared with uc386 and uplm80. A change in uc_core must pass
  all three test suites (uc_core `CLAUDE.md`). `srcmap.py` is in uc80, not
  in uc_core. uc386 needs no new feature and must only stay green.

### upeepz80

- Keep each `;#` line, in order. Add a test that compares the marker
  sequence before and after `optimize()`, over the existing test inputs.
- Add `;#noopt begin` and `;#noopt end`: a region that no pattern
  changes. Liveness must treat the region as "reads everything".
- Add a way to stop named patterns, for example
  `PeepholeOptimizer(disable={"tail_call"})`.
- upeepz80 is on PyPI. uplm80 requires `upeepz80>=0.2.6` and checks the
  behavior of upeepz80 at import (read in code). A new upeepz80 release
  and new pins in uc80, uplm80 and uada80 go with these changes.

### ucow

ucow does not use upeepz80. ucow has its own text optimizer,
`src/postopt.py`, with six passes (read in code). Each pass must skip
`;#` lines and keep them in order. `tail_merging_pass` merges code across
routines, and must not merge across a `;#func` boundary. The passes must
also obey `;#noopt` regions. The D2 test covers ucow too.

### mbasic

mbasic writes `#line N "prog.bas"` before the C code of each BASIC line,
in the uc80 dialect only. uc80 does the rest (D8).

### um80

- **The line counter (done, `af233b7`).** um80 now keeps
  `src_file` and `src_line`, a physical position, for messages and the
  `.PRN`. `line_num` stays as it was, because `promoted_jr` uses
  `line_num` as a key for each expanded line. Debug info takes the
  physical position from `src_file` and `src_line`.
- **`um80 --debug-info`** (the name `-g` is in use). Record one row for
  each instruction or data directive, at the place where the listing
  collects its bytes (`list_field` and `_save_listing_entry`,
  `um80.py:715-730`). Do not record in `emit_byte` and its helpers: some
  helpers call others, and `emit_link_expr` writes bytes without
  `emit_byte` (read in code). Record in pass 2 only, because JR promotion
  changes the offsets in pass 1.
- **A row** holds the segment, the load offset, the length, the run
  address, the `.mac` file and line, and the current marker state. The
  run address is `pc`, which `.PHASE` changes. The linker relocates by the
  load offset, and the debugger shows by the run address. A `DS` with no
  fill writes no bytes, and gets a row with no bytes.
- **Labels.** Record all labels with their segment offset, so local
  labels need no PUBLIC. `-g` makes all labels PUBLIC, and two modules with
  the same local label then give `%Mult. Def. Global` (read in code).
- **Macro expansion.** The assembler line of a byte from a macro is the
  macro call line. The body line is a second record.

### ulib80

The `ULIB` header has a version, 1, and for each module a name, an offset,
a size and the publics (read in code). Version 2 adds a length for a
`.dbm` member in each module. A `.dbm` belongs to the stored module name,
not to the `.REL` file name, because one `.REL` can hold several modules.
ul80 must still read version 1. A library in LINK-80 `.REL` form carries
no debug info, and gets labels only.

### ul80

- **`ul80 --debug-info`.** Read the `.dbm` of each module. Change each row
  from (segment, offset) to a buffer offset with `module.seg_buf_start`.
  Then relocate the row with `_buf_offset_addr` (`ul80.py:817`).
- When two modules write the same byte (`--allow-overlap`, shared
  COMMON), the later module owns the byte (`ul80.py:1130-1135`, read in
  code). Drop the rows of the earlier module for that byte.
- With `--rom-data`, a DSEG row gets two addresses: the run address in RAM
  and the load address in the image. The debugger uses the run address.
  The SHA-256 (D9) covers the image as written.
- Write `prog.dbg`. The `.SYM` stays as it is, because SID, ZSID and
  romwbw_emu `--symbols` read that format.

### The debug file `prog.dbg`

JSON, with a format version. The specification is
`um80_and_friends/docs/debug_info.md`, next to the producer. Rows are
arrays, not objects, so that a big program gives a small file. Contents:

- format version, producer, and the length, SHA-256 and load address of
  the program image (D9)
- memory model: the CP/M TPA at 0100H, or a RomWBW bank
- file table
- line table: address ranges, each with a list of source-level and
  assembler-level positions, a primary flag and a step flag
- routines: name, start, end, frame kind, `pops`, the IX range, and the
  declaration line
- inline records
- labels: all labels, global and local
- variables (phase 8): name, type, location, and the address range where
  the location is valid

### cpmemu: the RSP stub

All new code is in a new file pair, `src/gdbstub.cc` and `src/gdbstub.h`,
plus `cpmemu.cc` and `src/os/*/platform.cc`. The stub uses a small target
interface: read and write a register, read and write memory, run one
instruction. The RomWBW clients can then use the same file later
(decision 9). **qkz80 does not change.** Four sibling projects compile
qkz80 out of this tree (see
[Who else compiles qkz80](qkz80_library.md#who-else-compiles-qkz80)).

**Start and stop.**

- `--gdb[=PORT]`, default port 1234. Port 0 lets the system choose, and
  `--gdb-port-file=FILE` writes the chosen port, so tests can run in
  parallel. cpmemu waits for a connection and stops before the instruction
  at 0100H.
- The run loop calls `handle_pc` for each instruction (`cpmemu.cc:4785`,
  read in code). The stub check goes before `handle_pc`. A breakpoint at
  the BDOS entry therefore stops before the trap pops the return address.
- A trap is not an instruction. `handle_pc` returns true, and the loop
  skips `execute()` (read in code). A step at a trap address (0FD00H,
  0FF00H-0FF1FH) runs the trap once and stops at the return address.
- HLT waits for the next timer tick. With no interrupt that can come,
  HLT ends the run (fixed on 2026-11-04, `tests/hlt.asm`). With a
  debugger attached, such a HLT stops the program and does not end the
  run.
- With `--int-cycles`, a step can go into the interrupt routine at the
  RST vector (0038H by default, `--int-rst` changes the vector). GDB's
  `step` expects to stay in the current routine, so the stub runs the
  interrupt routine to its end.

**Speed.** The current loop runs about 89 to 94 million instructions each
second (measured: 9e9 instructions of a DEC/OR/JR loop in 100.8 s, and 67
million instructions of another loop in 0.71 s). The stub must not change
this rate when no debugger is attached.

Measure one branch against a
second copy of the loop. Use the second copy if the branch costs more
than 1 percent. The measurement goes in the commit message, because a
speed test in `run_tests.sh` would fail at random on a loaded machine.

**Packets.**

- Connection: `qSupported` (with `PacketSize`, `qXfer:features:read+`,
  `swbreak+`, `vContSupported+` and `QCatchSyscalls+`), `vMustReplyEmpty`,
  `qAttached` (reply `1`), `qC`, `qfThreadInfo` and `qsThreadInfo` (one
  thread), `Hg` and `Hc`, `qXfer:features:read:target.xml`.
- An empty reply to each packet that the stub does not know. GDB requires
  this.
- Run control: `?`, `c`, `s`, `vCont?` (reply `vCont;c;C;s;S;r`), `vCont`
  with range stepping (`r`), `k` (end cpmemu), `D` (let the program run
  on), the 03H interrupt.
- State: `g`, `G`, `p`, `P`, `m`, `M`, `X`. An address at 10000H or above
  gets `E01`.
- Breakpoints: `Z0`/`z0` and `Z1`/`z1`. The stub keeps the breakpoints
  and never writes into guest memory. A stop at a breakpoint replies
  `T05swbreak:;`.
- Later in phase 1, after the above works: watchpoints `Z2`-`Z4`,
  `QCatchSyscalls` for BDOS calls, and `qRcmd` for `monitor` commands.

**Registers.** GDB's z80 register order is af, bc, de, hl, sp, pc, ix, iy,
af', bc', de', hl', ir. `gdb/features/z80-cpu.xml` gives pc 32 bits, and the
other registers 16 bits. The stub serves the same XML as `target.xml`, with
pc at 16 bits: see [Phase 1 results](#phase-1-results). A test checks that
the `g` reply has the length that the XML gives.

**Watchpoints.** A subclass of `qkz80_cpu_mem` overrides the virtual
`store_mem` and `fetch_mem` (`qkz80_mem.h:14-15`, read in code). The qkz80
headers do not change.

**^C from the debugger.** Poll the socket every 4096 instructions, at the
place where `end_if_terminated()` runs now (`cpmemu.cc:4805`). A console
read blocks in `platform::console_getchar` (read in code). The console
read must therefore wait on the console and on the socket together, and
stop with `T02` when the debugger sends 03H.

**BDOS catchpoints.** With `QCatchSyscalls+`, GDB `catch syscall 15`
stops at BDOS function 15. The stop reply is `T05syscall_entry:f;`. The
GDB manual shows a numeric `catch syscall` with no names file (quoted),
but the plan did not test the numeric form on z80. Phase 0 tests the
numeric form.

**Exit.** One helper sends `W` (exit code) or `X` (signal). These paths
call the helper (read in code):

- `program_exit` (`cpmemu.cc:145`)
- the exit after five ^C (`:185`)
- the end of console input (`:238`)
- the two `exit(1)` paths for a read-only drive and a BIOS disk error
  (`:4061`, `:4202`)
- `qkz80_global_fatal` (`qkz80_errors.cc:7`)
- the return from `main` after the instruction limit
- `end_by_signal` (`:216`), with `X` and the signal number

**Shadow call stack** (D7) and `monitor bt`, only when a debugger is
attached.

**Crash backtrace.** `--backtrace` keeps the shadow stack with no
debugger attached. On an abnormal end, cpmemu prints the stack as hex
addresses: after a fatal BDOS or BIOS error (`exit(1)`), the instruction
limit, five ^C or a signal.

An unknown opcode is not an end. No opcode reaches
`qkz80::unimplemented_opcode`, in either CPU mode, with or without a CB,
DD, ED or FD prefix (measured). An undefined ED opcode runs as a NOP, as
on the Z80. `udb80 addr2line` turns the addresses into lines. Measure the cost of
`--backtrace`.

**Windows.** Winsock in `src/os/windows/platform.cc`. `<winsock2.h>`
must come before `<windows.h>`. Link `ws2_32` in `do_build.bat`,
`Makefile.win` and `src/CMakeLists.txt`. The mingw compile in
`tests/run_tests.sh` must stay free of warnings.

**Test.** `tests/gdb_stub_test.py` is a small RSP client. The test needs
no GDB. The first case does these steps:

- set a breakpoint, step, and step over `CALL 5`
- read the registers, and check the `g` length
- send ^C to a guest that waits for a key
- check the exit packet

A second case runs a recursive guest and
checks `monitor bt`. A third case runs a uplm80 REENTRANT procedure with
three arguments and checks the shadow stack (D7 rule 2). A fourth case
fills the TPA and shows that the debugger uses no guest memory.
`tests/run_tests.sh` runs the test on all three CI platforms.

### udb80: the Python debugger

udb80 is a new tool. The plan puts udb80 in um80_and_friends, next to the
`ud80` disassembler that udb80 needs, and next to the producer of
`prog.dbg`.

- **Core.** udb80 reads `prog.dbg` and checks D9. udb80 has an RSP
  client. udb80 turns `break file:line` into a set of addresses, because
  one line can have many addresses (inlined code, loop rotation, generic
  instances).
- **step** uses the D4 rule, with `vCont;r` range stepping, so the stub
  does the work and one round trip covers the whole line.
- **next** is like step, but runs over a call. When the stub reports a
  call, udb80 sets a temporary breakpoint at the return address. The
  breakpoint counts only when the shadow stack is back to its depth before
  the call. A depth below that also stops `next`, after a longjmp for
  example. A user breakpoint in the called routine stops `next` first.
  Without a shadow stack (GDB, z80fpga), the test is
  `SP >= SP before the call`, because a uplm80 callee pops its arguments.
- **finish** runs until the shadow stack is shorter.
- **bt** uses the sources of D7 in order, and marks each frame with its
  source.
- **Commands.** Words close to GDB's: `break`, `continue`, `step`,
  `next`, `finish`, `bt`, `list`, `disassemble`, `info registers`, `x`,
  `print`, `watch`, `sizes`. The SID and ZSID map is below.
- **VS Code.** A Debug Adapter Protocol (DAP) server on the same core,
  after the CLI works.
- **`udb80 addr2line prog.dbg ADDR...`** for crash backtraces from cpmemu
  and Ada.

The SID and ZSID commands (from `docs/archive/debug-research.md`) and
their udb80 words:

| SID | udb80 |
|---|---|
| A (assemble) | not planned |
| C (call a routine) | `call ADDR` (later) |
| D (display memory) | `x` |
| F (fill), M (move), S (set) | `set *ADDR = ...` (later) |
| G (go, two breakpoints) | `break` and `continue` (no limit) |
| H (hex, symbols) | `print`, `info symbols` |
| I (command line for the program) | `cpmemu --gdb prog.com ARGS` |
| L (list with symbols) | `disassemble` |
| P (pass counts) | `break ... ignore N`, `watch` |
| R (read program and symbols) | `prog.dbg` |
| T (trace), U (untrace) | `step`, `stepi` |
| X (registers) | `info registers` |

### GDB

- **Build.** GDB has a Z80 target since GDB 11.1, but Homebrew has no
  formula. Phase 7 adds `docs/gdb.md` to um80_and_friends with the build:
  `configure --target=z80-unknown-elf`.
- **`dbg2elf`.** A Python tool that writes an ELF file: ELFCLASS32,
  little-endian, `e_machine` 220 (`EM_Z80`), and `e_flags`
  `EF_Z80_MACH_Z80` (BFD reads the machine from `e_flags`, read in code).
  The file holds the program image at 0100H and a symbol table. The file
  also holds DWARF `.debug_line`, `.debug_info`, `.debug_abbrev` and the
  string sections, with an address size of 2. pyelftools only reads ELF, so the writer is
  new code.
- **Variables in GDB.** The z80 port has no DWARF register numbers (read
  in code). Only locations with no register work: `DW_OP_addr` for static
  storage. IX-relative locals need a Python helper in GDB.
- **Backtrace in GDB.** GDB's z80 unwinder knows only SDCC prologues. A
  GDB Python unwinder (`gdb.unwinder`) reads the frame records from
  `prog.dbg`, or asks the stub with `monitor bt`. The GDB manual says that
  GDB tries Python unwinders before the unwinders built into GDB (quoted).
  A test checks that the Python unwinder claims the frame.
- **Breakpoints.** The stub answers `Z0`, so GDB does not write into guest
  memory. GDB therefore needs no `_break_handler` symbol.
- **CI.** CI cannot build GDB in each run. The GDB test skips when no z80
  GDB is present. CI sets `UDB80_REQUIRE_GDB=1` where GDB is present, so
  that a skip fails, as `CPMEMU_REQUIRE_MSVC=1` does for MSVC.
- **Acceptance.** `break sumto.c:12`, `continue`, `step`, `next` and `bt`
  work in GDB against `cpmemu --gdb` for a uc80 test program.

### Variables

Variables come after line debugging and backtrace work. The survey found
these locations:

- uc80: static `??AUTO+n` for routines that are not recursive, `IX-n` in
  an IX frame, and the registers B, C, D, E and IY for register locals. A
  register local is valid only in part of a routine (read in code).
- uplm80: static `??AUTO+n`, shared between procedures that are never
  active at the same time. The individual locals have no label (measured).
- uada80: `IX-n` and `IX+n`, with Ada types: array bounds, discriminants,
  variant records (read in code).
- ucow: `v_SUB_VAR` labels, or `_wsbase+n` with workspace sharing
  (measured: two locals of two routines at the same address).

A shared address is correct for an active frame. A variable record must
give the routine and the PC range, never the address alone. A first
release can support scalars, pointers and arrays.

### Platforms

| Client | Platforms | Stub transport | Test | Can build on this Mac |
|---|---|---|---|---|
| cpmemu | Linux, macOS, Windows | TCP on 127.0.0.1 | `tests/gdb_stub_test.py` in `run_tests.sh`, all three CI jobs | yes |
| romwbw_emu | Linux, macOS | TCP on 127.0.0.1 | a Python RSP test in its suite | yes |
| ioscpm | iOS, macOS | TCP. The iOS app listens on the device network. | `MANUAL_CHECKS.md` | no (needs Xcode) |
| z80cpmw | Windows | TCP on 127.0.0.1 | `MANUAL_CHECKS.md`, or a test with no window | no (needs MSVC) |
| cpmdroid | Android | TCP, reached with `adb forward` | `MANUAL_CHECKS.md` | no (needs the Android SDK) |
| z80fpga | Nexys A7 board | serial line to a Python bridge | `sim/tb_romwbw`, then the board | no |

Each GUI client gets a setting that turns the stub on and sets the port.
The stub is off by default.

### RomWBW clients

- **romwbw_emu first.** romwbw_emu builds on this Mac, and its run loop
  already checks `breakpoints.count(pc)` (`romwbw_emu.cc:1706`, read in
  code).
- **Banks.** Addresses at 8000H and up are always in bank 8FH. A CP/M 2.2
  program at 0100H runs in bank 8EH (`hbios_dispatch.cc:2597` and
  `romwbw_mem.h:40`, read in code). GDB's z80 data pointers are 16 bits,
  so the stub shows one bank in the low 32K. A `monitor bank` command
  selects the bank.
- **Which program.** The emulator does not load the program. CP/M loads
  the program. udb80 gives the stub the first bytes of `prog.com`. The
  stub stops when PC is 0100H in bank 8EH and those bytes match (D9).
- **The other three clients.** ioscpm, z80cpmw and cpmdroid have their own
  run loops: `hbios_core.cc:269` (the `execute()` call is at line 280),
  `EmulatorEngine.cpp:392` (the loop is at line 396) and
  `emu_io_android.cpp:1346` (read in code). Each loop gets the stub check.
  z80cpmw runs 100000 instructions for each lock, so the check must be
  inside that batch (read in code). Each item gets a `[MAC]`,
  `[WINDOWS]` or Android tag in the `todo.txt` of its project.
- **Shared code.** romwbw_emu's shared files (`hbios_dispatch`,
  `hbios_cpu`, `romwbw_mem.h`) go into ioscpm, z80cpmw and cpmdroid on
  their next build, with no version gate. A stub in those files reaches
  all three at once.

### z80fpga

- **Hardware debug unit.** Stop at an instruction boundary with the
  existing `clk_en` gate and `m1_n`. Compare PC (and the MMU bank) during
  M1. Read the registers through a multiplexer. The survey estimates 300
  to 600 LUTs (a guess). The build uses 15009 LUTs and 128 of 135 RAMB36,
  which is 94.81 percent (quoted from the board README and `CLAUDE.md`).
  The unit must use LUTs and registers only.
- **Host link.** The only UART is the console. A second UART on a Pmod
  header keeps the console clean. The Nexys `.xdc` defines no Pmod pins
  today, so the second UART needs new constraints (read in code). A
  host-side Python bridge turns a small serial protocol into RSP, so udb80
  and GDB need no change.
- **Memory while stopped.** The stack is in DDR2 behind the MMU. The debug
  unit must read memory through the core bus while the core is stopped.
- **Backtrace.** z80fpga has no shadow stack, so the backtrace uses frame
  records (phase 6) and the stack scan.
- **Test.** Prove the unit in `sim/tb_romwbw` first. Each Nexys build takes
  about 25 minutes, and the build has a small timing margin (quoted from
  `CLAUDE.md`).

## Phase 0 results

GDB 18.1 was built on 2026-11-04 on this Mac (macOS 27, arm64), from
`gdb-18.1.tar.xz`. The GNU signature was good (Joel Brobecker's key, from
the GNU keyring). GDB 18.1 needs GMP and MPFR. MPFR came from Homebrew.

```
configure --target=z80-unknown-elf --disable-nls --disable-werror \
    --with-python=/opt/homebrew/bin/python3.14 --with-gmp=/opt/homebrew \
    --with-mpfr=/opt/homebrew --without-guile --disable-sim
make -j10 all-gdb
```

The build took about one minute. `set architecture` lists z80,
z80-strict, z80-full, r800, gbz80, z180, z80n, ez80-z80 and ez80-adl.

The test program is a C file of 16 lines and a hand-written `.mac` of the
same program with `;#line` markers. um80 and ul80 assembled the `.mac`,
and a prototype of `dbg2elf` read the `.PRN` and wrote the ELF: ELFCLASS32,
`e_machine` 220, `e_flags` 1, `.text` at 0100H, a symbol table, and DWARF
4 `.debug_line`, `.debug_info` and `.debug_abbrev`. The ELF has 981 bytes.
GDB loaded the ELF with no target and gave these results (measured):

- GDB set the z80 architecture from the ELF.
- `info functions`, `info variables`, `ptype` and `info address` were
  correct. A global with `DW_OP_addr` is "static storage at address 0x122".
- `info line FILE:N` and `info line *ADDR` gave the right ranges.
- `break FILE:N` and `break FUNCTION` gave the right addresses. A
  `nostmt` row (the loop increment) took no breakpoint, as D4 intends.
- `list` and `disassemble /s` showed the C lines with the Z80 code. Two
  lines at one address show as two source lines above the shared code.
- With no stub, GDB warns that it found no `_break_handler` and that it
  would write RST 08H for a breakpoint. The stub of phase 1 answers `Z0`,
  so GDB writes nothing.

Not tested yet: a run against a stub, `step` and `next`, a Python
unwinder, and IX-relative locals. These need the phase 1 stub.

## Phase 1 results

Phase 1 was built on 2026-11-04: `src/gdbstub.cc`, `src/gdbstub.h`, the socket
functions in `src/os/*/platform.cc`, and `tests/gdb_stub_test.py`. The user
view is [gdb_stub.md](gdb_stub.md). Three parts of this plan were wrong, and
real GDB 18.1 showed all three (measured, against `cpmemu --gdb`):

- **pc is 16 bits on the wire.** The plan said 32 bits, from
  `z80-cpu.xml`. GDB 18.1 sizes a `code_ptr` register by the pointer size of
  the architecture, 2 bytes for a Z80, and ignores `bitsize`. A 28-byte `g`
  reply gave `Remote 'g' packet reply is too long (expected 26 bytes, got 28
  bytes)`. The stub serves pc as 16 bits.
- **`vCont?` must list `C`.** `vCont;c;s;r` made GDB turn `vCont` off, with
  `Packet vCont (verbose-resume) is NOT supported`. GDB 18.1 needs `c` and
  `C`. The stub answers `vCont;c;C;s;S;r`.
- **`catch syscall` does not work on a Z80 in GDB 18.1.** The command answers
  `The feature 'catch syscall' is not supported on this architecture yet`. The
  stub implements `QCatchSyscalls` for other clients, and a client that sends
  the packet itself can use the packet.

One rule differed in the build. D7 rule 2 said to drop each entry with a
slot below SP first. That drops the entry of the REENTRANT callee itself.
The stub pops the entry with the slot equal to SP, or else the innermost entry
with the RET target as return address, and then drops the entries above it.
D7 now says so.

With GDB 18.1 and the `dbg2elf_proto` ELF file for `sumto.com`, these worked:
`break sumto.c:14`, `continue`, `info registers`, `step`, `next`,
`print total`, `bt` (GDB's own unwinder showed both frames for this
frameless program), `watch total` (a hardware watchpoint through `Z2`),
`tbreak`, `stepi` and `continue` to `exited normally`.

## Phases

Each phase ends with a test that can fail. **Milestone A** is phases 1 to
4 for uc80 only: a source-line breakpoint, a step and a backtrace for a C
program under cpmemu. Milestone A shows that the marker design works
before the other compilers start.

| Phase | Work | Projects | Depends on | Acceptance |
|---|---|---|---|---|
| 0 | Decisions. Build GDB for z80 (done). Load a hand-made ELF with `.debug_line` at 0100H (done, see above). Test numeric `catch syscall`. Measure one branch in the cpmemu loop. | all | none | The results go into this file, with the commands. A failed measurement changes phase 1 or 7. |
| 1 | RSP stub in `gdbstub.cc`, shadow call stack, `--backtrace` | cpmemu | 0 | `tests/gdb_stub_test.py` passes in `run_tests.sh` on all three CI platforms. `--backtrace` prints the expected chain for a guest that ends with a fatal BDOS error. Speed is the same with no debugger attached (commit message). |
| 2 | `--debug-info` in um80, ulib80 and ul80 (the line counter fix is done). `debug_info.md`. | um80_and_friends | none | For a test program with a macro, a JR promotion, a `.PHASE` block and a library module: the `.dbg` address of each label equals the ul80 address, and each instruction has a `.mac` line. |
| 3 | udb80 core and CLI, `addr2line`, `sizes` | um80_and_friends | 1, 2 | Break on an assembler line, step, and `bt` (shadow stack) on a recursive assembler program under `cpmemu --gdb`. A golden test for `addr2line`. |
| 4 | Fix `peep_flow` and `looprot` (D2). Markers in uc80, then uplm80, uada80 and ucow. upeepz80 marker test. um80 reads markers. `;#noopt` in um80 and upeepz80. `#line` in mbasic. | compilers, upeepz80, um80, mbasic | 2 | The D2 byte test passes in each compiler suite. Each code byte of a uc80 test program, other than runtime code, has a source line (the percentage goes in the commit). Break on a C line and step through the program. Break on a BASIC line. |
| 5 | Optnone (D6), the pattern switch, `peep_local` rule 3 switch | compilers, upeepz80 | 4 | A routine under optnone keeps its IX frame and its calls, and the step stops at each statement. A program that does not link under `--optnone=all` links with `--optnone=f`. |
| 6 | Frame records. Ada `GNAT.Traceback.Call_Chain`. Unhandled exception message with name and chain. | compilers, uada80 runtime | 4 | `bt` from frame records equals the shadow stack for the phase 3 program and for a uc80 program. A uada80 test program with an unhandled exception prints the name and the chain. |
| 7 | `dbg2elf`, GDB Python unwinder, `docs/gdb.md` | um80_and_friends | 0, 4 | The GDB acceptance list above, skipped only when no z80 GDB is present. |
| 8 | Variables | compilers, um80, udb80, dbg2elf | 4 | A test program for each compiler: `print` of a local in each storage kind. |
| 9 | romwbw_emu stub with banks, then ioscpm, z80cpmw and cpmdroid | RomWBW clients | 1, 3 | udb80 stops at 0100H in bank 8EH for a program run from a disk image in romwbw_emu. The other three clients: `MANUAL_CHECKS.md` items. |
| 10 | Debug unit and Python bridge | z80fpga, udb80 | 1, 3, 6 | A breakpoint and a step in `sim/tb_romwbw`, then on the board. |

Phases 1 and 2 can start at once and in parallel. Phase 1 is useful
alone: GDB or udb80 can debug at the instruction level with `.SYM`
symbols.

## Decisions for the owner

Each decision has a recommendation. The decision is still the owner's.

1. **GDB or Python first.** The owner prefers GDB, if GDB is possible.
   GDB is possible, but GDB needs more work before GDB shows source lines
   and a backtrace: a source build, a new ELF and DWARF writer
   (`dbg2elf`), and a Python unwinder. udb80 needs none of these, and the
   stub serves both. Recommendation: the stub, then udb80 (Milestone A),
   then GDB in phase 7. The other order is also possible: phase 7 needs
   only phases 0, 2 and 4.
2. **Sidecar or `.REL`.** Recommendation: the sidecar `.dbm` (D5). The
   measured LINK-80 refusal is the reason.
3. **Marker form.** Recommendation: `;#` whole-line comments (D3). A new
   pseudo-op such as `.LOC` is cleaner, but real M80 then refuses the
   compiler output.
4. **Line for a byte from a macro.** Recommendation: the call line is
   primary, and the body line is a second record.
5. **Home and name of udb80.** Recommendation: `udb80` in
   um80_and_friends.
6. **Where the "no optimize" region lives.** Recommendation: the `;#noopt`
   region in upeepz80, in ucow's `postopt.py` and in uc80's text passes,
   so that the liveness analysis sees the whole routine. The other choice
   is a cut of the text, as uc80 does for inline assembler.
7. **Ada exception names.** uada80 already writes a name table,
   `_exc_ntab`, when the program reads exception names
   (`codegen/__init__.py:105-130`, read in code). Recommendation: under
   `-g`, write the table always, and use the table for the unhandled
   exception message. Measure the size of the table.
8. **z80fpga host link.** Recommendation: a second UART on a Pmod header.
   An escape byte on the console changes what the guest reads.
9. **Stub file for the RomWBW clients.** Recommendation: the clients
   compile `src/gdbstub.cc` from this tree. The stub file then becomes a
   new channel like the qkz80 channel (`CLAUDE.md`), so the target
   interface must stay small.
   The other choice is one copy of the stub in each client.
10. **Scope.** Recommendation: approve Milestone A first, then decide the
    rest. Phases 8 to 10 are each a project of their own.

## Bugs that the survey found

The owner asked on 2026-11-04 for these bugs to be fixed, except the
uada80 bugs, which are recorded only.

- um80: line numbers in `.PRN` and in error messages were wrong after a
  macro or REPT (measured). Fixed in um80_and_friends `af233b7`, with the
  file name in messages for an INCLUDE file. 1554 `.rel` files are
  byte-identical before and after (measured). The fix is the first item
  of phase 2.
- romwbw_emu: the `sim>` commands `e`, `dm` and `d` read and wrote the flat
  `get_mem()` array, which banked mode never uses. The commands showed
  zeros (measured on a booted 3.6.0 ROM). Fixed in romwbw_emu `4814eb6`,
  with `banked_mem::peek_mem()`, an additive member of the shared
  `romwbw_mem.h`.
- ioscpm: the dangling `iOSCPM/Core/romwbw_pin.h` symlink was already gone.
  ioscpm commit `ed660d5` removed the symlink before the survey. The survey
  read a local checkout 37 commits behind `origin/main`.
- uada80: the comment over the exception handler frame in
  `runtime/runtime.mac` gives 8 bytes, and the code uses 10 bytes. uada80
  skips the peephole with no message above 3000 lines of assembler.
  Recorded in the uada80 `todo.txt` (`b8f06b0`), not fixed.
- cpmemu: the comment over the BIOS trap test gave the range as
  0FF00H-0FF10H, and the code checks 0FF00H-0FF1FH. Fixed.
- cpmemu: HLT ran on like a NOP, because nothing read `halted_`. HLT now
  waits for the next timer tick, or ends the run with exit status 1 when no
  interrupt can come. Fixed, with two checks from `tests/hlt.asm`.
- cpmemu: the draft of this plan said that an unknown opcode runs as a NOP
  with no message. That claim was wrong. No opcode reaches
  `qkz80::unimplemented_opcode` (measured), and an undefined ED opcode is
  a NOP on the Z80 too.
