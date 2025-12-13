; Test BDOS return mechanism

BDOS    equ 5
CONOUT  equ 2

        org 0x100

start:
        ld e, 'A'
        ld c, CONOUT
        call BDOS          ; First BDOS call

        nop                ; Should get here
        nop

        ld e, 'B'
        ld c, CONOUT
        call BDOS          ; Second BDOS call

        nop                ; Should get here too
        nop

        rst 0              ; Now exit
