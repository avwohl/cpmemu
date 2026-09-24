; Print the two default FCBs as the program finds them at start:
;
;   dd[nnnnnnnnttt]dd[nnnnnnnnttt]cc
;
; the drive byte, name and type of the FCB at 005Ch, the same for the one at
; 006Ch, and the CR byte at 007Ch, each byte as two hex digits except the
; eleven name and type bytes, which print as themselves, or as . when they
; are not printable.  The CCP fills both names whether or not the command line
; had them, blank when it did not; DRI's ED tests that the second one is blank
; and refuses to start when it is not.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
        ld      hl,005Ch
        call    onefcb
        ld      hl,006Ch
        call    onefcb
        ld      a,(007Ch)
        call    hex8
        ld      c,0
        jp      0005h

onefcb: ld      a,(hl)          ; drive
        inc     hl
        call    hex8
        ld      a,'['
        call    putc
        ld      b,11
name:   ld      a,(hl)
        inc     hl
        cp      ' '
        jr      c,dot
        cp      7Fh
        jr      c,show
dot:    ld      a,'.'
show:   call    putc
        djnz    name
        ld      a,']'
        jp      putc

hex8:   push    af
        rrca
        rrca
        rrca
        rrca
        call    hex4
        pop     af
hex4:   and     0Fh
        add     a,90h
        daa
        adc     a,40h
        daa
putc:   push    hl
        push    bc
        ld      e,a
        ld      c,2
        call    0005h
        pop     bc
        pop     hl
        ret
