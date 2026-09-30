# Console and keyboard

## The ^C escape hatch

Five consecutive ^C within two seconds exit the emulator, writing the
`--save-memory` image first if one was requested. The hatch exists because the
emulator takes ^C away from the host - ISIG cleared on POSIX,
`ENABLE_PROCESSED_INPUT` cleared on Windows - so ^C reaches the guest instead.

Turn it off with `--no-ctrl-c-exit`, or `ctrl_c_exit = false` in the config,
when the guest binds ^C itself: WordStar binds it to page-down, and five
page-downs in a row would otherwise kill the emulator.

On Windows ctrl+break is the other way out, since it is not gated on
`ENABLE_PROCESSED_INPUT`. It writes no `--save-memory` image.

ISIG is also cleared, so ^Z and ^\ are guest characters and the emulator cannot
be suspended from the keyboard; `kill -TSTP` and `fg` still work, and the
terminal is restored on HUP, INT, QUIT, TERM and the three crash signals.

## Line editing (BDOS function 10)

| Key | Action |
|-----|--------|
| RUB / ^H | Delete the previous character |
| ^U | Cancel the line, echo `#` and start a new one |
| ^X | Cancel the current physical line, erasing it from the screen |
| ^E | Start a new physical line, keep collecting |
| ^R | Echo `#` and retype the line so far on a new line |
| ^P | Toggle console echo to the printer; nothing unless a printer file is configured |
| ^S | Ignored |
| CR / LF | End the line |

Every other character below 0x80 reaches the program: printable ones as
themselves, control characters (TAB and ESC included) stored in the buffer and
echoed as `^x`.

^C is not special here. Real CP/M 2.2 warm boots on a ^C in column one; a warm
boot in this emulator is program termination, so ^C is stored and echoed like
any other control character and the program decides what it means. It still
counts toward the five-^C exit.

## Terminal output

Console output is translated from ADM-3A, the terminal most CP/M software was
written for, to ANSI. The translation is always on; there is no flag to turn it
off.

```
ESC *        clear the screen and home the cursor
ESC T        clear to end of line
ESC Y        clear to end of screen
ESC )        reverse video on
ESC (        reverse video off
ESC G n      Kaypro/Televideo attribute: 0 normal, 4 reverse, 2 dim,
             1 underline; anything else resets
ESC = r c    cursor to row r, column c, each byte biased by 32
^Z           clear the screen and home
^^           home the cursor
^K           cursor up
^L           cursor right
```

BS, BEL, CR and LF pass through. An escape sequence not in the table passes
through as ESC plus the character that followed it. A control character below
0x20 that is not in the table is dropped, **TAB included**, so a program that
lays out columns with tabs loses them; DEL (0x7F) is written out. The high bit
is stripped from every byte, as on a real 7-bit console.

This covers BDOS 2, BDOS 6 output, BDOS 9 and BIOS CONOUT. It does not cover the
function 10 line editor's echo, which is the user's own keystrokes coming back
in the `^x` form above.

## Windows special keys

On Windows a special key is translated to the WordStar diamond control code:

| Key | Code | Key | Code | Key | Code |
|-----|------|-----|------|-----|------|
| Up | ^E | Home | ^Q^S | Insert | ^V |
| Down | ^X | End | ^Q^D | Delete | ^G |
| Left | ^S | PgUp | ^R | Ctrl+Left | ^A |
| Right | ^D | PgDn | ^C | Ctrl+Right | ^F |
| | | | | Ctrl+Up | ^W |
| | | | | Ctrl+Down | ^Z |

A special key not in the table is swallowed. A translated PgDn does not count
toward the ^C exit. At a function 10 prompt the translated codes are given to
the program rather than obeyed as line-editing commands.

**There is no equivalent on Linux or macOS.** A POSIX terminal hands over the
raw escape sequence and nothing translates it, so Up reaches the guest as
`ESC [ A` and at a function 10 prompt is echoed as `^[[A`. A guest that wants
arrow keys there has to decode the sequence itself.

## Known limitations

**Console input is seven bits.** The four console read sites - BDOS 1, the
BDOS 10 buffer store, BDOS 6 and BIOS CONIN - mask with `& 0x7F`, so no byte at
or above 0x80 reaches the guest intact. BDOS 3 and BIOS READER mask the same
way, which makes six read sites in all; those two are the Reader device. This is deliberate and is
not going to change: CP/M software expects seven bits, and BDOS 6 spells "no
character" as 0, so a byte that masks to 0x00 is dropped outright.
[console_seven_bit.md](console_seven_bit.md) has the measurements and
the consequences.

**Ctrl+V cannot reach the guest under Windows Terminal**, which binds it to
paste and does not fall through
([microsoft/terminal#16280](https://github.com/microsoft/terminal/issues/16280),
still open). No `SetConsoleMode` call changes this. Press **Insert**, which the
emulator translates to ^V, or unbind the key in `settings.json`:

```json
{ "keys": "ctrl+v", "command": "unbound" }
```

A Windows Terminal fragment extension cannot do this for you - fragments may
contribute profiles and colour schemes only. ^V reaches the guest normally on
Linux and macOS.

**Ctrl+C is shadowed by an active selection** on the same host. The emulator
clears `ENABLE_QUICK_EDIT_MODE`, so a stray click no longer starts one, but a
deliberate selection still shadows ^C until Esc clears it.
