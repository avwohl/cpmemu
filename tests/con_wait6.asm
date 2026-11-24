; Wait for a key with a delay between polls, through BDOS 6 with E = FFh,
; which answers 0 both for "no key yet" and at the end of input, so this guest
; can never stop by itself.  At the end of input cpmemu has to stop it with
; the 1024-read diagnostic, as it did before eof_idle.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
loop:   ld      b,200
delay:  djnz    delay
        ld      c,6
        ld      e,0FFh
        call    0005h
        or      a
        jr      z,loop
        jr      loop
