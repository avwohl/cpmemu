; Minimal ALU test for comparing with reference emulator
; Assemble with: asm8080 test_alu_minimal.asm

        ORG     0100H

        ; Test 1: Simple ADD
        MVI     A, 0EH          ; A = 0x0E
        MVI     B, 02H          ; B = 0x02
        ADD     B               ; A = 0x10, should set H flag

        ; Test 2: AND operation
        MVI     A, 0FFH         ; A = 0xFF
        MVI     B, 0AAH         ; B = 0xAA
        ANA     B               ; A = 0xAA, should set H flag

        ; Test 3: OR operation
        MVI     A, 0FH          ; A = 0x0F
        MVI     B, 0F0H         ; B = 0xF0
        ORA     B               ; A = 0xFF

        ; Test 4: XOR operation
        MVI     A, 55H          ; A = 0x55
        MVI     B, 0AAH         ; B = 0xAA
        XRA     B               ; A = 0xFF

        ; Test 5: INR
        MVI     A, 0FFH         ; A = 0xFF
        INR     A               ; A = 0x00, should set Z flag

        JMP     0               ; Exit
        END
