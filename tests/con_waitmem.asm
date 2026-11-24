; Wait for a key and count the polls in memory, the way a BASIC INKEY$ loop
; or a blinking cursor changes memory as it waits: BDOS 11 until a key is
; ready, then BDOS 1.  Each byte is printed as hex, and ^Z prints "END" and
; ends.  The state of this guest never repeats, so only the probe - "a key
; is ready" at every 16384th status call - tells it that input has ended.
; It must still get CR and then ^Z.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
loop:   ld      hl,(count)
        inc     hl
        ld      (count),hl
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
count:  dw      0
endmsg: db      'END$'
