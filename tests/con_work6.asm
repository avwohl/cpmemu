; Work, and look for ^C between pieces of work the way the programs HI-TECH C
; builds do (_sigchk in read() and write()): 5000 times, about 4200 T-states of
; work, then BDOS 6 with E = FFh.  A ^C prints "BREAK" and ends; any other byte
; is dropped.  Prints "DONE" at the end.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
        ld      hl,5000
outer:  push    hl
        ld      b,200
work:   ld      a,b
        xor     c
        djnz    work
        ld      c,6
        ld      e,0FFh
        call    0005h
        cp      03h
        jr      z,brk
        pop     hl
        dec     hl
        ld      a,h
        or      l
        jr      nz,outer
        ld      de,done
        ld      c,9
        call    0005h
        jp      0
brk:    ld      de,break
        ld      c,9
        call    0005h
        jp      0
done:   db      'DONE$'
break:  db      'BREAK$'
