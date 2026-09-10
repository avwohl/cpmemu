; Read BDOS 1 forever, ignoring everything, including ^Z.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
spin:   ld      c,1             ; BDOS 1, ignore what comes back, forever
        call    0005h
        jr      spin
