; Put a marker at 0200h, then finish the way the command tail asks:
;   0  BDOS 0 System Reset      W  BIOS WBOOT      J  a jump to 0000h
; All three are a CP/M program finishing, so --save-memory has to write the
; memory image for every one of them.  Before 4.7.1 only the jump did, so the
; flag wrote nothing for a program that ended through BDOS 0 - which is how
; most CP/M programs end.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
        ld      hl,0200h
        ld      (hl),0A5h
        inc     hl
        ld      (hl),05Ah
        ld      a,(0082h)       ; first character of the command tail
        cp      'W'
        jr      z,wboot
        cp      'J'
        jr      z,jmp0
        ld      c,0
        jp      0005h           ; BDOS 0 - System Reset
; jp, not call: a call here would fall through into the WBOOT branch below if
; BDOS 0 ever stopped terminating the guest, and the run would still end with
; an image written - so the check named for BDOS 0 would pass without BDOS 0
; doing anything.  run_tests.sh also greps stderr for the exit each case is
; named after, because a jump to 0000h lands on the JP WBOOT the emulator puts
; there and cannot be isolated from inside the guest at all.
wboot:  jp      0FE03h          ; BIOS WBOOT, the second jump-table entry
jmp0:   jp      0000h
