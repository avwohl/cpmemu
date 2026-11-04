; GDB stub test guest: fills 0200h to 0EFFFh with 0A5h, then exits.  The test
; sets a breakpoint inside the loop and one after it, and checks that the
; program bytes at 0100h and the whole fill are as the program left them, so
; the stub put nothing in guest memory.
; Assembled at test time; no .com is committed.
	.z80
	public	start,fill,filled
start:	ld	hl,0200h
fill:	ld	(hl),0A5h
	inc	hl
	ld	a,h
	cp	0F0h
	jr	nz,fill
filled:	jp	0
