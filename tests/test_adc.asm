; Test ADC with different carry flag states

        org 0x100

        ; Test 1: ADC with carry=0
        or a            ; Clear carry
        ld a, 0x10
        adc a, 0x20     ; 0x10 + 0x20 + 0 = 0x30
        push af
        halt

        ; Test 2: ADC with carry=1
        scf             ; Set carry
        ld a, 0x10
        adc a, 0x20     ; 0x10 + 0x20 + 1 = 0x31
        push af
        halt
