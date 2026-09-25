; Run a script of BDOS file calls against one FCB and print what came back.
;
;   FCB_IO <file> <script>
;
; <file> is parsed into the default FCB by the emulator and copied into a
; private 36-byte FCB here, with EX, S1, S2, RC, CR and R0-R2 zeroed.  The
; script is the second word of the command tail, so it arrives upper-cased;
; every command is one letter, some take a character or a decimal number:
;
;   O  open (15)         M  make (22)          C  close (16)
;   R  read seq (20)     W  write seq (21)     G  read random (33)
;   P  write random (34) Q  write random with zero fill (40)
;   F  file size (35), prints #r2r1r0
;   T  set random record (36), prints #r2r1r0
;   S  prints the FCB's (EX,S2,CR)
;   @  prints @ and the FCB's RC
;   L  read seq to end of file, printing every byte up to a ^Z: CR prints
;      as <, LF as >, ^Z as ~ and ends the dump, other control bytes as ?
;   Hc fill the DMA buffer with the character c
;   Jn Kn Un  put CR, LF or ^Z at byte n of the DMA buffer
;   Zn Xn Yn  set the FCB's CR, EX or S2 byte to n
;   &n set the FCB's drive byte to n (63 is '?')
;   Nn set R0-R2 to n, up to 16777215
;   Vnc put the character c at byte n of the DMA buffer
;   #n run the command after it n times in all
;   E  delete (19)       B  reset disk (13), then the DMA buffer again
;   D  search first (17) A  search next (18): both print the entry they
;      return as [EX,S2,RC]
;   >name;  rename (23) the FCB's file to name, which ends at the ;
;   I  copy the FCB, all 36 bytes, to a second one at another address and
;      use that from here on; a second I copies it back and switches back
;   !  read the console (BDOS 1) for ever, closing nothing: the run ends
;      when the emulator gives up at the end of its input
;   %  call BIOS READ (0FE27h), closing nothing: under CPM_BIOS_DISK=error
;      the emulator ends the run there
;
; A read that succeeds prints the first byte of the record it read.  Any call
; that fails prints = and the status in A as two hex digits, so a read at end
; of file prints =01.  The DMA buffer is this program's own, never 0080h, so
; the command tail is not overwritten by the first read.
;
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
bdos	equ	0005h

	ld	sp,stack
	ld	hl,fcb		; a clean FCB: every position byte zero
	ld	b,36
zfcb:	ld	(hl),0
	inc	hl
	djnz	zfcb
	ld	hl,005Ch	; drive, name and type only
	ld	de,fcb
	ld	bc,12
	ldir
	ld	hl,0080h	; the script is the second word of the tail
	ld	b,(hl)
	inc	hl
	call	skipsp
skipw:	ld	a,b
	or	a
	jr	z,gotw
	ld	a,(hl)
	cp	' '
	jr	z,gotw
	inc	hl
	dec	b
	jr	skipw
gotw:	call	skipsp
	ld	de,script
copy:	ld	a,b
	or	a
	jr	z,copied
	ld	a,(hl)
	ld	(de),a
	inc	hl
	inc	de
	dec	b
	jr	copy
copied:	xor	a
	ld	(de),a
	ld	de,dma		; set DMA to our own buffer
	ld	c,26
	call	bdos
	ld	hl,script
	ld	(ip),hl

next:	ld	hl,(repc)	; a # count still running: the same command again
	ld	a,h
	or	l
	jr	z,next1
	dec	hl
	ld	(repc),hl
	ld	hl,(repip)
	ld	(ip),hl
next1:	call	getch
	or	a
	jp	z,finish
	cp	'O'
	jp	z,c_open
	cp	'M'
	jp	z,c_make
	cp	'C'
	jp	z,c_close
	cp	'R'
	jp	z,c_read
	cp	'W'
	jp	z,c_write
	cp	'G'
	jp	z,c_rrand
	cp	'P'
	jp	z,c_wrand
	cp	'Q'
	jp	z,c_wzero
	cp	'F'
	jp	z,c_size
	cp	'T'
	jp	z,c_setrr
	cp	'S'
	jp	z,c_show
	cp	'L'
	jp	z,c_dump
	cp	'H'
	jp	z,c_fill
	cp	'J'
	jp	z,c_putcr
	cp	'K'
	jp	z,c_putlf
	cp	'U'
	jp	z,c_puteof
	cp	'Z'
	jp	z,c_setcr
	cp	'X'
	jp	z,c_setex
	cp	'Y'
	jp	z,c_sets2
	cp	'N'
	jp	z,c_setr
	cp	'V'
	jp	z,c_putch
	cp	'#'
	jp	z,c_rep
	cp	'E'
	jp	z,c_dele
	cp	'B'
	jp	z,c_reset
	cp	'D'
	jp	z,c_srch1
	cp	'A'
	jp	z,c_srchn
	cp	'>'
	jp	z,c_ren
	cp	'I'
	jp	z,c_other
	cp	'@'
	jp	z,c_rc
	cp	'!'
	jp	z,c_hang
	cp	'%'
	jp	z,c_bios
	cp	'&'
	jp	z,c_setdr
	push	af		; unknown: print ? and the letter, and go on
	ld	a,'?'
	call	putc
	pop	af
	call	putc
	jp	next

