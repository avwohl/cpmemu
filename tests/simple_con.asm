; Simplest possible console output test

BDOS    equ 5
CONOUT  equ 2

        org 0x100

        ; Just print "ABC" and exit
        ld e, 'A'
        ld c, CONOUT
        call BDOS

        ld e, 'B'
        ld c, CONOUT
        call BDOS

        ld e, 'C'
        ld c, CONOUT
        call BDOS

        rst 0
