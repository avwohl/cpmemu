; Test 16-bit ADD HL flags

BDOS    equ 5
CONOUT  equ 2

        org 0x100

        ; Test 1: ADD HL with half-carry
        ; First set some flags
        ld a, 0xFF
        add a, 0x01      ; Sets Z=1, C=1
        ; Now ADD HL should preserve Z
        ld hl, 0x0F00
        ld bc, 0x0100
        add hl, bc       ; HL = 0x1000, should preserve Z=1
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        ; Test 2: ADD HL with carry
        ld a, 0x00       ; Clear flags
        add a, 0x00
        ld hl, 0x8000
        ld bc, 0x8000
        add hl, bc       ; HL = 0x0000, should set C
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        ; Exit
        rst 0

; Print A as hex
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