finish:	ld	c,0
	jp	bdos

; --- the calls ---------------------------------------------------------------
c_open:	ld	c,15
	jr	dirop
c_make:	ld	c,22
	jr	dirop
c_dele:	ld	c,19
	jr	dirop
c_close:ld	c,16
dirop:	call	callf
	cp	0FFh
	jp	z,fail
	jp	next

c_read:	ld	c,20
	jr	rdop
c_rrand:ld	c,33
rdop:	call	callf
	or	a
	jp	nz,fail
	ld	a,(dma)
	call	putc
	jp	next

c_write:ld	c,21
	jr	wrop
c_wrand:ld	c,34
	jr	wrop
c_wzero:ld	c,40
wrop:	call	callf
	or	a
	jp	nz,fail
	jp	next

c_size:	ld	c,35
	call	callf
	cp	0FFh
	jp	z,fail
	jr	showr
c_setrr:ld	c,36
	call	callf
showr:	ld	a,'#'
	call	putc
	ld	ix,(cur)
	ld	a,(ix+35)
	call	hexbyte
	ld	ix,(cur)
	ld	a,(ix+34)
	call	hexbyte
	ld	ix,(cur)
	ld	a,(ix+33)
	call	hexbyte
	jp	next

c_show:	ld	ix,(cur)
	ld	a,'('
	call	putc
	ld	a,(ix+12)
	call	hexbyte
	ld	a,','
	call	putc
	ld	ix,(cur)
	ld	a,(ix+14)
	call	hexbyte
	ld	a,','
	call	putc
	ld	ix,(cur)
	ld	a,(ix+32)
	call	hexbyte
	ld	a,')'
	call	putc
	jp	next

c_dump:	ld	c,20
	call	callf
	or	a
	jp	nz,fail
	ld	hl,dma
	ld	b,128
dbyte:	ld	a,(hl)
	cp	1Ah
	jr	z,deof
	cp	0Dh
	jr	nz,dnotcr
	ld	a,'<'
	jr	dput
dnotcr:	cp	0Ah
	jr	nz,dnotlf
	ld	a,'>'
	jr	dput
dnotlf:	cp	20h
	jr	c,dctl
	cp	7Fh
	jr	c,dput
dctl:	ld	a,'?'
dput:	push	hl
	push	bc
	call	putc
	pop	bc
	pop	hl
	inc	hl
	djnz	dbyte
	jr	c_dump
deof:	ld	a,'~'
	call	putc
	jp	next

; --- the DMA buffer and the FCB ----------------------------------------------
c_fill:	call	getch
	ld	hl,dma
	ld	b,128
fillb:	ld	(hl),a
	inc	hl
	djnz	fillb
	jp	next

c_putcr:ld	c,0Dh
	jr	putdma
c_putlf:ld	c,0Ah
	jr	putdma
c_puteof:ld	c,1Ah
putdma:	push	bc
	call	getnum
	pop	bc
	ld	h,0
	ld	de,dma
	add	hl,de
	ld	(hl),c
	jp	next

c_setcr:call	getnum
	ld	ix,(cur)
	ld	(ix+32),l
	jp	next
c_setex:call	getnum
	ld	ix,(cur)
	ld	(ix+12),l
	jp	next
c_sets2:call	getnum
	ld	ix,(cur)
	ld	(ix+14),l
	jp	next
c_setdr:call	getnum
	ld	ix,(cur)
	ld	(ix+0),l
	jp	next
c_setr:	call	getnum
	ld	ix,(cur)
	ld	(ix+33),l
	ld	(ix+34),h
	ld	(ix+35),e
	jp	next

c_putch:call	getnum		; Vnc
	push	hl
	call	getch
	pop	hl
	ld	h,0
	ld	de,dma
	add	hl,de
	ld	(hl),a
	jp	next

c_rep:	call	getnum		; #n: n - 1 more after this one
	dec	hl
	ld	(repc),hl
	ld	hl,(ip)
	ld	(repip),hl
	jp	next1

