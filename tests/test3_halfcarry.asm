; Test 3: ADD that causes half-carry
; 0x0F + 0x01 = 0x10
; Expected flags: S=0, Z=0, P/V=0, H=1 (half-carry from bit 3), N=0, C=0

        org 0x100

        ld a, 0x0F
        add a, 0x01     ; A = 0x10
        push af
        halt
