; An FCB and a DMA buffer at the top of the guest's 64K.
;
; The guest's memory is exactly the emulator's 64K buffer, and the file calls
; read and write it as mem[DE + n] and mem[DMA + n].  Two things are checked,
; and the output is all four results in a row:
;
;   1. BDOS 22 with its FCB at FFECh.  Make clears 17 bytes from the FCB's
;      RC on, which from FFECh runs 12 bytes past FFFFh into the host heap.
;      No CP/M program can keep an FCB there - the BDOS and BIOS are at the
;      top of memory - so the call is refused: FF.
;   2. BDOS 20 into a DMA buffer at FFC0h.  CP/M addresses wrap at 64K, so
;      the record's first 64 bytes land at FFC0h and its last 64 at 0000h.
;      Page zero is saved first and put back before the next BDOS call,
;      since the read overwrites the jump at 0005h.  Prints the status, the
;      byte at FFC0h and the byte at 0000h.
;
; WRAP.DAT, one record of 64 'A' then 64 'B', is made by run_tests.sh.  The
; expected output is FF00AB.
;
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
bdos	equ	0005h

	ld	sp,stack
	ld	hl,topfcb	; 1: an FCB whose last byte would be 1000Bh
	ld	de,0FFECh
	ld	bc,20		; FFECh .. FFFFh
	ldir
	ld	de,0FFECh
	ld	c,22
	call	bdos
	call	hexbyte

	ld	de,fcb		; 2: WRAP.DAT, record 0, into FFC0h
	ld	c,15
	call	bdos
	ld	de,0FFC0h
	ld	c,26
	call	bdos
	ld	hl,0000h
	ld	de,save
	ld	bc,64
	ldir
	ld	de,fcb
	ld	c,20
	call	bdos
	ld	(status),a
	ld	a,(0FFC0h)
	ld	(first),a
	ld	a,(0000h)
	ld	(last),a
	ld	hl,save		; page zero back before anything calls 0005h
	ld	de,0000h
	ld	bc,64
	ldir
	ld	a,(status)
	call	hexbyte
	ld	a,(first)
	call	putc
	ld	a,(last)
	call	putc
	ld	c,0
	jp	bdos

putc:	ld	e,a
	ld	c,2
	jp	bdos

hexbyte:push	af
	rrca
	rrca
	rrca
	rrca
	call	hexdig
	pop	af
hexdig:	and	0Fh
	cp	10
	jr	c,dig
	add	a,'A'-10
	jr	putc
dig:	add	a,'0'
	jr	putc

topfcb:	defb	0,'TOPFCB  DAT'
	defs	8
fcb:	defb	0,'WRAP    DAT'
	defs	24
status:	defb	0
first:	defb	0
last:	defb	0
save:	defs	64
	defs	64
stack:
