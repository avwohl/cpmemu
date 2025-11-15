; Print flags after each operation so we can compare
; Works on both our emulator and tnylpo

BDOS    equ 5
CONOUT  equ 2

        org 0x100

        ; Test 1: 0x7F + 0x01 = 0x80 (overflow)
        ld de, msg1
        call print_string
        ld a, 0x7F
        add a, 0x01
        call print_flags_byte
        call print_newline

        ; Test 2: 0xFF + 0x01 = 0x00 (carry, zero)
        ld de, msg2
        call print_string
        ld a, 0xFF
        add a, 0x01
        call print_flags_byte
        call print_newline

        ; Test 3: 0x0F + 0x01 = 0x10 (half-carry)
        ld de, msg3
        call print_string
        ld a, 0x0F
        add a, 0x01
        call print_flags_byte
        call print_newline

        ; Test 4: 0x80 - 0x01 = 0x7F (subtract overflow)
        ld de, msg4
        call print_string
        ld a, 0x80
        sub 0x01
        call print_flags_byte
        call print_newline

        ; Exit to CP/M
        ld c, 0
        call BDOS
        ret

; Print flags from current AF
; Preserves all registers except AF
print_flags_byte:
        push af
        push bc
        push de
        push hl

        ; Get flags into A
        pop hl          ; Discard HL
        pop hl          ; Discard DE
        pop hl          ; Discard BC
        pop hl          ; HL now has original AF
        push hl
        push hl
        push hl
        push hl

        ld a, l         ; A = flags byte
        call print_hex

        pop hl
        pop de
        pop bc
        pop af
        ret

; Print string pointed to by DE (terminated by $)
print_string:
        push af
        push de
ps_loop:
        ld a, (de)
        cp '$'
        jr z, ps_done
        push de
        ld e, a
        ld c, CONOUT
        call BDOS
        pop de
        inc de
        jr ps_loop
ps_done:
        pop de
        pop af
        ret

; Print A as hex
print_hex:
        push af
        push bc
        push de

        ; Print high nibble
        ld d, a
        rrca
        rrca
        rrca
        rrca
        and 0x0F
        call print_nibble

        ; Print low nibble
        ld a, d
        and 0x0F
        call print_nibble

        pop de
        pop bc
        pop af
        ret

print_nibble:
        add a, '0'
        cp ':'
        jr c, pn1
        add a, 7
pn1:    ld e, a
        ld c, CONOUT
        call BDOS
        ret

print_newline:
        push af
        push bc
        push de
        ld e, 13
        ld c, CONOUT
        call BDOS
        ld e, 10
        ld c, CONOUT
        call BDOS
        pop de
        pop bc
        pop af
        ret

msg1:   db "Test1(7F+01): $"
msg2:   db "Test2(FF+01): $"
msg3:   db "Test3(0F+01): $"
msg4:   db "Test4(80-01): $"
