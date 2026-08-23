; Make and close the default FCB. Prints MADE or ERR.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
        ld      de,005Ch
        ld      c,22            ; make file
        call    0005h
        cp      0FFh
        jr      z,bad
        ld      de,005Ch
        ld      c,16            ; close
        call    0005h
        ld      de,okmsg
        jr      done
bad:    ld      de,badmsg
done:   ld      c,9
        call    0005h
        ld      c,0
        jp      0005h
okmsg:  defb    'MADE$'
badmsg: defb    'ERR$'
