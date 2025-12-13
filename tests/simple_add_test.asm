; Simple ADD HL,BC test
; Assemble with: z80asm simple_add_test.asm -o simple_add_test.com

    ORG 0100h

    ; Set up known values
    LD HL, 1234h
    LD BC, 5678h
    LD A, 0FFh      ; Set all flags
    PUSH AF
    POP AF          ; Load flags

    ; Do the ADD
    ADD HL, BC

    ; Save result
    PUSH AF         ; Flags on stack
    PUSH HL         ; Result on stack

    ; Exit to CP/M
    JP 0

    END
