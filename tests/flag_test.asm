; Minimal flag test for ADD HL,BC
; Expected: HL=68AC, Flags should preserve S,Z,P/V, clear N, set C,H from operation

    ORG 0100h

    ; Test 1: Simple ADD HL,BC
    LD HL, 1234h
    LD BC, 5678h
    ADD HL, BC      ; Result: 68AC, should set C=0, H=0

    ; Print HL
    LD C, 2
    LD E, H
    CALL 5
    LD E, L
    CALL 5

    ; Print flags
    PUSH AF
    POP BC
    LD E, B
    CALL 5

    ; Newline
    LD E, 0Dh
    CALL 5
    LD E, 0Ah
    CALL 5

    ; Exit
    JP 0

    END
