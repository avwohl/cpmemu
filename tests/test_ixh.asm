; Simple test for IXH/IXL operations
; Assemble with: z80asm test_ixh.asm -o test_ixh.com

        ORG 0100H

        ; Set up IX with a known value
        LD IX, 1234H        ; IX = 0x1234, so IXH=0x12, IXL=0x34

        ; Test ADD A,IXH (DD 84)
        LD A, 10H
        DB 0DDH, 084H       ; ADD A,IXH (should give 0x10 + 0x12 = 0x22)

        ; Test ADD A,IXL (DD 85)
        LD A, 20H
        DB 0DDH, 085H       ; ADD A,IXL (should give 0x20 + 0x34 = 0x54)

        ; Test SUB IXH (DD 94)
        LD A, 50H
        DB 0DDH, 094H       ; SUB IXH (should give 0x50 - 0x12 = 0x3E)

        ; Exit
        JP 0

        END
