; Rename the FCB at 005Ch to the name at 006Ch. Prints REN or ERR.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
        ld      de,005Ch        ; 005C holds old name, 006C holds new name
        ld      c,23
        call    0005h
        cp      0FFh
        ld      de,okmsg
        jr      nz,done
        ld      de,badmsg
done:   ld      c,9
        call    0005h
        ld      c,0
        jp      0005h
okmsg:  defb    'REN$'
badmsg: defb    'ERR$'
