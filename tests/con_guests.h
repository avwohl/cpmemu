/*
 * The CP/M guest programs the console harnesses run.
 *
 * Shared by tests/win_console.cc and tests/pty_console.cc so the two report
 * comparable byte strings: a case named the same on both platforms runs the
 * same guest, and a fix to one of these programs cannot drift out of the other.
 *
 * There is no assembler in this repo and none on a stock Windows box, so these
 * are hand assembled, the way tests/make_test.sh already produces
 * flag_test.com.  The source each one was assembled from is written out above
 * it.  They are tiny on purpose: every one of them prints the bytes the guest
 * received as hex, so a failure names the wrong byte instead of describing a
 * missing behaviour.
 */

#ifndef CPMEMU_TESTS_CON_GUESTS_H
#define CPMEMU_TESTS_CON_GUESTS_H


// con6hex - BDOS 6 in polled input mode, every byte printed as hex.
// A 0 from BDOS 6 means "no character", so a special key with no translation
// has to leave nothing behind here.  Stops on '.'.
//
//         org     0100h
// main:   ld      c,6
//         ld      e,0ffh          ; polled input
//         call    0005h
//         or      a
//         jr      z,main          ; nothing waiting
//         ld      b,a
//         rrca
//         rrca
//         rrca
//         rrca
//         call    hexdig          ; high nibble
//         ld      a,b
//         call    hexdig          ; low nibble
//         ld      a,' '
//         call    putc
//         ld      a,b
//         cp      '.'
//         jr      nz,main
//         ld      c,0
//         jp      0005h
// hexdig: and     0fh             ; falls into putc
//         add     a,90h
//         daa
//         adc     a,40h
//         daa
// putc:   push    af
//         push    bc
//         ld      e,a
//         ld      c,2
//         call    0005h
//         pop     bc
//         pop     af
//         ret
static const unsigned char con6hex_com[] = {
    0x0E, 0x06, 0x1E, 0xFF, 0xCD, 0x05, 0x00, 0xB7, 0x28, 0xF6, 0x47, 0x0F,
    0x0F, 0x0F, 0x0F, 0xCD, 0x25, 0x01, 0x78, 0xCD, 0x25, 0x01, 0x3E, 0x20,
    0xCD, 0x2D, 0x01, 0x78, 0xFE, 0x2E, 0x20, 0xE0, 0x0E, 0x00, 0xC3, 0x05,
    0x00, 0xE6, 0x0F, 0xC6, 0x90, 0x27, 0xCE, 0x40, 0x27, 0xF5, 0xC5, 0x5F,
    0x0E, 0x02, 0xCD, 0x05, 0x00, 0xC1, 0xF1, 0xC9
};

// con1hex - the same hex echo through BDOS 1, the blocking read.
//
// main:   ld      c,1
//         call    0005h
//         <hex echo, stop on '.', same tail as con6hex>
static const unsigned char con1hex_com[] = {
    0x0E, 0x01, 0xCD, 0x05, 0x00, 0x47, 0x0F, 0x0F, 0x0F, 0x0F, 0xCD, 0x20,
    0x01, 0x78, 0xCD, 0x20, 0x01, 0x3E, 0x20, 0xCD, 0x28, 0x01, 0x78, 0xFE,
    0x2E, 0x20, 0xE5, 0x0E, 0x00, 0xC3, 0x05, 0x00, 0xE6, 0x0F, 0xC6, 0x90,
    0x27, 0xCE, 0x40, 0x27, 0xF5, 0xC5, 0x5F, 0x0E, 0x02, 0xCD, 0x05, 0x00,
    0xC1, 0xF1, 0xC9
};

// con10buf - BDOS 10, the line editor, printing what it stored as hex.
// The echo comes out first, then the buffer contents, so both halves of the
// editor's behaviour are visible in one string.
//
//         ld      a,32
//         ld      (0200h),a       ; buffer length byte
//         ld      de,0200h
//         ld      c,10
//         call    0005h
//         ld      a,(0201h)       ; characters stored
//         ld      b,a
//         ld      hl,0202h
// loop:   ld      a,b
//         or      a
//         jr      z,done
//         ld      a,(hl)
//         push    hl
//         push    bc
//         call    hexbyte
//         pop     bc
//         pop     hl
//         inc     hl
//         dec     b
//         jr      loop
// done:   ld      c,0
//         jp      0005h
// hexbyte: ld     (0300h),a
//         rrca  x4
//         call    hexdig
//         ld      a,(0300h)
//         call    hexdig
//         ld      a,' '
//         jp      putc
static const unsigned char con10buf_com[] = {
    0x3E, 0x20, 0x32, 0x00, 0x02, 0x11, 0x00, 0x02, 0x0E, 0x0A, 0xCD, 0x05,
    0x00, 0x3A, 0x01, 0x02, 0x47, 0x21, 0x02, 0x02, 0x78, 0xB7, 0x28, 0x0C,
    0x7E, 0xE5, 0xC5, 0xCD, 0x29, 0x01, 0xC1, 0xE1, 0x23, 0x05, 0x18, 0xF0,
    0x0E, 0x00, 0xC3, 0x05, 0x00, 0x32, 0x00, 0x03, 0x0F, 0x0F, 0x0F, 0x0F,
    0xCD, 0x3E, 0x01, 0x3A, 0x00, 0x03, 0xCD, 0x3E, 0x01, 0x3E, 0x20, 0xC3,
    0x46, 0x01, 0xE6, 0x0F, 0xC6, 0x90, 0x27, 0xCE, 0x40, 0x27, 0xF5, 0xC5,
    0x5F, 0x0E, 0x02, 0xCD, 0x05, 0x00, 0xC1, 0xF1, 0xC9
};

