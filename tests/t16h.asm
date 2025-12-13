; Test 16-bit flags with HALT

        org 0x100

        ; Test 1
        ld a, 0xFF
        add a, 0x01
        ld hl, 0x0F00
        ld bc, 0x0100
        add hl, bc
        push af
        halt
