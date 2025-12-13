; Test 1: ADD that causes overflow
; 0x7F + 0x01 = 0x80
; Expected flags: S=1 (negative), Z=0, P/V=1 (overflow), H=0, N=0, C=0

        org 0x100

        ld a, 0x7F
        add a, 0x01     ; A = 0x80
        push af         ; Push for ZSID examination
        halt            ; Our emulator will print flags
