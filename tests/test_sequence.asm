; Test sequence of operations to see if flags propagate correctly

BDOS    equ 5
CONOUT  equ 2

        org 0x100

        ; Do a sequence of adds and print flags after each
        ld a, 0x00
        add a, 0x01     ; A=01
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        ; Now add again using the result
        ld a, 0x01
        add a, 0x01     ; A=02
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        ; Add with overflow
        ld a, 0x7F
        add a, 0x01     ; A=80, should set P/V
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        ; Now use ADC with the carry that might be set
        ld a, 0x00
        adc a, 0x00     ; A=00 or 01 depending on carry from previous
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
