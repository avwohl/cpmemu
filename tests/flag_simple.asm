; Simplest possible flag test - just do one ADD HL and print the flags

	org	0x100

	; Clear all flags first
	ld	a, 0
	push	af
	pop	af

	; Do a simple ADD HL,BC that should set half-carry
	ld	hl, 0x0F00
	ld	bc, 0x0100
	add	hl, bc		; Result: 0x1000, should have H=1, C=0

	; Get flags into A and print as hex
	push	af
	pop	bc		; BC now has AF
	ld	a, c		; A = flags

	; Print the flag byte in hex
	; Convert high nibble
	ld	b, a
	rrca
	rrca
	rrca
	rrca
	and	0x0F
	add	a, '0'
	cp	':'
	jr	c, skip1
	add	a, 7
skip1:	ld	e, a
	ld	c, 2		; BDOS print char
	call	5

	; Convert low nibble
	ld	a, b
	and	0x0F
	add	a, '0'
	cp	':'
	jr	c, skip2
	add	a, 7
skip2:	ld	e, a
	ld	c, 2
	call	5

	; Print newline
	ld	e, 13
	ld	c, 2
	call	5
	ld	e, 10
	ld	c, 2
	call	5

	; Exit
	ld	c, 0
	call	5
