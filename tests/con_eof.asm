; Read BDOS 1 and print each byte as hex until ^Z arrives.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
main:   ld      c,1
        call    0005h
        ld      b,a
        rrca
        rrca
        rrca
        rrca
        call    hexdig
        ld      a,b
        call    hexdig
        ld      a,' '
        call    putc
        ld      a,b
        cp      1ah
        jr      nz,main
        ld      c,0
        jp      0005h
hexdig: and     0fh
        add     a,90h
        daa
        adc     a,40h
        daa
putc:   push    af
        push    bc
        ld      e,a
        ld      c,2
        call    0005h
        pop     bc
        pop     af
        ret
