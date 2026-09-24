# CP/M support

What this emulator implements of CP/M 2.2, and the memory map a guest sees.

## BDOS functions

| # | Function | Status |
|---|----------|--------|
| 0 | System Reset | Supported |
| 1 | Console Input | Supported |
| 2 | Console Output | Supported |
| 3-5 | Auxiliary/List I/O | Supported |
| 6 | Direct Console I/O | Supported |
| 7-8 | Get/Set IOBYTE | Supported |
| 9 | Print String | Supported |
| 10 | Read Console Buffer | Supported |
| 11 | Console Status | Supported |
| 12 | Get Version | Supported |
| 13-14 | Reset/Select Disk | Supported |
| 15-16 | Open/Close File | Supported |
| 17-18 | Search First/Next | Supported |
| 19 | Delete File | Supported |
| 20-21 | Read/Write Sequential | Supported |
| 22 | Make File | Supported |
| 23 | Rename File | Supported |
| 24 | Get Login Vector | Supported (A: plus every configured or selected drive) |
| 25-27 | Current Drive, Set DMA, Allocation Vector | Supported |
| 28-30 | Write Protect, Read-Only Vector, Set Attributes | Stub (accepted, nothing stored or enforced) |
| 31-32 | Get DPB, Get/Set User | Supported |
| 33-34 | Read/Write Random | Supported |
| 35 | Compute File Size | Supported |
| 36 | Set Random Record | Supported |
| 37 | Reset Drive | Supported (the DE bitmap is ignored; every open file closes) |
| 38 | Access Free Space | Stub (returns success) |
| 39 | Free Space | Stub (no-op) |
| 40 | Write Random Zero Fill | Supported |
| 48 | Flush Buffers | Supported (writes back text file changes not yet written, `FFh` if one cannot be; binary writes go straight to the host file) |

A file's position is its FCB's, as in CP/M: BDOS 20 and 21 read and write
record CR of logical extent EX of module S2, which is the record BDOS 36
reports, and a random read or write leaves CR, EX and S2 at its record, so the
next sequential call reads it again or writes it again. Open keeps the EX it is
given and does not touch CR, so a program zeroes both itself. A text file with
conversion is held as the file a disk would hold - its host text converted, LF
to CR LF, ending at `^Z`, padded with `^Z` to a record - and every call reads,
writes and counts that, except BDOS 17 and 18, whose directory entries are
sized from the host file. It is written back as host text in the file's own
style; `docs/file_handling_notes.md` says how.

The disk this emulates has 2 KB blocks and `EXM` = 0, so a directory entry is
one logical extent of 128 records. Open fails, `FFh`, for an extent the file
has no records in - extent 0 always exists, even for an empty file - and sets
RC to that extent's record count. Search First and Next return one entry per
extent: an FCB whose EX is `?` gets every extent (of the module S2 names, or
of every module if S2 is `?` too), any other EX gets that extent of module 0
or nothing, and a drive byte of `?` gets every extent whatever EX holds.

As in CP/M, a close, a disk reset or a copy of an FCB leaves the FCB usable: a
read or write through an FCB the emulator holds no host file for opens the
file again from its name and goes on from the FCB's position.

Where this differs from 2.2 on purpose:

- **Random records up to 2^18 - 1.** 2.2's `POSITION` answers error 6 for any
  record of 65536 or more (R2 not zero). This takes records up to 262143, as
  CP/M 3 and MP/M II do, and answers 6 past that, the most S2:EX:CR can hold.
  It is here for the MP/M II and CP/M 3 tools this emulator runs and for host
  files over 8 MB; 4.9.0 read and wrote any record at all.
- **Unwritten random records.** A host file cannot say which of its records
  were written, so a record inside the file that nothing wrote reads as the
  zeros the host returns for a hole, where 2.2 answers 1 (unwritten data) or 4
  (unwritten extent), and a record past the end answers 1 whether or not 2.2
  would have had its extent and answered 4.
- **A write at CR = 128** writes record 0 of the next extent, where 2.2
  answers 1; an end-of-file read at an extent boundary leaves the FCB there
  rather than on the next extent. Both only make an append succeed that 2.2
  would have refused.
- **A text file holds its text.** A text file with conversion is kept on the
  host as text, which ends at the first `^Z`, so what a program writes after
  that `^Z` is read back while the file stays open and is gone once it is
  closed and opened again. No text reader sees it on a disk either. A record
  written past the end leaves the records before it, if any are missing, as
  NULs, which is what a new block holds here rather than what a disk left in
  it. When those NULs land in the text of a file that is text only because
  of what it holds, the host file becomes the file's records as a disk holds
  them, CR LF and padding included, until the text is text again; see
  `docs/file_handling_notes.md`.
- **A close can fail.** A text file's change that could not be written back
  to the host file - read-only, or its disk full - makes BDOS 16 answer `FFh`,
  which 2.2's close answers only for a file that is not in the directory. It
  is said on stderr as well.
- **An FCB that runs past FFFFh** - the bytes the call uses, 36 for BDOS 33-36
  and 40, 33 for 20 and 21, fewer for the others - is refused with `FFh`
  rather than wrapping to 0000h. No CP/M program keeps an FCB there, above
  the BIOS. The DMA buffer does wrap, as a Z80 address does.

The allocation vector BDOS 27 points at is initialised all-free and never
updated, so a program that reads it to compute free space gets the same answer
whatever is on the drive. Any other function number prints
`Unimplemented BDOS function N` on stderr and returns 0xFF.

## BIOS functions

- Console I/O: CONST, CONIN, CONOUT (implemented)
- Device I/O: LIST, PUNCH, READER, LISTST (implemented)
- SELDSK: returns the Disk Parameter Header at 0xFE33 for any drive A: to P:,
  and 0 above that. Every letter is selectable because an unconfigured drive
  means the working directory, not an absent disk.
- WBOOT: exits the emulator, as does a jump to 0x0000. BOOT is not implemented.
- SECTRAN: implemented, not a stub. BC is the logical sector and DE the
  translate table, and the physical sector comes back in HL: the byte at DE +
  BC with H = 0, which is the skeletal CBIOS lookup, or HL = BC when DE is 0,
  which is the no-translation convention that listing omits. It is arithmetic
  over the guest's own memory and reaches no media, so
  it cannot fail and does not answer to `CPM_BIOS_DISK` - all three modes give
  the same answer, and `error` no longer ends the run on a table lookup. The
  call leaves A untouched, as the skeletal listing does.
- HOME, SETTRK, SETSEC, SETDMA, READ, WRITE: stubs, returning A = 0 or A = 1
  per `CPM_BIOS_DISK`. Of the six, only READ and WRITE return a status in A;
  HOME, SETTRK, SETSEC and SETDMA return nothing, so the byte is simply unread
  there. File I/O is handled at the BDOS level, so a program that drives the
  disk through the BIOS will not work.

## Memory layout

```
0x0000-0x0002  JMP to BIOS WBOOT
0x0003         IOBYTE (device control)
0x0004         Current drive/user
0x0005-0x0007  JMP to the BDOS entry
0x005C-0x007F  Default FCBs, the first at 0x005C and the second at 0x006C,
               filled from the first two command-line words as the CCP
               fills them: a name not given is blank, and a * fills the
               rest of its field with ?
0x0080-0x00FF  Default DMA buffer (command tail)
0x0100-0xFCFF  TPA (Transient Program Area); a .COM file is read into it
0xFD00         BDOS entry - a trap address, no code in memory
0xFE00-0xFE32  BIOS jump table, 17 entries of three bytes
0xFE33-0xFF51  BIOS disk workspace: DPH, DPB, directory buffer,
               allocation and check vectors
0xFFF0         Initial stack pointer
```

There is no CCP: nothing runs above the TPA, and a program that returns lands
back in the emulator rather than at a command prompt.

## End of redirected input

When stdin is a file or a pipe and it runs out, the first BDOS 1 read still
returns CR, so a line the program was part way through submits. Every BDOS 1
read after that returns `^Z`, CP/M's end-of-input character, which is what BIOS
CONIN returns from the first read on - a program that checks for it stops on its
own. Function 10 ends the line it was collecting. BDOS 6 has no way to say `^Z`:
0 is its answer for both "nothing waiting" and end of input, so a program that
only polls never sees the end. A program that reads on regardless is stopped
after 1024 consecutive reads past the end, with a message on stderr, rather than
being left to loop forever.
