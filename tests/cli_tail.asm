; Print the CP/M command tail at 0080h, or (empty).
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
        ld      a,(0080h)       ; command tail length
        or      a
        jr      z,empty
        ld      c,a
        ld      b,0
        ld      hl,0081h
pr:     ld      a,(hl)
        push    hl
        push    bc
        ld      e,a
        ld      c,2
        call    0005h
        pop     bc
        pop     hl
        inc     hl
        dec     c
        jr      nz,pr
        jr      done
empty:  ld      de,emsg
        ld      c,9
        call    0005h
done:   ld      c,0
        jp      0005h
emsg:   defb    '(empty)$'
