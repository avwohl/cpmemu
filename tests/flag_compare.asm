; Very simple flag test - just do a few operations and halt
; We'll use ZSID to examine flags after each operation

        org 0x100

start:
        ; Test 1: ADD that causes overflow
        ; 0x7F + 0x01 = 0x80
        ; Should set: S (result is negative), P/V (overflow from pos to neg)
        ; Should clear: Z, C
        ld a, 0x7F
        add a, 0x01
        nop             ; <-- breakpoint here to examine flags

        ; Test 2: ADD that causes carry
        ; 0xFF + 0x01 = 0x00
        ; Should set: Z, C
        ; Should clear: S, P/V
        ld a, 0xFF
        add a, 0x01
        nop             ; <-- breakpoint here

        ; Test 3: ADD with half-carry
        ; 0x0F + 0x01 = 0x10
        ; Should set: H (half-carry from bit 3)
        ; Should clear: C, S, Z, P/V
        ld a, 0x0F
        add a, 0x01
        nop             ; <-- breakpoint here

        ; Test 4: 16-bit ADD HL that sets half-carry
        ; First, set some initial flags
        ld a, 0xFF      ; Set some flags
        add a, 0x01     ; This sets Z=1, C=1
        ; Now do ADD HL - it should preserve Z
        ld hl, 0x0F00
        ld bc, 0x0100
        add hl, bc      ; HL = 0x1000, should set H, preserve Z from above
        nop             ; <-- breakpoint here

        ; Exit to CP/M
        jp 0

end:
