; GDB stub test guest: calls the BIOS HOME entry (the WBOOT address plus 15H).  With CPM_BIOS_DISK=error the
; run ends there with status 1, and the stub must send W01.  The "caller"
; routine calls the BIOS, so the shadow stack holds one frame at the end.
; Assembled at test time; no .com is committed.
	.z80
	public	start,caller,after
start:	call	caller
after:	jp	0

caller:	ld	hl,(1)
	ld	de,15h
	add	hl,de
	jp	(hl)
