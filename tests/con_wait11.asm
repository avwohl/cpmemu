; Wait for a key with a delay between polls: BDOS 11, and when no key is
; ready, about 2600 T-states of delay and BDOS 11 again.  When a key is ready,
; BDOS 1 reads it; each byte is printed as hex, and ^Z prints "END" and ends.
; The delay is longer than the gap between MBASIC's ^C checks, so only the
; guest's state can tell this waiter from a worker: its registers and memory
; are the same at every poll.  At the end of input it must still get CR and
; then ^Z, as it did before eof_idle, and not "no key" for ever.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
loop:   ld      b,200
delay:  djnz    delay
        ld      c,11
        call    0005h
        or      a
        jr      z,loop
        ld      c,1
        call    0005h
        cp      1Ah
        jr      z,fin
        push    af              ; BDOS 2 returns B = 0, as 2.2 does
        rrca
        rrca
        rrca
        rrca
        call    hexdig
        pop     af
        call    hexdig
        ld      e,' '
        ld      c,2
        call    0005h
        jr      loop
fin:    ld      de,endmsg
        ld      c,9
        call    0005h
        jp      0
hexdig: and     0Fh
        add     a,'0'
        cp      '9'+1
        jr      c,hd1
        add     a,7
hd1:    ld      e,a
        ld      c,2
        jp      0005h
endmsg: db      'END$'
