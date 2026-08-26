; Drive every branch of the ADM-3A to ANSI translator in console_output().
;
; Every other expected string in the suite is plain ASCII, so nothing anywhere
; else puts a byte into that function that changes term_state, and the whole
; escape-sequence path - four states, ESC = cursor addressing, the Kaypro
; ESC G attribute byte, and the control codes ADM-3A terminals use for cursor
; movement - was reachable by no test at all.
;
; The bytes below go out through BDOS 2 one at a time.  BDOS 9 would reach the
; same translator, but a $ terminator cannot carry an arbitrary byte string.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
        org     0100h
        ld      hl,seq
        ld      b,36            ; seq is 36 bytes; keep this in step with it
loop:   ld      a,(hl)
        push    hl
        push    bc
        ld      e,a
        ld      c,2
        call    0005h
        pop     bc
        pop     hl
        inc     hl
        djnz    loop
        ld      c,0
        jp      0005h

seq:    defb    'A'                     ; an ordinary character passes through
        defb    1bh,'*'                 ; clear screen and home
        defb    1bh,'T'                 ; clear to end of line
        defb    1bh,'Y'                 ; clear to end of screen
        defb    1bh,')'                 ; reverse video on
        defb    1bh,'('                 ; reverse video off
        defb    1bh,'G','4'             ; Kaypro attribute: reverse
        defb    1bh,'G','2'             ; Kaypro attribute: half intensity
        defb    1bh,'G','9'             ; an attribute with no meaning: reset
        defb    1bh,'=',22h,25h         ; cursor to row 2, column 5, both +32
        defb    1ah                     ; ^Z clears the screen and homes
        defb    1eh                     ; ^^ homes the cursor
        defb    0bh                     ; ^K cursor up
        defb    0ch                     ; ^L cursor right
        defb    08h                     ; backspace passes through unchanged
        defb    07h                     ; so does BEL
        defb    01h                     ; a control code with no meaning: dropped
        defb    1bh,'q'                 ; an unknown escape passes through as-is
        defb    0dh,0ah                 ; CR LF are not translated to anything
        defb    'Z'
