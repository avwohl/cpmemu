; Test 2: ADD that causes carry and zero
; 0xFF + 0x01 = 0x00
; Expected flags: S=0, Z=1 (zero), P/V=0 (no overflow), H=1 (half-carry), N=0, C=1 (carry)

        org 0x100

        ld a, 0xFF
        add a, 0x01     ; A = 0x00
        push af
        halt
