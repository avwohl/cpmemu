; Make BDOS calls with sentinels in A, B and HL, and print what each call left
; in them: one line per call, the function number and then A, B, H and L, all
; as hex.  "0F FF0000FF" is BDOS 15 returning A = FFh, B = 0, H = 0, L = FFh.
;
; CP/M 2.2 clears its result word on entry and returns through one exit that
; loads it into HL and copies it, A = L and B = H.  So an 8-bit answer comes
; back in L as well as A with H = B = 0, a call with no answer returns 0 in all
; four, and the 16-bit answers of 24, 27, 29 and 31 come back in HL with the
; same bytes in A and B.  The emulator set A alone for an 8-bit answer and HL
; alone for a 16-bit one, so the other registers still held the sentinels:
; 25 printed 00A51234 and 24 printed 5AA50001.  HI-TECH C 3.09 reads the
; answer from HL, and took the 1234h left after BDOS 34 for a failed write
; (issue #5) - the 22 line below is that call.
;
; The calls, in order: 12 version, 25 current drive, 32 get user, 24 login
; vector, 31 DPB address, 26 set DMA (no answer), 15 open a file that is not
; there, 102 (not implemented here: FFh, where 2.2 gives 0), then 22 make,
; 34 write random, 16 close and 19 delete on REGTEST.$$$ in the working
; directory, which the run leaves as it found it.
; Assembled at test time by tests/run_tests.sh; no .com is committed.
	.z80
        ld      hl,calls
        ld      (next),hl
loop:   ld      hl,(next)
        ld      a,(hl)          ; function number, 0 ends the table
        or      a
        jr      z,done
        ld      (func),a
        ld      c,a
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        inc     hl
        ld      (next),hl
        ld      b,0A5h          ; the sentinels the BDOS has to replace
        ld      hl,1234h
        ld      a,5Ah
        call    0005h
        ld      (rega),a
        ld      a,b
        ld      (regb),a
        ld      (reghl),hl
        ld      a,(func)
        call    hexbyte
        ld      e,' '
        ld      c,2
        call    0005h
        ld      a,(rega)
        call    hexbyte
        ld      a,(regb)
        call    hexbyte
        ld      a,(reghl+1)     ; H
        call    hexbyte
        ld      a,(reghl)       ; L
        call    hexbyte
        ld      e,13
        ld      c,2
        call    0005h
        ld      e,10
        ld      c,2
        call    0005h
        jr      loop
done:   ld      c,0
        jp      0005h
hexbyte:push    af
        rrca
        rrca
        rrca
        rrca
        call    hexdig
        pop     af
hexdig: and     0Fh
        cp      10
        jr      c,dig
        add     a,'A'-10
        jr      emit
dig:    add     a,'0'
emit:   ld      e,a
        ld      c,2
        jp      0005h
; function number, then DE
calls:  defb    12
        defw    0
        defb    25
        defw    0
        defb    32
        defw    00FFh           ; E = FFh: get the user number
        defb    24
        defw    0
        defb    31
        defw    0
        defb    26
        defw    0080h
        defb    15
        defw    nofile
        defb    102
        defw    nofile
        defb    22
        defw    regfile
        defb    34
        defw    regfile
        defb    16
        defw    regfile
        defb    19
        defw    regfile
        defb    0
next:   defw    0
func:   defb    0
rega:   defb    0
regb:   defb    0
reghl:  defw    0
; Two FCBs: drive 0, name, type, then EX S1 S2 RC, the map, CR and R0-R2,
; all 0, so BDOS 34 writes record 0.
nofile: defb    0,'NOSUCHFL$$$'
        defb    0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0
regfile:defb    0,'REGTEST $$$'
        defb    0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0
