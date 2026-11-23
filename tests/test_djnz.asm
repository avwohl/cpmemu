; Test DJNZ instruction
;
; B is the loop counter, and it is saved around the BDOS call.  A BDOS call
; does not keep B: CP/M 2.2 returns with B = H, so B is 0 after every call
; that returns an 8-bit result.  This program used to keep B across the call
; and print "321" only because the emulator left B alone; with the 2.2
; return it looped for ever.
;
; Committed as test_djnz.com (and the same bytes as test_djnz.bin), built
; with:  um80 -o test_djnz.rel test_djnz.asm && ul80 -o test_djnz.com test_djnz.rel

BDOS    equ 5
CONOUT  equ 2

	.z80
        ; Simple DJNZ loop - count from 3 down to 0
        ld b, 3
loop:
        ld a, b
        add a, '0'      ; Convert to ASCII
        ld e, a
        ld c, CONOUT
        push bc         ; the BDOS returns B = H, not the caller's B
        call BDOS
        pop bc

        djnz loop       ; Decrement B, jump if not zero

        ; Print newline
        ld e, 13
        ld c, CONOUT
        call BDOS
        ld e, 10
        ld c, CONOUT
        call BDOS

        rst 0
