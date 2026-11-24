; Work, and look for ^C between pieces of work the way MBASIC does while a
; BASIC program runs: 5000 times, about 4200 T-states of work, then BIOS CONST
; and, when it says a key is ready, BIOS CONIN.  A ^C prints "BREAK" and ends;
; any other byte is dropped.  Prints "DONE" at the end.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
        ld      hl,5000
outer:  push    hl
        ld      b,200
work:   ld      a,b
        xor     c
        djnz    work
        call    const
        or      a
        jr      z,next
        call    conin
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
const:  ld      hl,(0001h)      ; the WBOOT entry; CONST is the one after it
        ld      de,3
        add     hl,de
        jp      (hl)
conin:  ld      hl,(0001h)
        ld      de,6
        add     hl,de
        jp      (hl)
done:   db      'DONE$'
break:  db      'BREAK$'
