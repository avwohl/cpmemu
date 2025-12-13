; Print flag values after each operation for easy comparison

        org 0x100

start:
        ; Test 1: 0x7F + 0x01 = 0x80 (overflow)
        ld a, 0x7F
        add a, 0x01
        push af
        pop hl
        ld a, l
        call print_hex

        ; Test 2: 0xFF + 0x01 = 0x00 (carry, zero)
        ld a, 0xFF
        add a, 0x01
        push af
        pop hl
        ld a, l
        call print_hex

        ; Test 3: 0x0F + 0x01 = 0x10 (half-carry)
        ld a, 0x0F
        add a, 0x01
        push af
        pop hl
        ld a, l
        call print_hex

        ; Test 4: 0x80 + 0x80 = 0x00 (carry, overflow, zero)
        ld a, 0x80
        add a, 0x80
        push af
        pop hl
        ld a, l
        call print_hex

        ; Exit
        rst 0

; Print A as hex
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
        ; Print newline
        ld e, 13
        ld c, 2
        call 5
        ld e, 10
        ld c, 2
        call 5
        ret

print_nibble:
        add a, '0'
        cp ':'
        jr c, pn1
        add a, 7
pn1:    ld e, a
        ld c, 2
        call 5
        ret
