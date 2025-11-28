# CP/M Boot Track Analysis Notes

## Summary
Attempting to boot real CP/M from SIMH Altair disk images. The boot tracks are incomplete - CCP entry points are missing.

## Memory Layout (from CP/M 2.2 System Alteration Guide Table 6-2)

| Size | Bias | CCP | BDOS | BIOS |
|------|------|-----|------|------|
| 62K | A800H | DC00 | E400 | F200 |
| 63K | AC00H | E000 | E800 | F600 |
| 64K | B000H | E400 | EC00 | FA00 |

**The disk has:** CCP=DF00 (~62.75K system), BIOS expected at F200

## Key Findings

### 1. SIMH Disk Format
- 137-byte sectors (3-byte header + 128-byte data)
- Headers are all 0xE5 E5 E5 (not proper SIMH format with track/sector info)
- Track 0 has 32 sectors, Track 1 has 32 sectors

### 2. Altair 88-DCDD Sector Order
The Altair boot ROM reads sectors in this order (not sequential):
```
[0,6,12,18,24,30,4,10,16,22,28,2,8,14,20,26,1,7,13,19,25,31,5,11,17,23,29,3,9,15,21,27]
```
This puts boot code (phys 12) at address 0x0100 where the `JP NZ,0109` loop expects it.

### 3. Boot Process
```
1. Boot ROM loads T0 with Altair sector ordering
2. Boot sector at 0x0600 has: JMP 0100
3. Code at 0x0100: LD HL,0A00; LD DE,DC00; LD BC,2300; copy loop; JMP F200
4. Should copy from 0x0900 (not 0x0A00!) to DC00 for correct alignment
```

### 4. The Problem - Incomplete Boot Track
Physical sector 17 contains CCP header but NOT the entry point code:
```
0x00-0x05: C3 5C DF C3 58 DF  (JMP DF5C, JMP DF58)
0x06-0x3D: Copyright notice
0x3E-0x7F: ZEROS  <-- Cold entry (offset 0x5C=92) is HERE - MISSING!
```

The CCP header says cold=DF5C, warm=DF58, but those locations are zeros.

### 5. Non-Empty Sectors in Boot Area
Track 0: 12, 17, 19, 21, 23, 25, 27, 29, 31 (only 9 sectors)
Track 1: 32-63 (all 32 sectors)
Total actual data: ~5K (should be ~9K for full boot)

### 6. BIOS Not on Disk
The BIOS must be provided by the emulator at F200. The disk only contains CCP+BDOS.
BIOS entry points referenced: F200, F203, F206, F209, F20C, F20F, F212, F215, F218, F21B, F21E, F221, F224, F227, F22A, F22D, F230

## Files Analyzed
- `/home/wohl/in/cpm2/cpm2.dsk` - SIMH floppy image (1113536 bytes) - INCOMPLETE
- `/home/wohl/in/cpm2/i.dsk` - SIMH hard disk image (8MB) - SAME ISSUE
- `/home/wohl/in/cpm2/CPM63.COM` - MOVCPM output - WRONG ADDRESSES (B900 not E000)
- `/home/wohl/in/cpm2/128sssd.imd` - IMD format, different sector layout (26 spt)

## Options to Proceed

1. **Build-in CCP+BDOS** - Have emulator provide complete CP/M, not load from disk

2. **Patch the disk** - Fill in missing CCP entry points manually

3. **Find complete image** - Try other sources (schorn.ch, deramp.com had connection issues)

4. **Run MOVCPM** - If BDOS emulation works, generate proper system image

## Code References
- Boot code analysis: physical sector 12
- CCP header: physical sector 17 -> logical 24 with Altair ordering
- Address distribution in boot tracks: DCxx-DFxx (136 refs), E0xx-EFxx (238 refs), F2xx (20 refs)

## Date
2024-11-28
