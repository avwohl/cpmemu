; Simplest flag test - just print hex values after operations
; No conditional jumps to keep it simple

BDOS    equ 5
CONOUT  equ 2

        org 0x100

        ; Test 1
        ld a, 0x7F
        add a, 0x01
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        ; Test 2
        ld a, 0xFF
        add a, 0x01
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        ; Test 3
        ld a, 0x0F
        add a, 0x01
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        ; Test 4
        ld a, 0x80
        sub 0x01
        push af
        pop hl
        ld a, l
        call phex
        call pnl

        ; Exit
        rst 0   ; Warm boot

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
