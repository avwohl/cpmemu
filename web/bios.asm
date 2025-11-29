;
; CP/M 2.2 BIOS Stub for Web Emulator
;
; This file contains only the jump table and disk parameter tables.
; The actual BIOS calls are trapped by the emulator - the jump
; targets are dummy addresses that will never be reached.
;
; Assemble with: um80 -g bios.asm
; Link with:     ul80 -o bios.sys -p F600 bios.rel
;

        ORG     0F600H          ; BIOS base address (64K - 2.5K)

;----------------------------------------------------------------------
; BIOS JUMP TABLE - 17 entry points, 3 bytes each (51 bytes total)
;----------------------------------------------------------------------

        ; The emulator traps at F600-F633, so these JMP targets
        ; are never executed. BUT some programs (like MBASIC) read
        ; the jump table to get routine addresses, so each JMP must
        ; point to its own entry address for proper behavior.

BOOT:   JMP     BOOT            ; 0 - Cold boot
WBOOT:  JMP     WBOOT           ; 1 - Warm boot
CONST:  JMP     CONST           ; 2 - Console status
CONIN:  JMP     CONIN           ; 3 - Console input
CONOUT: JMP     CONOUT          ; 4 - Console output
LIST:   JMP     LIST            ; 5 - List output
PUNCH:  JMP     PUNCH           ; 6 - Punch output
READER: JMP     READER          ; 7 - Reader input
HOME:   JMP     HOME            ; 8 - Home disk
SELDSK: JMP     SELDSK          ; 9 - Select disk
SETTRK: JMP     SETTRK          ; 10 - Set track
SETSEC: JMP     SETSEC          ; 11 - Set sector
SETDMA: JMP     SETDMA          ; 12 - Set DMA address
READ:   JMP     READ            ; 13 - Read sector
WRITE:  JMP     WRITE           ; 14 - Write sector
LISTST: JMP     LISTST          ; 15 - List status
SECTRAN: JMP    SECTRAN         ; 16 - Sector translation

;----------------------------------------------------------------------
; DISK PARAMETER TABLES
;----------------------------------------------------------------------
; Standard IBM 8" SSSD format:
;   77 tracks, 26 sectors/track, 128 bytes/sector
;   1024-byte blocks, 2 reserved tracks
;   243 blocks total, 64 directory entries
;----------------------------------------------------------------------

; Sector Translation Table (skew table) for 8" SSSD
; Standard IBM skew factor of 6:1
; XLT is 0 for no translation (1:1 mapping), or points here for skewed
XLTTAB:
        DB      1,7,13,19,25,5,11,17,23,3,9,15,21
        DB      2,8,14,20,26,6,12,18,24,4,10,16,22

; Disk Parameter Block for 8" SSSD
; All CP/M 2.2 standard values
DPB0:
        DW      26              ; SPT - sectors per track
        DB      3               ; BSH - block shift factor (1K blocks = 2^(7+3))
        DB      7               ; BLM - block mask (2^3 - 1)
        DB      0               ; EXM - extent mask (for DSM < 256 and 1K blocks)
        DW      242             ; DSM - max block number (243 blocks - 1)
        DW      63              ; DRM - max directory entry (64 - 1)
        DB      0C0H            ; AL0 - directory allocation bitmap
        DB      0               ; AL1 - (2 blocks for directory = bits 7,6 set)
        DW      16              ; CKS - checksum vector size ((DRM+1)/4)
        DW      2               ; OFF - reserved tracks (system tracks)

; Disk Parameter Headers - 16 bytes per drive, 4 drives (A-D)
DPH0:
        DW      0               ; XLT - no sector translation (disk image has no skew)
        DW      0               ; Scratch 1 (used by BDOS)
        DW      0               ; Scratch 2
        DW      0               ; Scratch 3
        DW      DIRBUF          ; DIRBUF - 128-byte directory buffer
        DW      DPB0            ; DPB - disk parameter block
        DW      CSV0            ; CSV - checksum vector (16 bytes)
        DW      ALV0            ; ALV - allocation vector (31 bytes)

DPH1:
        DW      0
        DW      0
        DW      0
        DW      0
        DW      DIRBUF
        DW      DPB0
        DW      CSV1
        DW      ALV1

DPH2:
        DW      0
        DW      0
        DW      0
        DW      0
        DW      DIRBUF
        DW      DPB0
        DW      CSV2
        DW      ALV2

DPH3:
        DW      0
        DW      0
        DW      0
        DW      0
        DW      DIRBUF
        DW      DPB0
        DW      CSV3
        DW      ALV3

;----------------------------------------------------------------------
; WORK AREAS - must be in RAM, not initialized
;----------------------------------------------------------------------

; Directory buffer - shared by all drives
DIRBUF: DS      128             ; 128-byte sector buffer

; Checksum vectors - one per drive, 16 bytes each (CKS)
CSV0:   DS      16
CSV1:   DS      16
CSV2:   DS      16
CSV3:   DS      16

; Allocation vectors - one per drive, 31 bytes each (ceiling(DSM/8) = 31)
ALV0:   DS      31
ALV1:   DS      31
ALV2:   DS      31
ALV3:   DS      31

;----------------------------------------------------------------------
; END - Export symbols for the emulator to find
;----------------------------------------------------------------------

; Public symbols for easy reference
        PUBLIC  BOOT, WBOOT, CONST, CONIN, CONOUT
        PUBLIC  LIST, PUNCH, READER, HOME, SELDSK
        PUBLIC  SETTRK, SETSEC, SETDMA, READ, WRITE
        PUBLIC  LISTST, SECTRAN
        PUBLIC  DPH0, DPH1, DPH2, DPH3, DPB0, XLTTAB
        PUBLIC  DIRBUF, CSV0, CSV1, CSV2, CSV3
        PUBLIC  ALV0, ALV1, ALV2, ALV3

        END