c_reset:ld	c,13		; the reset sets the DMA back to 0080h
	call	bdos
	ld	de,dma
	ld	c,26
	call	bdos
	jp	next

c_srch1:ld	c,17
	jr	srch
c_srchn:ld	c,18
srch:	call	callf
	cp	0FFh
	jp	z,fail
	add	a,a		; the entry is at dma + 32 * A
	add	a,a
	add	a,a
	add	a,a
	add	a,a
	ld	e,a
	ld	d,0
	ld	ix,dma
	add	ix,de
	ld	a,'['
	call	putc
	ld	a,(ix+12)
	call	hexbyte
	ld	a,','
	call	putc
	ld	a,(ix+14)
	call	hexbyte
	ld	a,','
	call	putc
	ld	a,(ix+15)
	call	hexbyte
	ld	a,']'
	call	putc
	jp	next

c_hang:	ld	c,1
	call	bdos
	jr	c_hang

c_bios:	call	0FE27h		; BIOS READ
	jp	next

c_rc:	ld	a,'@'
	call	putc
	ld	ix,(cur)
	ld	a,(ix+15)
	call	hexbyte
	jp	next

; >name;  The old name is bytes 0-11 of the FCB, the new one goes in 16-27.
c_ren:	ld	hl,(cur)
	ld	de,16
	add	hl,de
	ld	(hl),0		; drive: the same one
	inc	hl
	ld	b,11
rblank:	ld	(hl),' '
	inc	hl
	djnz	rblank
	ld	hl,(cur)
	ld	de,17
	add	hl,de		; HL: where the next name byte goes
	ld	c,8		; bytes left in this part
rname:	push	hl
	call	getch
	pop	hl
	or	a
	jr	z,rdone
	cp	';'
	jr	z,rdone
	cp	'.'
	jr	z,rdot
	inc	c		; a byte past the part's end is dropped
	dec	c
	jr	z,rname
	ld	(hl),a
	inc	hl
	dec	c
	jr	rname
rdot:	ld	hl,(cur)
	ld	de,25
	add	hl,de
	ld	c,3
	jr	rname
rdone:	ld	c,23
	jp	dirop

; I: copy the FCB in use to the other one, and use that one.
c_other:ld	hl,(cur)
	ld	de,fcb
	or	a
	sbc	hl,de
	ld	hl,fcb
	ld	de,fcb2
	jr	z,other1
	ex	de,hl
other1:	ld	(cur),de
	ld	bc,36
	ldir
	jp	next

; --- helpers -----------------------------------------------------------------
; Call BDOS function C on the FCB in use.
callf:	ld	de,(cur)
	jp	bdos

; A failed call: print = and the status byte.
fail:	push	af
	ld	a,'='
	call	putc
	pop	af
	call	hexbyte
	jp	next

; Next script character in A, 0 at the end.  Does not step past the end.
getch:	ld	hl,(ip)
	ld	a,(hl)
	or	a
	ret	z
	inc	hl
	ld	(ip),hl
	ret

; A decimal number from the script into E:HL (24 bits).
getnum:	ld	hl,0
	ld	e,0
gnloop:	push	hl
	ld	hl,(ip)
	ld	a,(hl)
	pop	hl
	sub	'0'
	ret	c
	cp	10
	ret	nc
	push	af
	push	hl
	ld	hl,(ip)
	inc	hl
	ld	(ip),hl
	pop	hl
	add	hl,hl		; E:HL * 2
	rl	e
	ld	(tlo),hl
	ld	a,e
	ld	(thi),a
	add	hl,hl		; * 4
	rl	e
	add	hl,hl		; * 8
	rl	e
	ld	bc,(tlo)	; * 8 + * 2
	add	hl,bc
	ld	a,(thi)
	adc	a,e
	ld	e,a
	pop	af		; + the digit
	ld	c,a
	ld	b,0
	add	hl,bc
	ld	a,e
	adc	a,0
	ld	e,a
	jr	gnloop

skipsp:	ld	a,b
	or	a
	ret	z
	ld	a,(hl)
	cp	' '
	ret	nz
	inc	hl
	dec	b
	jr	skipsp

putc:	push	hl
	push	de
	push	bc
	ld	e,a
	ld	c,2
	call	bdos
	pop	bc
	pop	de
	pop	hl
	ret

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
	jp	putc
dig:	add	a,'0'
	jp	putc

ip:	defw	0
cur:	defw	fcb		; the FCB in use: fcb, or fcb2 after an I
repc:	defw	0
repip:	defw	0
tlo:	defw	0
thi:	defb	0
fcb:	defs	36
fcb2:	defs	36
dma:	defs	128
script:	defs	130
	defs	64
stack:
