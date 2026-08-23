; Search First/Next over ????????.??? and print each 11-byte directory name.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
        ld      hl,005Dh        ; FCB name+ext = '???????????'
        ld      b,11
fillq:  ld      (hl),'?'
        inc     hl
        djnz    fillq
        ld      de,005Ch
        ld      c,17            ; search first
        call    0005h
loop:   cp      0FFh
        jr      z,done
        ld      hl,0081h        ; DMA+1, skip the user byte
        ld      b,11
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
        ld      e,0Dh
        ld      c,2
        call    0005h
        ld      e,0Ah
        ld      c,2
        call    0005h
        ld      de,005Ch
        ld      c,18            ; search next
        call    0005h
        jr      loop
done:   ld      c,0
        jp      0005h
