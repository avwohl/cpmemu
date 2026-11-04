; GDB stub test guest: a HLT that no interrupt can end.  Without a debugger
; the run ends there (tests/hlt.asm).  With one attached the stub stops the
; program, and the program runs on to its exit when the debugger resumes it.
; Assembled at test time; no .com is committed.
	.z80
	public	start,hlt,after
start:	nop
hlt:	halt
after:	jp	0
