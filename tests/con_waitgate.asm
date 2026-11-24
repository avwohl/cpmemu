; Wait for a key with two status calls in the loop: BDOS 11 to see whether a
; key is ready, then BDOS 6 with E = FFh to read it, and round again while
; BDOS 6 answers 0.  The registers differ between the two calls, so the
; waiting rule has to see the loop come back to the same state every two
; calls, and a "ready" from BDOS 11 has to hold for the BDOS 6 read after it.
; At the end of input cpmemu has to stop it with the 1024-read diagnostic, as
; it did before eof_idle; for a while this guest instead ran for ever.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
loop:   ld      c,11
        call    0005h
        or      a
        jr      z,loop
        ld      c,6
        ld      e,0FFh
        call    0005h
        or      a
        jr      z,loop
        jr      loop
