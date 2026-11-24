; Work, and look for ^C between pieces of work the way HI-TECH C's passes do:
; 5000 times, about 4200 T-states of work, then BDOS 11 and, when it says a key
; is ready, BDOS 1.  A ^C prints "BREAK" and ends; any other byte is dropped.
; Prints "DONE" at the end.  Once redirected input has ended, eof_idle has to
; answer this guest "no key", or its ^C checks read past the end of input and
; the 1024-read stop ends it part way.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
        ld      hl,5000
outer:  push    hl
        ld      b,200
work:   ld      a,b
        xor     c
        djnz    work
        ld      c,11
        call    0005h
        or      a
        jr      z,next
        ld      c,1
        call    0005h
        cp      03h
        jr      z,brk
next:   pop     hl
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
