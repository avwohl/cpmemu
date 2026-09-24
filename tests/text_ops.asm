; Apply a script of BDOS file calls, read from OPS.DAT, to the file the
; command line names, and write what every call returned to RES.DAT.
;
;   TEXT_OPS <file>
;
; tests/text_image_prop.py writes the script and checks the results; this
; program only runs it.  OPS.DAT is 128-byte records, and byte 0 of each is
; the call:
;
;   O  zero EX, S2 and CR, and open (15)
;   C  close (16)
;   R  read sequential (20)
;   W  write sequential (21) the record that follows it in OPS.DAT
;   G  read random (33) record n
;   P  write random (34) record n, from the record that follows
;   Z  point EX, S2 and CR at record n, for 20 and 21
;   F  compute file size (35)
;
; where n is bytes 1, 2 and 3, low byte first.  Anything else, or the end of
; OPS.DAT, ends the run - without closing the file unless the script did, so
; that what happens to a file a program never closes is checked too.
;
; RES.DAT gets a record for every call: byte 0 the status it returned in A,
; bytes 1-3 the FCB's EX, S2 and CR after it, bytes 4-6 its R0-R2.  After a
; read that succeeded comes a second record, the 128 bytes it read.
;
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
bdos	equ	0005h

	ld	sp,stack
	ld	hl,fcb		; the file: drive, name and type from 005Ch
	ld	b,36
zfcb:	ld	(hl),0
	inc	hl
	djnz	zfcb
	ld	hl,005Ch
	ld	de,fcb
	ld	bc,12
	ldir
	ld	de,opsfcb
	ld	c,15
	call	bdos
	inc	a
	jp	z,finish	; no OPS.DAT: nothing to do
	ld	de,resfcb
	ld	c,19
	call	bdos
	ld	de,resfcb
	ld	c,22
	call	bdos
	inc	a
	jp	z,finish

next:	ld	de,opsbuf	; the next call
	call	getops
	jp	nz,done
	ld	a,(opsbuf)
	cp	'O'
	jr	z,c_open
	cp	'C'
	jr	z,c_close
	cp	'R'
	jr	z,c_read
	cp	'W'
	jr	z,c_write
	cp	'G'
	jr	z,c_rrand
	cp	'P'
	jr	z,c_wrand
	cp	'Z'
	jr	z,c_pos
	cp	'F'
	jr	z,c_size
	jp	done

c_open:	xor	a
	ld	(fcb+12),a
	ld	(fcb+14),a
	ld	(fcb+32),a
	ld	c,15
	jr	call0
c_close:ld	c,16
	jr	call0
c_size:	ld	c,35
call0:	call	callf
	jr	result

c_read:	ld	c,20
	jr	rdop
c_rrand:call	setr
	ld	c,33
rdop:	call	callf
	push	af
	call	result1
	pop	af
	or	a
	jr	nz,next
	ld	de,databuf	; and what it read
	call	putres
	jr	next

c_write:ld	de,databuf	; the record to write comes next in OPS.DAT
	call	getops
	jp	nz,done
	ld	c,21
	jr	call0
c_wrand:call	setr
	ld	de,databuf
	call	getops
	jp	nz,done
	ld	c,34
	jr	call0

c_pos:	ld	a,(opsbuf+1)	; CR = n & 7Fh
	and	7Fh
	ld	(fcb+32),a
	ld	a,(opsbuf+1)	; EX = (n >> 7) & 1Fh
	rla
	ld	a,(opsbuf+2)
	rla
	and	1Fh
	ld	(fcb+12),a
	ld	a,(opsbuf+2)	; S2 = n >> 12
	rrca
	rrca
	rrca
	rrca
	and	0Fh
	ld	b,a
	ld	a,(opsbuf+3)
	rlca
	rlca
	rlca
	rlca
	and	0F0h
	or	b
	ld	(fcb+14),a
	xor	a
	jr	result

result:	call	result1
	jp	next

; A record for the call just made, A its status: status, EX, S2, CR, R0-R2.
result1:ld	hl,resbuf
	ld	b,128
zres:	ld	(hl),0
	inc	hl
	djnz	zres
	ld	(resbuf),a
	ld	a,(fcb+12)
	ld	(resbuf+1),a
	ld	a,(fcb+14)
	ld	(resbuf+2),a
	ld	a,(fcb+32)
	ld	(resbuf+3),a
	ld	hl,fcb+33
	ld	de,resbuf+4
	ld	bc,3
	ldir
	ld	de,resbuf
	; fall through

; Write the record at DE to RES.DAT.
putres:	ld	c,26
	call	bdos
	ld	de,resfcb
	ld	c,21
	jp	bdos

; Read the next OPS.DAT record into DE.  NZ at its end.
getops:	ld	c,26
	call	bdos
	ld	de,opsfcb
	ld	c,20
	call	bdos
	or	a
	ret

; R0-R2 from bytes 1-3 of the call.
setr:	ld	hl,opsbuf+1
	ld	de,fcb+33
	ld	bc,3
	ldir
	ret

; Make call C on the file with the DMA at databuf.  Returns its A.
callf:	push	bc
	ld	de,databuf
	ld	c,26
	call	bdos
	pop	bc
	ld	de,fcb
	jp	bdos

done:	ld	de,resfcb
	ld	c,16
	call	bdos
finish:	ld	c,0
	jp	bdos

opsfcb:	defb	0,'OPS     DAT'
	defs	24
resfcb:	defb	0,'RES     DAT'
	defs	24
fcb:	defs	36
opsbuf:	defs	128
databuf:defs	128
resbuf:	defs	128
	defs	64
stack:
