; Open the default FCB, read one record, print the first three bytes. Prints NF if the open fails.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
        ld      de,005Ch
        ld      c,15            ; open
        call    0005h
        cp      0FFh
        jr      z,nf
        ld      de,005Ch
        ld      c,20            ; read sequential -> DMA (0080h)
        call    0005h
        or      a
        jr      nz,nf
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
