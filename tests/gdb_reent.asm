; GDB stub test guest: callee-pops epilogues, as uplm80 writes them for a
; REENTRANT procedure with a third argument on the stack (D7 rule 2 of
; docs/debugger_plan.md).
;
; proc_a takes its argument off the stack and moves the return address up:
; pop de / pop hl / push de / ret.  The RET runs with SP above the slot where
; the call put the return address.
;
; proc_b takes the argument off, then jumps through HL instead of using RET,
; so no RET ever removes its shadow stack entry.  The entry is dropped when
; outer returns.
; Assembled at test time; no .com is committed.
	.z80
	public	start,after_outer,outer,body_a,after_a,body_b,after_b

start:	call	outer
after_outer:
	jp	0

outer:	ld	hl,0AAAAh
	push	hl
	call	proc_a
after_a:
	ld	hl,0BBBBh
	push	hl
	call	proc_b
after_b:
	ret

proc_a:	nop
body_a:	nop
	pop	de
	pop	hl
	push	de
	ret

proc_b:	nop
body_b:	nop
	pop	de
	inc	sp
	inc	sp
	ex	de,hl
	jp	(hl)