// bioshex - the same hex echo through the BIOS CONST/CONIN pair, which is a
// separate pair of call sites from BDOS 6 and BDOS 1.
//
// main:   call    0fe06h          ; CONST
//         or      a
//         jr      z,main
//         call    0fe09h          ; CONIN
//         <hex echo, stop on '.'>
static const unsigned char bioshex_com[] = {
    0xCD, 0x06, 0xFE, 0xB7, 0x28, 0xFA, 0xCD, 0x09, 0xFE, 0x47, 0x0F, 0x0F,
    0x0F, 0x0F, 0xCD, 0x24, 0x01, 0x78, 0xCD, 0x24, 0x01, 0x3E, 0x20, 0xCD,
    0x2C, 0x01, 0x78, 0xFE, 0x2E, 0x20, 0xE1, 0x0E, 0x00, 0xC3, 0x05, 0x00,
    0xE6, 0x0F, 0xC6, 0x90, 0x27, 0xCE, 0x40, 0x27, 0xF5, 0xC5, 0x5F, 0x0E,
    0x02, 0xCD, 0x05, 0x00, 0xC1, 0xF1, 0xC9
};

// conststall - the shape that catches a lying status call.  It polls CONST a
// bounded number of times and prints T when the polls run out, so "the status
// said a key was ready and the read then blocked" reads as no output at all
// rather than as a test that merely took a while.
//
// The blocking CONIN in front of the loop is what makes the case honest.  The
// bounded loop runs out in well under a millisecond, so without it the poll
// could finish before the harness had typed anything, and the case would print
// T and pass without ever seeing the key it is about.  Blocking first means
// every key of the script is already in the console buffer when the poll
// starts, whatever the machine's timing.  The cases below all send one
// throwaway key to release it.
//
//         call    0fe09h          ; CONIN, discarded: a start line
//         ld      hl,0            ; 65536 polls
// poll:   push    hl
//         call    0fe06h          ; CONST
//         pop     hl
//         or      a
//         jr      nz,gotkey
//         dec     hl
//         ld      a,h
//         or      l
//         jr      nz,poll
//         ld      a,'T'           ; polls ran out, nothing was ever ready
//         call    putc
//         ld      c,0
//         jp      0005h
// gotkey: call    0fe09h          ; CONIN
//         <hex echo, back to poll, stop on '.'>
static const unsigned char conststall_com[] = {
    0xCD, 0x09, 0xFE, 0x21, 0x00, 0x00, 0xE5, 0xCD, 0x06, 0xFE, 0xE1, 0xB7,
    0x20, 0x0F, 0x2B, 0x7C, 0xB5, 0x20, 0xF3, 0x3E, 0x54, 0xCD, 0x43, 0x01,
    0x0E, 0x00, 0xC3, 0x05, 0x00, 0xCD, 0x09, 0xFE, 0x47, 0x0F, 0x0F, 0x0F,
    0x0F, 0xCD, 0x3B, 0x01, 0x78, 0xCD, 0x3B, 0x01, 0x3E, 0x20, 0xCD, 0x43,
    0x01, 0x78, 0xFE, 0x2E, 0x20, 0xD0, 0x0E, 0x00, 0xC3, 0x05, 0x00, 0xE6,
    0x0F, 0xC6, 0x90, 0x27, 0xCE, 0x40, 0x27, 0xF5, 0xC5, 0x5F, 0x0E, 0x02,
    0xCD, 0x05, 0x00, 0xC1, 0xF1, 0xC9
};

// coneof - BDOS 1 hex echo that stops on ^Z, so end of input ends it.  This is
// the shape tests/con_eof.asm has on the Linux side, and it expects the same
// two bytes: CR once, so a part typed line submits, then ^Z.
//
// main:   ld      c,1
//         call    0005h
//         <hex echo, stop on 1ah rather than '.'>
static const unsigned char coneof_com[] = {
    0x0E, 0x01, 0xCD, 0x05, 0x00, 0x47, 0x0F, 0x0F, 0x0F, 0x0F, 0xCD, 0x20,
    0x01, 0x78, 0xCD, 0x20, 0x01, 0x3E, 0x20, 0xCD, 0x28, 0x01, 0x78, 0xFE,
    0x1A, 0x20, 0xE5, 0x0E, 0x00, 0xC3, 0x05, 0x00, 0xE6, 0x0F, 0xC6, 0x90,
    0x27, 0xCE, 0x40, 0x27, 0xF5, 0xC5, 0x5F, 0x0E, 0x02, 0xCD, 0x05, 0x00,
    0xC1, 0xF1, 0xC9
};

// crlf - send CR LF and stop.  Windows doubles a CR on the way out unless
// platform::init() has put stdout in binary mode.
//
//         ld      e,13
//         ld      c,2
//         call    0005h
//         ld      e,10
//         ld      c,2
//         call    0005h
//         ld      c,0
//         jp      0005h
static const unsigned char crlf_com[] = {
    0x1E, 0x0D, 0x0E, 0x02, 0xCD, 0x05, 0x00, 0x1E, 0x0A, 0x0E, 0x02, 0xCD,
    0x05, 0x00, 0x0E, 0x00, 0xC3, 0x05, 0x00
};

#endif  // CPMEMU_TESTS_CON_GUESTS_H
