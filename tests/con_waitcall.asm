; Wait for a key and ask for the current drive (BDOS 25) each time round:
; BDOS 25, BDOS 11, and BDOS 1 when a key is ready.  Each byte is printed as
; hex, and ^Z prints "END" and ends.  BDOS 25 touches no file, so it is not
; work, and at the end of input this guest must still get CR and then ^Z.
; For a while any call but a console one counted as work, and this guest
; then waited for ever.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
loop:   ld      c,25
        call    0005h
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
