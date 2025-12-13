# QKZ80 Emulator Status

## 8080 Mode

**Status: 100% PASS on 8080EXM.COM test suite**

All 27 tests passing:
- dad <b,d,h,sp>: PASS
- aluop nn: PASS
- aluop <b,c,d,e,h,l,m,a>: PASS
- <daa,cma,stc,cmc>: PASS
- <inr,dcr> a: PASS
- <inr,dcr> b: PASS
- <inx,dcx> b: PASS
- <inr,dcr> c: PASS
- <inr,dcr> d: PASS
- <inx,dcx> d: PASS
- <inr,dcr> e: PASS
- <inr,dcr> h: PASS
- <inx,dcx> h: PASS
- <inr,dcr> l: PASS
- <inr,dcr> m: PASS
- <inx,dcx> sp: PASS
- lhld nnnn: PASS
- shld nnnn: PASS
- lxi <b,d,h,sp>,nnnn: PASS
- ldax <b,d>: PASS
- mvi <b,c,d,e,h,l,m,a>,nn: PASS
- mov <bcdehla>,<bcdehla>: PASS
- sta nnnn / lda nnnn: PASS
- <rlc,rrc,ral,rar>: PASS
- stax <b,d>: PASS

### Key Implementation Details

The critical fix for 8080 flag handling: Flag bits 1, 3, and 5 have fixed values in 8080 mode (bit 1=1, bits 3,5=0), but these are only applied when flags are read for PUSH PSW operations. Internally, flags are stored as-is, matching how real 8080 hardware and reference implementations work.

## Z80 Mode

Work in progress.
