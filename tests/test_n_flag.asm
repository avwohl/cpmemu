; Test N flag is set/cleared correctly

BDOS    equ 5
CONOUT  equ 2

        org 0x100

        ; Test ADD clears N
        ld a, 0x10
        add a, 0x20
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        ; Test SUB sets N
        ld a, 0x20
        sub 0x10
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        ; Test ADD clears N even after SUB
        ld a, 0x10
        sub 0x05        ; N=1
        ld a, 0x10
        add a, 0x05     ; Should clear N
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        rst 0

phex:
        push af
        rrca
        rrca
        rrca
        rrca
        and 0x0F
        call pnib
        pop af
        and 0x0F
        call pnib
        ret

pnib:
        add a, 0x30
        cp 0x3A
        jp c, pnib1
        add a, 7
pnib1:  ld e, a
        ld c, CONOUT
        call BDOS
        ret

pnl:
        push af
        push de
        push bc
        ld e, 13
        ld c, CONOUT
        call BDOS
        ld e, 10
        ld c, CONOUT
        call BDOS
        pop bc
        pop de
        pop af
        ret
