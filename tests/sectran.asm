; Call BIOS SECTRAN (0FE30h) through the jump table and print the physical
; sector it left in HL, as four hex digits.  The command tail picks the case:
;   T  BC = 2, DE = the table          H  BC = 0, DE = the table, HL dirtied
;   M  BC = 0, DE = the table          W  DE+BC carries past FFFF
;   A  prints A, not HL                anything else  BC = 1234h, DE = 0
; SECTRAN takes BC = the logical sector and DE = the translate table and
; returns the physical sector in HL: HL = BC when DE is 0, else the byte at
; DE+BC with H = 0.  It shared the CPM_BIOS_DISK stub group, which sets only
; A, so HL came back holding whatever the guest had left in it and the table
; was never read: Z, T, H and M printed 0000, 0000, AA55 and 0000.
; M is the dullest lookup there is - index 0, and HL already clean - so it
; moves for no arithmetic mistake and for no missing H.  That is the point:
; it is the case the CPM_BIOS_DISK mode checks use, so those fail for the
; mode and for nothing else.
; W and A cover the two claims the other four cases cannot see.  W puts the
; table below address 0 so that DE+BC carries past FFFF: the emulator has to
; wrap the sum the way ADD HL,BC does, and a build that lets it grow past
; 64K reads off the end of its own memory array and answers something else.
; A prints the accumulator rather than HL, because a real SECTRAN does not
; touch it and the version that shared the stub group overwrote it with the
; group's status byte - the one part of that bug no HL check can see.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
        ld      a,(0082h)       ; first character of the command tail
        cp      'T'
        jr      z,dotable
        cp      'H'
        jr      z,dodirty
        cp      'W'
        jr      z,dowrap
        cp      'A'
        jr      z,doacc
        cp      'M'
        jr      z,domode
        ld      bc,1234h        ; no table: the answer is BC itself, both bytes
        ld      de,0
        ld      hl,0
        jr      docall
dotable:ld      bc,0002h        ; third entry of the table
        ld      de,xlt
        ld      hl,0
        jr      docall
dodirty:ld      bc,0000h        ; first entry, reached with H already non-zero
        ld      de,xlt
        ld      hl,0AA55h
        jr      docall
dowrap: ld      bc,0203h        ; table + sector carries past FFFF onto xlt+3
        ld      de,xlt-0200h
        ld      hl,0
        jr      docall
doacc:  ld      bc,0000h
        ld      de,xlt
        ld      hl,0
        ld      a,5Ah           ; a sentinel SECTRAN has no business touching
        call    0FE30h          ; BIOS SECTRAN
        call    hexbyte         ; A as SECTRAN left it, and nothing else
        ld      c,0
        jp      0005h
domode: ld      bc,0000h        ; first entry, and nothing else to get wrong
        ld      de,xlt
        ld      hl,0
docall: call    0FE30h          ; BIOS SECTRAN
        push    hl              ; BDOS 2 is free to clobber HL
        ld      a,h
        call    hexbyte
        pop     hl
        ld      a,l
        call    hexbyte
        ld      c,0
        jp      0005h
hexbyte:push    af
        rrca
        rrca
        rrca
        rrca
        call    hexdig
        pop     af
hexdig: and     0Fh
        cp      10
        jr      c,dig
        add     a,'A'-10
        jr      emit
dig:    add     a,'0'
emit:   ld      e,a
        ld      c,2
        jp      0005h
xlt:    defb    06h,07h,08h,09h,0Ah,0Bh
