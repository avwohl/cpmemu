; Delete the default FCB, which may hold '?'. Prints DEL or ERR.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
        ld      de,005Ch
        ld      c,19            ; delete file
        call    0005h
        cp      0FFh
        ld      de,okmsg
        jr      nz,done
        ld      de,badmsg
done:   ld      c,9
        call    0005h
        ld      c,0
        jp      0005h
okmsg:  defb    'DEL$'
badmsg: defb    'ERR$'
