; GDB stub test guest: waits for a key with BDOS 1, then exits.  The test
; sends ^C through the debugger while the guest waits, then types a key and
; looks for it in A at "got".
; Assembled at test time; no .com is committed.
	.z80
	public	start,rd,got
start:	ld	c,1
rd:	call	5
got:	jp	0
