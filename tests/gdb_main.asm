; GDB stub test guest: a call and a return, a BDOS call and the exit.
; tests/gdb_stub_test.py sets breakpoints at the public labels, steps through
; CALL 5 and checks the registers.  The sub1 routine adds one to HL.
; Assembled at test time; no .com is committed.
	.z80
	public	start,sub1,after_sub,call5,after5
start:	ld	hl,1234h
	ld	bc,5678h
	call	sub1
after_sub:
	ld	e,'K'
	ld	c,2
call5:	call	5
after5:	jp	0

sub1:	inc	hl
	ret
