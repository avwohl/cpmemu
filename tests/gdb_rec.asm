; GDB stub test guest: a routine that calls itself three deep.  The test
; stops at "bottom" and reads the shadow call stack with "monitor bt": three
; return addresses of "after" inside rec, then the one inside start.
; Assembled at test time; no .com is committed.
	.z80
	public	start,rec,after,bottom,done
start:	ld	b,3
	call	rec
done:	jp	0

rec:	dec	b
	jr	z,bottom
	call	rec
after:	ret
bottom:	ret
