; Test 16-bit flags - carry case

        org 0x100

        ld a, 0x00
        add a, 0x00
        ld hl, 0x8000
        ld bc, 0x8000
        add hl, bc
        push af
        halt
