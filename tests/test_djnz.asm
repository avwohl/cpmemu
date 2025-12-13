; Test DJNZ instruction

BDOS    equ 5
CONOUT  equ 2

        org 0x100

        ; Simple DJNZ loop - count from 3 down to 0
        ld b, 3
loop:
        ld a, b
        add a, '0'      ; Convert to ASCII
        ld e, a
        ld c, CONOUT
        call BDOS

        djnz loop       ; Decrement B, jump if not zero

        ; Print newline
        ld e, 13
        ld c, CONOUT
        call BDOS
        ld e, 10
        ld c, CONOUT
        call BDOS

        rst 0
