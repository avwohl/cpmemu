; GDB stub test guest for --backtrace: two calls deep, then a HLT that nothing
; can end, so the run ends with status 1.  The backtrace is the PC and the
; return addresses ret_b and ret_a.
; Assembled at test time; no .com is committed.
	.z80
	public	start,ret_a,ret_b
start:	call	sub_a
ret_a:	jp	0

sub_a:	call	sub_b
ret_b:	ret

sub_b:	nop
	halt
