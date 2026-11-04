; GDB stub test guest: a loop that never ends, for ^C and SIGTERM.
; Assembled at test time; no .com is committed.
	.z80
	public	start
start:	jr	start
