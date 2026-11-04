; HLT waits for an interrupt.  The timer routine at 0038h counts the
; interrupts, and after three HLTs the program prints the count as one
; digit.  With --int-cycles each HLT waits for one tick, so the digit is 3.
; A HLT that runs on like a NOP prints 0, because the three HLTs and the
; setup take far fewer cycles than one tick.  Without --int-cycles no
; interrupt can end the first HLT, and cpmemu ends the run there.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
start:  ld      a,0C3h          ; JP isr at the RST 38h vector
        ld      (0038h),a
        ld      hl,isr
        ld      (0039h),hl
        xor     a
        ld      (count),a
        ei
        halt
        halt
        halt
        di
        ld      a,(count)
        add     a,'0'
        ld      e,a
        ld      c,2             ; BDOS 2, console output
        call    0005h
        jp      0

isr:    push    af
        ld      a,(count)
        inc     a
        ld      (count),a
        pop     af
        ei
        reti

count:  db      0
