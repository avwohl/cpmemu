; Test RLCA instruction flags
; Expected behavior:
; - Carry gets bit 7
; - N and H are cleared
; - X and Y come from result (bits 3 and 5)
; - S, Z, P/V unchanged

        ORG 0100H

        ; Test 1: RLCA with A=0x81 (10000001)
        ; Expected: A=0x03, Carry=1, X=1 (bit 3 of 0x03), Y=0 (bit 5 of 0x03)
        LD A, 81H
        SCF             ; Set carry first to clear flags predictably
        RLCA
        ; A should now be 03H, flags should have C=1, X=1, Y=0
        LD B, A         ; Save result in B
        PUSH AF         ; Save flags
        POP HL          ; Get flags in L

        ; Print result
        LD A, B
        CALL PRINT_HEX
        LD A, ' '
        CALL PRINT_CHAR
        LD A, L
        CALL PRINT_HEX
        LD A, 0AH       ; Newline
        CALL PRINT_CHAR

        ; Test 2: RLCA with A=0x40 (01000000)
        ; Expected: A=0x80, Carry=0, X=0 (bit 3 of 0x80), Y=0 (bit 5 of 0x80)
        LD A, 40H
        OR A            ; Clear carry
        RLCA
        LD B, A
        PUSH AF
        POP HL

        LD A, B
        CALL PRINT_HEX
        LD A, ' '
        CALL PRINT_CHAR
        LD A, L
        CALL PRINT_HEX
        LD A, 0AH
        CALL PRINT_CHAR

        ; Test 3: RLCA with A=0x28 (00101000)
        ; Expected: A=0x50, Carry=0, X=0 (bit 3 of 0x50), Y=1 (bit 5 of 0x50)
        LD A, 28H
        OR A
        RLCA
        LD B, A
        PUSH AF
        POP HL

        LD A, B
        CALL PRINT_HEX
        LD A, ' '
        CALL PRINT_CHAR
        LD A, L
        CALL PRINT_HEX
        LD A, 0AH
        CALL PRINT_CHAR

        JP 0             ; Exit

PRINT_HEX:
        PUSH AF
        RRA
        RRA
        RRA
        RRA
        CALL PRINT_DIGIT
        POP AF
        CALL PRINT_DIGIT
        RET

PRINT_DIGIT:
        AND 0FH
        ADD A, '0'
        CP '9'+1
        JR C, PRINT_CHAR
        ADD A, 'A'-'9'-1
PRINT_CHAR:
        PUSH BC
        PUSH DE
        PUSH HL
        LD E, A
        LD C, 2          ; BDOS console output
        CALL 5
        POP HL
        POP DE
        POP BC
        RET

        END
