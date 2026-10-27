; BDOS 28, 29 and 37, then a make on the read-only disk.  Prints VEC when
; the R/O vector follows 28 and 37, BAD when it does not.  The make at the
; end must not return: 2.2 ends the program with "Bdos Err On A: R/O", so
; MADE or ERR after it is a failure too.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
        ld      c,29            ; nothing read-only yet
        call    0005h
        ld      a,h
        or      l
        jr      nz,bad
        ld      c,28            ; write protect A:, the current disk
        call    0005h
        ld      c,29
        call    0005h
        ld      a,h
        or      a
        jr      nz,bad
        ld      a,l
        cp      01h
        jr      nz,bad
        ld      de,0001h        ; reset A: clears its R/O bit
        ld      c,37
        call    0005h
        ld      c,29
        call    0005h
        ld      a,h
        or      l
        jr      nz,bad
        ld      de,vecmsg
        ld      c,9
        call    0005h
        ld      c,28            ; protect A: again, and write to it
        call    0005h
        ld      de,005Ch
        ld      c,22            ; make file
        call    0005h
        cp      0FFh
        ld      de,madmsg
        jr      nz,done
        ld      de,errmsg
        jr      done
bad:    ld      de,badmsg
done:   ld      c,9
        call    0005h
        ld      c,0
        jp      0005h
vecmsg: defb    'VEC$'
badmsg: defb    'BAD$'
madmsg: defb    'MADE$'
errmsg: defb    'ERR$'
