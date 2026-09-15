# Console input is seven bits

Six read sites mask incoming bytes with `& 0x7F` - four console sites and the
two Reader sites below - so no byte at or above 0x80 reaches the guest intact. This is deliberate and permanent; the
README states the rule, and this file records what it costs and why it is not
going to change.

## The six sites

BDOS 1, the BDOS 10 buffer store, BDOS 6 and BIOS CONIN mask with `& 0x7F`.
BDOS 3 and BIOS READER mask the same way, which makes six in all; those two are
the Reader device, which is seven-bit in both directions. The layers below do keep the eighth bit: POSIX
raw mode clears ISTRIP, and the Windows console path encodes the key in the
console input code page. The mask is above them. Measured: the UTF-8 for
e-acute, alpha and `A` piped at a hex-echo guest comes back as `43 29 4E 31 41`.

This is deliberate and it is not going to change. A CP/M program written for
this hardware expects seven bits, and the two things eight bits would buy —
accented characters and a byte-exact console — are not what the software in
this emulator's reach does. Dropping the masks is also not a handful of line deletions:
BDOS 6 spells "no character" as 0, so it would need another way to say it, and
the code-page expectations in `tests/win_console.cc` are written against the
masked bytes.

## What it costs, measured


- On the polled path a byte that masks to 0x00 is dropped outright, because
  BDOS 6 reads 0 as "no character". Two input bytes do this, 0x00 and 0x80, and
  the byte is consumed rather than left waiting: `80 41` at a BDOS 6 guest
  yields `41` alone. CP/M 2.2 already spells "no character" 0, so a genuine NUL
  is ambiguous on real hardware too; what this emulator adds is the second byte.
- A guest that polls BIOS CONST until it says a key is ready and only then calls
  BDOS 6 gets 0 back for a typed 0x80. Status and read contradict each other.
- The mask is applied to what gets stored, after every raw-byte test, so a raw
  0x8D is stored as CR rather than ending a BDOS 10 line early, a raw 0xFF is
  stored as 0x7F rather than acting as rubout, and five raw 0x83 reach the guest
  as five `03` without tripping the five-^C exit.

## Where it is pinned

The seven-bit cases in `tests/pty_console.cc` pin the console sites, over a pty
and - for the blocking read, where it separates this mask from a line discipline
that strips the bit - over a pipe as well, so the answer cannot change by
accident. The two Reader sites are documented here but asserted by nothing.
