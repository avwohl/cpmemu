	.title test1 - emulator test
	.module test1
	.area TEST1 (ABS,OVR)

	.org #0x0100
start:	ld sp,#0x4000
	ld bc,#0x0000

nocary:	scf
	ccf
	ld a,b
	sbc a,c
	inc bc
	ld a,b
	or c
	jp nz,nocary

	ld bc,#0x0000
	
cary:	scf
	ld a,b
	sbc a,c
	inc bc
	ld a,b
	or c
	jp nz,cary

	ld hl,#0x0000
	push hl
	ret


	
	
