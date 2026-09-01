; Call a BIOS disk function through the jump table and print the status byte it
; left in A, as two hex digits.  The command tail picks the call:
;   R  READ (0FE27h)      W  WRITE (0FE2Ah)      anything else  HOME (0FE18h)
; CPM_BIOS_DISK=ok has to print 00 and CPM_BIOS_DISK=fail has to print 01.
; Before 4.7.1 both printed 00: the fail branch set A to 0, the same byte the
; ok branch sets, so the mode was invisible to the guest.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
        ld      a,(0082h)       ; first character of the command tail
        cp      'W'
        jr      z,dowrite
        cp      'R'
        jr      z,doread
        call    0FE18h          ; BIOS HOME
        jr      show
doread: call    0FE27h          ; BIOS READ
        jr      show
dowrite:call    0FE2Ah          ; BIOS WRITE
show:   push    af
        rrca
        rrca
        rrca
        rrca
        call    hexdig
        pop     af
        call    hexdig
        ld      c,0
        jp      0005h
hexdig: and     0Fh
        cp      10
        jr      c,dig
        add     a,'A'-10
        jr      emit
dig:    add     a,'0'
emit:   ld      e,a
        ld      c,2
        jp      0005h
