	.title test2 - emulator test
	.module test2
	.area TEST2 (ABS,OVR)

	.org #0x0100
start:	ld sp,#0x4000
	ld bc,#0x0000

nocary:	push bc
	pop af
	daa
	inc bc
	ld a,b
	or c
	jp nz,nocary

	ld hl,#0x0000
	push hl
	ret


	
	
