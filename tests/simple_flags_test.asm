; Simple flag test - compare specific operations
; Print flag values after each operation for comparison with tnylpo

        org 0x100

        ; Test 1: Simple 8-bit ADD
        ld a, 0x7F      ; 127
        ld b, 0x01      ; 1
        add a, b        ; Result: 0x80 (-128), should set S, clear C, set P/V (overflow)
        push af
        pop bc          ; BC now has AF (A=result, C=flags)
        ld a, c         ; A = flags
        call print_hex
        call print_nl

        ; Test 2: 8-bit ADD with carry
        ld a, 0xFF      ; 255
        ld b, 0x01      ; 1
        add a, b        ; Result: 0x00, should set Z, set C, clear S
        push af
        pop bc
        ld a, c
        call print_hex
        call print_nl

        ; Test 3: 16-bit ADD HL (should preserve S,Z,P/V)
        ld hl, 0x0F00
        ld bc, 0x0100
        or a            ; Clear carry
        ld a, 0x55      ; Set some flags: S=0, Z=0, P=1
        push af
        pop af
        add hl, bc      ; Result: 0x1000, should set H, preserve S/Z/P
        push af
        pop bc
        ld a, c
        call print_hex
        call print_nl

        ; Exit
        ld c, 0
        call 5
        ret

; Print hex value in A
print_hex:
        push af
        rrca
        rrca
        rrca
        rrca
        and 0x0F
        call print_nibble
        pop af
        and 0x0F
        call print_nibble
        ret

print_nibble:
        add a, '0'
        cp ':'
        jr c, print_char
        add a, 7
print_char:
        ld e, a
        ld c, 2
        call 5
        ret

print_nl:
        ld e, 13
        ld c, 2
        call 5
        ld e, 10
        ld c, 2
        call 5
        ret
