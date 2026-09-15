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
| 48 | Flush Buffers | Supported (writes go straight to the host file, so this is a no-op) |

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
0x005C-0x007F  Default FCBs, the first at 0x005C and the second at 0x006C
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
