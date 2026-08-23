; Select drive B via BDOS 14, then open a dr=0 HELLO.TXT and print three bytes.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
        ld      e,1             ; BDOS 14: select drive B (0-based)
        ld      c,14
        call    0005h
        ld      hl,fcb          ; copy a dr=0 FCB for HELLO.TXT to 005Ch
        ld      de,005Ch
        ld      bc,36
        ldir
        ld      de,005Ch
        ld      c,15
        call    0005h
        cp      0FFh
        jr      z,nf
        ld      de,005Ch
        ld      c,20
        call    0005h
        ld      hl,0080h
        ld      b,3
pr:     ld      a,(hl)
        push    hl
        push    bc
        ld      e,a
        ld      c,2
        call    0005h
        pop     bc
        pop     hl
        inc     hl
        djnz    pr
        jr      done
nf:     ld      de,nfmsg
        ld      c,9
        call    0005h
done:   ld      c,0
        jp      0005h
nfmsg:  defb    'NF$'
fcb:    defb    0               ; dr = 0 -> current drive
        defb    'HELLO   '
        defb    'TXT'
        defs    24,0
