; Test if CALL works correctly

        org 0x100

start:
        ld a, 0x42       ; Load 'B' into A
        call sub1        ; Call subroutine
        halt             ; Should get here with A='C'

sub1:
        ld a, 0x43       ; Load 'C' into A
        ret              ; Return
