; Test AND instruction
    ORG 0x100
    LD A, 0xFF
    LD B, 0x0F
    AND B       ; Should result in A=0x0F
    HALT
