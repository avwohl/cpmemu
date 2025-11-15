; Test 4: SUB that causes overflow
; 0x80 - 0x01 = 0x7F
; Expected flags: S=0 (positive), Z=0, P/V=1 (overflow neg to pos), H=1, N=1 (subtract), C=0

        org 0x100

        ld a, 0x80
        sub 0x01        ; A = 0x7F
        push af
        halt
