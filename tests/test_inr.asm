; Test INR instruction
        ORG     100H

        MVI     A, 0FFH  ; A = 0xFF
        INR     A        ; A = 0x00, should set Z=1, S=0, P=1, AC=1, bit1=1

        ; Exit
        JMP     0

        END
