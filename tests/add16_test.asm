; Simple 16-bit ADD test to compare flags with tnylpo
; This tests ADD HL,BC and then halts

	org	0x100

	; Test 1: Simple ADD HL,BC with no overflow
	ld	hl, 0x1234
	ld	bc, 0x0056
	push	af		; Save flags
	add	hl, bc		; HL = 0x128A
	push	af		; Save flags after ADD

	; Test 2: ADD HL,BC with half-carry
	ld	hl, 0x0F00
	ld	bc, 0x0100
	add	hl, bc		; HL = 0x1000, should set H flag
	push	af

	; Test 3: ADD HL,BC with carry
	ld	hl, 0x8000
	ld	bc, 0x8000
	add	hl, bc		; HL = 0x0000, should set C flag
	push	af

	; Test 4: ADD HL,BC with both carry and half-carry
	ld	hl, 0xF800
	ld	bc, 0x0800
	add	hl, bc		; Should set both H and C
	push	af

	; Print the flags to screen for visual inspection
	; We'll use BDOS to print characters

	; First, let's just halt - we can examine flags in debugger
	jp	0		; Warm boot to CP/M
