; A jump to 0000h runs the JP that is there, the way a real machine runs it.
; This program takes the warm boot over twice, prints a digit each time it
; gets control back, then gives the warm boot back and ends through it:
;   1  the address at 0001h points here.  HI-TECH C's $EXEC does this to get
;      control back after each compiler pass, and it points the BDOS vector
;      at 0006h at a JP of its own as well, so this does both and prints the
;      digits through that JP.
;   2  0001h is put back, and the WBOOT entry of the BIOS jump table - the JP
;      that 0001h names - points here instead.
; The output is "12".  The last jump to 0000h finds both put back, so the
; emulator's trap ends the run with "Program exit via JMP 0".  Until issue #5
; that trap ended the run at every jump to 0000h, so the program printed
; nothing.  A trap that let the jump through only when the word at 0001h had
; changed would print "1" and end the run at the second jump.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
        ld      hl,(0006h)      ; the BDOS vector, as $EXEC takes it
        ld      (bdosjp+1),hl
        ld      hl,bdosjp
        ld      (0006h),hl
        ld      hl,(0001h)      ; case 1: the word at 0001h
        ld      (wbsave),hl
        ld      hl,back1
        ld      (0001h),hl
        jp      0000h
back1:  ld      e,'1'
        ld      c,2
        call    0005h
        ld      hl,(wbsave)
        ld      (0001h),hl
        inc     hl              ; case 2: the target of the WBOOT entry's JP
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ld      (tabsave),de
        ld      de,back2
        ld      (hl),d
        dec     hl
        ld      (hl),e
        jp      0000h
back2:  ld      e,'2'
        ld      c,2
        call    0005h
        ld      hl,(0001h)      ; give the WBOOT entry back
        inc     hl
        ld      de,(tabsave)
        ld      (hl),e
        inc     hl
        ld      (hl),d
        ld      hl,(bdosjp+1)   ; and the BDOS vector, as $EXEC's doexit does
        ld      (0006h),hl
        jp      0000h           ; a plain warm boot now: the run ends
bdosjp: jp      0               ; the target is filled in at the start
wbsave: defw    0
tabsave:defw    0
