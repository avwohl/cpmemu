; Print the BDOS 24 login vector as four hex digits.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
        ld      c,24            ; get login vector -> HL
        call    0005h
        ld      a,h
        call    hex8
        ld      a,l
        call    hex8
        ld      c,0
        jp      0005h
hex8:   push    af
        rrca
        rrca
        rrca
        rrca
        call    hex4
        pop     af
hex4:   and     0Fh
        add     a,90h
        daa
        adc     a,40h
        daa
        ld      e,a
        push    hl
        ld      c,2
        call    0005h
        pop     hl
        ret
