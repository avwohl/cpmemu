/*
 * CP/M BIOS-Level Emulator
 *
 * This emulator boots real CP/M from disk images by:
 * - Loading the boot sector from track 0
 * - Trapping BIOS calls and implementing them in C++
 * - Using disk_image.h for reading .IMD and .dsk files
 *
 * Unlike cpmemu (which emulates at BDOS level), this can run
 * authentic CP/M distributions from disk images.
 */

#include "qkz80.h"
#include "disk_image.h"
#include "console_io.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <vector>

// CP/M Memory Layout for 64K system (matching SIMH Altair CP/M 2.2)
#define BIOS_BASE      0xF200  // BIOS location (matching disk's expected location)
#define BDOS_BASE      0xE400  // BDOS location (loaded from disk)
#define CCP_BASE       0xDC00  // CCP location (loaded from disk)
#define TPA_START      0x0100  // Transient Program Area

// Page zero addresses
#define BOOT_ADDR      0x0000  // JMP WBOOT
#define IOBYTE_ADDR    0x0003  // I/O byte
#define DRVUSER_ADDR   0x0004  // Current drive/user
#define BDOS_ENTRY     0x0005  // JMP BDOS

// BIOS function offsets (from BIOS_BASE)
#define BIOS_BOOT      0   // Cold boot
#define BIOS_WBOOT     3   // Warm boot
#define BIOS_CONST     6   // Console status
#define BIOS_CONIN     9   // Console input
#define BIOS_CONOUT    12  // Console output
#define BIOS_LIST      15  // List output
#define BIOS_PUNCH     18  // Punch output
#define BIOS_READER    21  // Reader input
#define BIOS_HOME      24  // Home disk head
#define BIOS_SELDSK    27  // Select disk
#define BIOS_SETTRK    30  // Set track
#define BIOS_SETSEC    33  // Set sector
#define BIOS_SETDMA    36  // Set DMA address
#define BIOS_READ      39  // Read sector
#define BIOS_WRITE     42  // Write sector
#define BIOS_LISTST    45  // List status
#define BIOS_SECTRAN   48  // Sector translation

// Maximum drives supported
#define MAX_DRIVES     4

// Standard 8" SSSD skew table
static const uint8_t SKEW_TABLE_8INCH[] = {
   1,  7, 13, 19, 25,  5, 11, 17, 23,  3,  9, 15, 21,
   2,  8, 14, 20, 26,  6, 12, 18, 24,  4, 10, 16, 22
};

// Disk Parameter Block for 8" SSSD
struct DPB_8SSSD {
  uint16_t spt;    // Sectors per track = 26
  uint8_t  bsh;    // Block shift = 3 (1024-byte blocks)
  uint8_t  blm;    // Block mask = 7
  uint8_t  exm;    // Extent mask = 0
  uint16_t dsm;    // Total blocks - 1 = 242
  uint16_t drm;    // Directory entries - 1 = 63
  uint8_t  al0;    // Allocation bitmap = 0xC0
  uint8_t  al1;    // = 0x00
  uint16_t cks;    // Checksum size = 16
  uint16_t off;    // Reserved tracks = 2
};

class CPMBiosEmulator {
private:
  qkz80* cpu;
  qkz80_cpu_mem* mem;
  bool debug;

  // Disk drives
  DiskImage* drives[MAX_DRIVES];
  int current_drive;
  int current_track;
  int current_sector;
  int current_head;
  uint16_t current_dma;

  // Disk Parameter Headers (one per drive)
  uint16_t dph_addresses[MAX_DRIVES];

  // BIOS magic address for trapping (we place RET instructions here)
  static const uint16_t BIOS_MAGIC = 0xFF00;

  // Dynamic CP/M addresses (set by cold_boot or load_memory)
  uint16_t ccp_base;
  uint16_t bdos_base;
  uint16_t bios_base;
  bool memory_loaded;  // True if using --load-memory (don't reload CCP from disk)

public:
  CPMBiosEmulator(qkz80* acpu, qkz80_cpu_mem* amem, bool adebug = false)
    : cpu(acpu), mem(amem), debug(adebug),
      current_drive(0), current_track(0), current_sector(1),
      current_head(0), current_dma(0x0080),
      ccp_base(CCP_BASE), bdos_base(BDOS_BASE), bios_base(BIOS_BASE),
      memory_loaded(false) {
    for (int i = 0; i < MAX_DRIVES; i++) {
      drives[i] = nullptr;
      dph_addresses[i] = 0;
    }
  }

  ~CPMBiosEmulator() {
    for (int i = 0; i < MAX_DRIVES; i++) {
      if (drives[i]) {
        delete drives[i];
        drives[i] = nullptr;
      }
    }
  }

  // Mount a disk image on a drive
  bool mount_drive(int drive, const char* filename) {
    if (drive < 0 || drive >= MAX_DRIVES) {
      fprintf(stderr, "Invalid drive number: %d\n", drive);
      return false;
    }

    if (drives[drive]) {
      delete drives[drive];
      drives[drive] = nullptr;
    }

    drives[drive] = open_disk_image(filename, false);
    if (!drives[drive]) {
      fprintf(stderr, "Failed to open disk image: %s\n", filename);
      return false;
    }

    const DiskGeometry& g = drives[drive]->geometry();
    fprintf(stderr, "Drive %c: %s\n", 'A' + drive, filename);
    fprintf(stderr, "  Geometry: %d tracks, %d heads, %d sectors/track, %d bytes/sector\n",
            g.tracks, g.heads, g.sectors_per_track, g.sector_size);

    return true;
  }

  // Setup BIOS jump table and memory structures
  void setup_bios() {
    char* m = mem->get_mem();

    // Setup BIOS jump table at BIOS_BASE
    // Each entry is JMP to a magic address that we trap
    for (int i = 0; i < 17; i++) {
      uint16_t addr = BIOS_BASE + (i * 3);
      uint16_t magic = BIOS_MAGIC + i;
      m[addr] = 0xC3;  // JMP
      m[addr + 1] = magic & 0xFF;
      m[addr + 2] = (magic >> 8) & 0xFF;
    }

    // Setup page zero - only if not already set by cold_boot
    // JMP WBOOT at 0x0000 (use BIOS trap)
    m[0x0000] = 0xC3;
    m[0x0001] = (BIOS_BASE + BIOS_WBOOT) & 0xFF;
    m[0x0002] = ((BIOS_BASE + BIOS_WBOOT) >> 8) & 0xFF;

    // Don't touch IOBYTE, DRVUSER, or BDOS entry - cold_boot() sets these correctly

    // Setup DPH and DPB structures in high memory
    setup_disk_parameters();
  }

  // Set CP/M addresses for memory-loaded images
  void set_cpm_addresses(uint16_t ccp, uint16_t bdos, uint16_t bios) {
    ccp_base = ccp;
    bdos_base = bdos;
    bios_base = bios;
    memory_loaded = true;
  }

  // Setup Disk Parameter Headers and Blocks
  void setup_disk_parameters() {
    char* m = mem->get_mem();

    // Place DPH structures at BIOS_BASE - 0x100
    uint16_t dph_area = BIOS_BASE - 0x100;

    // Standard 8" SSSD DPB
    uint16_t dpb_addr = dph_area;
    DPB_8SSSD dpb = {26, 3, 7, 0, 242, 63, 0xC0, 0x00, 16, 2};
    memcpy(&m[dpb_addr], &dpb, sizeof(dpb));

    // Skew table
    uint16_t xlt_addr = dpb_addr + sizeof(dpb);
    memcpy(&m[xlt_addr], SKEW_TABLE_8INCH, 26);

    // Directory buffer (128 bytes)
    uint16_t dirbuf_addr = xlt_addr + 32;

    // Allocation vectors (32 bytes each, enough for 256 blocks)
    uint16_t alv_base = dirbuf_addr + 128;

    // Checksum vectors (16 bytes each)
    uint16_t csv_base = alv_base + (MAX_DRIVES * 32);

    // DPH for each drive
    uint16_t dph_base = csv_base + (MAX_DRIVES * 16);

    for (int i = 0; i < MAX_DRIVES; i++) {
      dph_addresses[i] = dph_base + (i * 16);
      uint16_t dph = dph_addresses[i];

      // DPH structure:
      // +0: XLT (sector translate table)
      // +2,4,6: scratch (BDOS work area)
      // +8: DIRBUF
      // +10: DPB
      // +12: CSV
      // +14: ALV

      m[dph + 0] = xlt_addr & 0xFF;
      m[dph + 1] = (xlt_addr >> 8) & 0xFF;
      m[dph + 2] = 0; m[dph + 3] = 0;  // scratch
      m[dph + 4] = 0; m[dph + 5] = 0;
      m[dph + 6] = 0; m[dph + 7] = 0;
      m[dph + 8] = dirbuf_addr & 0xFF;
      m[dph + 9] = (dirbuf_addr >> 8) & 0xFF;
      m[dph + 10] = dpb_addr & 0xFF;
      m[dph + 11] = (dpb_addr >> 8) & 0xFF;
      m[dph + 12] = (csv_base + i * 16) & 0xFF;
      m[dph + 13] = ((csv_base + i * 16) >> 8) & 0xFF;
      m[dph + 14] = (alv_base + i * 32) & 0xFF;
      m[dph + 15] = ((alv_base + i * 32) >> 8) & 0xFF;
    }
  }

  // Detect if this is a SIMH format disk (by checking geometry)
  bool is_simh_disk() {
    const DiskGeometry& g = drives[0]->geometry();
    // SIMH uses 32 sectors per track, 128-byte data sectors
    return (g.sectors_per_track == 32 && g.sector_size == 128);
  }

  // SIMH Altair skew factor
  static const int SIMH_SKEW = 17;

  // Translate logical sector to physical sector using SIMH skew
  // Logical sectors are 0-based, physical sectors are 1-based in our API
  int simh_sectran(int logical_sector) {
    // Physical = (logical * 17) % 32
    // Sectors are 0-31, but our API uses 1-32
    int physical = (logical_sector * SIMH_SKEW) % 32;
    return physical + 1;  // Convert to 1-based
  }

  // Boot from SIMH Altair format disk
  bool cold_boot_simh() {
    char* m = mem->get_mem();
    uint8_t buffer[128];

    fprintf(stderr, "Detected SIMH Altair disk format\n");

    // SIMH CP/M disk layout has interleaved sectors on system tracks.
    // Many sectors are empty (0x00 or 0xE5). The actual CP/M system data
    // is in the non-empty sectors, which need to be loaded contiguously.
    //
    // We:
    // 1. Load all non-empty sectors from tracks 0-2 contiguously
    // 2. Find the CCP header (JMP xx5C, JMP xx58 pattern)
    // 3. Calculate the memory load address based on where CCP wants to be
    // 4. Copy to high memory and set up our BIOS traps

    auto is_empty_sector = [](uint8_t* buf) {
      for (int i = 0; i < 128; i++) {
        if (buf[i] != 0x00 && buf[i] != 0xE5) return false;
      }
      return true;
    };

    // Load all non-empty sectors contiguously into a buffer
    uint8_t disk_buffer[32 * 128 * 3];
    memset(disk_buffer, 0, sizeof(disk_buffer));

    int offset = 0;
    int boot_sectors = 0;  // Track T0 non-empty before CCP header
    int ccp_header_offset = -1;

    for (int track = 0; track < 3; track++) {
      for (int sec = 1; sec <= 32; sec++) {
        if (drives[0]->read_sector(track, 0, sec, buffer)) {
          if (!is_empty_sector(buffer)) {
            // Check for CCP header
            if (buffer[0] == 0xC3 && buffer[3] == 0xC3) {
              uint16_t cold = buffer[1] | (buffer[2] << 8);
              uint16_t warm = buffer[4] | (buffer[5] << 8);
              if ((cold & 0xFF) == 0x5C && (warm & 0xFF) == 0x58) {
                ccp_header_offset = offset;
                fprintf(stderr, "CCP header at T%d S%d (buffer offset 0x%04X): cold=%04X warm=%04X\n",
                        track, sec, offset, cold, warm);
              }
            }
            memcpy(&disk_buffer[offset], buffer, 128);
            offset += 128;
          }
        }
      }
    }

    int total_loaded = offset;
    fprintf(stderr, "Loaded %d non-empty sectors (%d bytes) contiguously\n",
            total_loaded/128, total_loaded);

    if (ccp_header_offset < 0) {
      fprintf(stderr, "No CCP header found!\n");
      return false;
    }

    // The CCP header at S18 says DF00, but the ACTUAL CODE references DC00 addresses.
    // The boot loader at S13 also says destination is DC00.
    // The CCP header was created by MOVCPM for a different memory size but the
    // code was assembled for DC00. Use DC00 as the actual CCP base.

    // Skip boot sectors (S09, S13) and the misleading CCP header (S18)
    // Load the actual system code (S20 onwards in T0, then T1) to DC00

    // Find where the actual system code starts (first non-boot, non-header sector)
    int system_start = ccp_header_offset + 128;  // Skip the CCP header sector
    int system_size = total_loaded - system_start;
    uint16_t ccp_base = 0xDC00;  // From boot loader and code analysis

    // Check what the CCP header claimed vs what we're using
    uint16_t header_cold = disk_buffer[ccp_header_offset+1] | (disk_buffer[ccp_header_offset+2] << 8);
    fprintf(stderr, "CCP header says cold=%04X (CCP=%04X), but using DC00 based on code analysis\n",
            header_cold, header_cold - 0x5C);

    fprintf(stderr, "Loading system from buffer offset 0x%04X (%d bytes) to memory 0x%04X\n",
            system_start, system_size, ccp_base);

    memcpy(&m[ccp_base], &disk_buffer[system_start], system_size);

    // Also copy the CCP header itself to the correct location relative to DC00
    // The header's JMPs need to be patched to point to DC5C/DC58 instead of DF5C/DF58
    memcpy(&m[ccp_base], &disk_buffer[ccp_header_offset], 128);
    // Patch the JMPs
    m[ccp_base + 1] = 0x5C;  // Cold entry: DC5C
    m[ccp_base + 2] = 0xDC;
    m[ccp_base + 4] = 0x58;  // Warm entry: DC58
    m[ccp_base + 5] = 0xDC;

    fprintf(stderr, "Patched CCP header JMPs to DC5C/DC58\n");

    // Calculate BDOS and BIOS addresses from CCP base
    uint16_t bdos_base = ccp_base + 0x0800;   // CCP is 2K
    uint16_t bios_base = bdos_base + 0x0E00;  // BDOS is ~3.5K
    uint16_t bdos_entry = bdos_base + 0x06;   // BDOS entry at BDOS+6

    fprintf(stderr, "CP/M layout: CCP=%04X BDOS=%04X BIOS=%04X\n",
            ccp_base, bdos_base, bios_base);

    // Verify CCP header is now at correct location
    if ((uint8_t)m[ccp_base] == 0xC3) {
      uint16_t check_cold = (uint8_t)m[ccp_base+1] | ((uint8_t)m[ccp_base+2] << 8);
      fprintf(stderr, "Verified CCP at 0x%04X: first JMP target=%04X\n", ccp_base, check_cold);
    }

    // Set up page zero
    m[0x0000] = 0xC3;  // JMP WBOOT (BIOS+3)
    m[0x0001] = (bios_base + 3) & 0xFF;
    m[0x0002] = ((bios_base + 3) >> 8) & 0xFF;

    m[IOBYTE_ADDR] = 0x00;
    m[DRVUSER_ADDR] = 0x00;

    // BDOS entry
    m[BDOS_ENTRY] = 0xC3;
    m[BDOS_ENTRY + 1] = bdos_entry & 0xFF;
    m[BDOS_ENTRY + 2] = (bdos_entry >> 8) & 0xFF;

    // Clear default DMA area
    memset(&m[0x0080], 0, 128);

    // Set up BIOS trap table at the detected BIOS location
    for (int i = 0; i < 17; i++) {
      uint16_t addr = bios_base + (i * 3);
      uint16_t magic = BIOS_MAGIC + i;
      m[addr] = 0xC3;  // JMP
      m[addr + 1] = magic & 0xFF;
      m[addr + 2] = (magic >> 8) & 0xFF;
    }

    // Start at CCP cold entry
    cpu->regs.PC.set_pair16(ccp_base + 0x5C);
    cpu->regs.SP.set_pair16(ccp_base);

    fprintf(stderr, "CP/M booting from CCP cold entry at 0x%04X...\n", ccp_base + 0x5C);
    return true;
  }

  // Load boot sector and CP/M from disk
  bool cold_boot() {
    if (!drives[0]) {
      fprintf(stderr, "No disk in drive A:\n");
      return false;
    }

    // Check for SIMH format
    if (is_simh_disk()) {
      return cold_boot_simh();
    }

    char* m = mem->get_mem();
    uint8_t buffer[8192];  // Buffer for system tracks (CCP+BDOS is ~5.5K)
    const DiskGeometry& g = drives[0]->geometry();

    // Load system tracks (reserved tracks contain CCP+BDOS)
    fprintf(stderr, "Loading system tracks to find CP/M signature...\n");

    int sectors_loaded = 0;
    for (int track = 0; track < g.reserved_tracks; track++) {
      for (int sector = 1; sector <= g.sectors_per_track; sector++) {
        if (sectors_loaded * g.sector_size >= (int)sizeof(buffer)) break;
        if (!drives[0]->read_sector(track, 0, sector, &buffer[sectors_loaded * g.sector_size])) {
          break;
        }
        sectors_loaded++;
      }
    }

    fprintf(stderr, "Read %d sectors (%d bytes)\n", sectors_loaded, sectors_loaded * g.sector_size);

    // Find CCP signature (C3 xx xx C3 xx xx followed by "Copyrigh")
    // Note: header addresses may not match actual code addresses (generic images)
    // The boot loader is at offset 0, CCP header at 0x80, actual CCP code at 0x100
    uint16_t ccp_header_offset = 0;
    uint16_t ccp_code_offset = 0;  // Actual code starts after header
    uint16_t ccp_dest = 0;
    uint16_t header_ccp = 0;
    for (int i = 0; i < sectors_loaded * g.sector_size - 16; i++) {
      if (buffer[i] == 0xC3 && buffer[i+3] == 0xC3) {
        // Check for "Copyrigh" after the two jumps
        if (strncmp((char*)&buffer[i+8], "Copyrigh", 8) == 0 ||
            strncmp((char*)&buffer[i+8], "Copyright", 9) == 0) {
          uint16_t cold = buffer[i+1] | (buffer[i+2] << 8);
          uint16_t warm = buffer[i+4] | (buffer[i+5] << 8);
          header_ccp = cold - 0x5C;  // Header says CCP should be here
          ccp_header_offset = i;
          ccp_code_offset = i + 0x80;  // Actual code starts 0x80 bytes after header
          fprintf(stderr, "CCP header at offset 0x%04X, code at 0x%04X: cold=%04X warm=%04X (header suggests CCP=%04X)\n",
                  i, ccp_code_offset, cold, warm, header_ccp);

          // Scan code to find actual CALL addresses to determine real CCP base
          // Look for CALL instructions (CD xx xx) in the code after header
          for (int j = ccp_code_offset; j < ccp_code_offset + 0x300 && j < sectors_loaded * g.sector_size - 3; j++) {
            if (buffer[j] == 0xCD) {  // CALL instruction
              uint16_t target = buffer[j+1] | (buffer[j+2] << 8);
              // If target is in 0x7xxx-0xDxxx range, use it to determine CCP base
              if (target >= 0x7000 && target < 0xD000) {
                // CCP internal calls are typically to addresses like 7A8C, 7A92, etc.
                // The CCP base is the target rounded down to 0xx00
                ccp_dest = target & 0xFF00;
                if (ccp_dest >= 0x7000 && ccp_dest < 0xD000) {
                  fprintf(stderr, "Detected actual CCP base from CALL %04X: CCP=%04X\n", target, ccp_dest);
                  break;
                }
              }
            }
          }

          if (ccp_dest == 0) {
            ccp_dest = header_ccp;  // Fall back to header address
          }
          break;
        }
      }
    }

    if (ccp_dest == 0) {
      fprintf(stderr, "Error: CCP signature not found in system tracks\n");
      return false;
    }

    // Calculate BDOS and BIOS locations
    uint16_t bdos_dest = ccp_dest + 0x0800;  // BDOS follows CCP (2K)
    uint16_t bios_dest = bdos_dest + 0x0E00;  // BIOS follows BDOS (~3.5K)
    uint16_t bdos_entry = bdos_dest + 0x06;  // BDOS entry point

    fprintf(stderr, "CP/M layout: CCP=%04X BDOS=%04X BIOS=%04X\n",
            ccp_dest, bdos_dest, bios_dest);

    // Copy system from buffer to runtime location
    // The actual CCP code starts at ccp_code_offset (after boot loader and header)
    // We copy it to the detected CCP base address
    // CCP is 2K, BDOS is ~3.5K, total ~5.5K
    uint16_t system_size = sectors_loaded * g.sector_size - ccp_code_offset;
    if (system_size > 0x1800) system_size = 0x1800;  // Cap at 6K (CCP+BDOS)

    fprintf(stderr, "Copying %d bytes from disk offset 0x%04X to memory 0x%04X\n",
            system_size, ccp_code_offset, ccp_dest);
    memcpy(&m[ccp_dest], &buffer[ccp_code_offset], system_size);

    // Set up page zero
    m[0] = 0xC3;  // JMP WBOOT
    m[1] = (bios_dest + 3) & 0xFF;
    m[2] = ((bios_dest + 3) >> 8) & 0xFF;
    m[3] = 0;  // IOBYTE
    m[4] = 0;  // Current drive/user
    m[5] = 0xC3;  // JMP BDOS
    m[6] = bdos_entry & 0xFF;
    m[7] = (bdos_entry >> 8) & 0xFF;

    // Set up BIOS trap table
    for (int i = 0; i < 17; i++) {
      uint16_t addr = bios_dest + (i * 3);
      uint16_t magic = BIOS_MAGIC + i;
      m[addr] = 0xC3;  // JMP
      m[addr + 1] = magic & 0xFF;
      m[addr + 2] = (magic >> 8) & 0xFF;
    }

    // Clear default DMA area
    memset(&m[0x0080], 0, 128);

    // Start at CCP cold boot entry (CCP+5C)
    uint16_t ccp_cold = ccp_dest + 0x5C;
    fprintf(stderr, "Starting at CCP cold entry: 0x%04X\n", ccp_cold);
    cpu->regs.PC.set_pair16(ccp_cold);
    cpu->regs.SP.set_pair16(ccp_dest);  // Stack just below CCP

    return true;
  }

  // Handle PC to check for BIOS calls
  bool handle_pc(uint16_t pc) {
    // Check for BIOS magic addresses
    if (pc >= BIOS_MAGIC && pc < BIOS_MAGIC + 17) {
      int func = pc - BIOS_MAGIC;
      bios_call(func * 3);

      // Simulate RET
      uint16_t ret_addr = cpu->pop_word();
      cpu->regs.PC.set_pair16(ret_addr);
      return true;
    }

    // Check for JMP 0 (system reset)
    if (pc == 0) {
      fprintf(stderr, "System reset - warm boot\n");
      // Reload CCP and restart
      warm_boot();
      return true;
    }

    return false;
  }

  // Warm boot - reload CCP only
  void warm_boot() {
    char* m = mem->get_mem();

    // If using memory-loaded image, CCP is already in memory - don't reload from disk
    if (!memory_loaded && drives[0]) {
      uint8_t buffer[256];
      const DiskGeometry& g = drives[0]->geometry();

      // Reload CCP from track 0 (CCP is ~2K)
      uint16_t load_addr = ccp_base;
      int sector_size = g.sector_size;
      int ccp_sectors = (0x800 + sector_size - 1) / sector_size;

      for (int i = 0; i < ccp_sectors && i < g.sectors_per_track; i++) {
        if (drives[0]->read_sector(0, 0, i + 1, buffer)) {
          memcpy(&m[load_addr], buffer, sector_size);
          load_addr += sector_size;
        }
      }
    }

    // Reset page zero - use dynamic addresses
    m[0x0000] = 0xC3;
    m[0x0001] = (bios_base + BIOS_WBOOT) & 0xFF;
    m[0x0002] = ((bios_base + BIOS_WBOOT) >> 8) & 0xFF;

    // Clear command line buffer
    memset(&m[0x0080], 0, 128);

    // Jump to CCP - use dynamic address
    cpu->regs.PC.set_pair16(ccp_base);
    cpu->regs.SP.set_pair16(ccp_base);  // SP just below CCP
  }

  // BIOS function dispatcher
  void bios_call(int offset) {
    if (debug) {
      static const char* bios_names[] = {
        "BOOT", "WBOOT", "CONST", "CONIN", "CONOUT", "LIST",
        "PUNCH", "READER", "HOME", "SELDSK", "SETTRK", "SETSEC",
        "SETDMA", "READ", "WRITE", "LISTST", "SECTRAN"
      };
      int func = offset / 3;
      // Only log non-CONST calls to reduce noise
      if (offset != BIOS_CONST) {
        fprintf(stderr, "BIOS: %s (offset %d)\n",
                (func < 17) ? bios_names[func] : "?", offset);
      }
    }

    switch (offset) {
    case BIOS_BOOT:
      if (debug) fprintf(stderr, "BIOS: BOOT (cold)\n");
      // For memory-loaded images, cold boot acts like warm boot (system is already loaded)
      if (memory_loaded) {
        warm_boot();
      } else {
        cold_boot();
      }
      break;

    case BIOS_WBOOT:
      if (debug) fprintf(stderr, "BIOS: WBOOT (warm)\n");
      warm_boot();
      break;

    case BIOS_CONST:
      bios_const();
      break;

    case BIOS_CONIN:
      bios_conin();
      break;

    case BIOS_CONOUT:
      bios_conout();
      break;

    case BIOS_LIST:
      bios_list();
      break;

    case BIOS_PUNCH:
      bios_punch();
      break;

    case BIOS_READER:
      bios_reader();
      break;

    case BIOS_HOME:
      bios_home();
      break;

    case BIOS_SELDSK:
      bios_seldsk();
      break;

    case BIOS_SETTRK:
      bios_settrk();
      break;

    case BIOS_SETSEC:
      bios_setsec();
      break;

    case BIOS_SETDMA:
      bios_setdma();
      break;

    case BIOS_READ:
      bios_read();
      break;

    case BIOS_WRITE:
      bios_write();
      break;

    case BIOS_LISTST:
      bios_listst();
      break;

    case BIOS_SECTRAN:
      bios_sectran();
      break;

    default:
      fprintf(stderr, "Unknown BIOS function: offset %d\n", offset);
      break;
    }
  }

private:
  // BIOS: Console status
  void bios_const() {
    cpu->set_reg8(console_has_input() ? 0xFF : 0x00, qkz80::reg_A);
  }

  // BIOS: Console input
  void bios_conin() {
    cpu->set_reg8(console_read_char(), qkz80::reg_A);
  }

  // BIOS: Console output
  void bios_conout() {
    uint8_t ch = cpu->get_reg8(qkz80::reg_C);
    console_write_char(ch);
  }

  // BIOS: List (printer) output
  void bios_list() {
    uint8_t ch = cpu->get_reg8(qkz80::reg_C);
    console_printer_out(ch);
  }

  // BIOS: Punch output
  void bios_punch() {
    uint8_t ch = cpu->get_reg8(qkz80::reg_C);
    console_aux_out(ch);
  }

  // BIOS: Reader input
  void bios_reader() {
    cpu->set_reg8(console_aux_in(), qkz80::reg_A);
  }

  // BIOS: Home disk head
  void bios_home() {
    current_track = 0;
    current_head = 0;
    if (debug) fprintf(stderr, "BIOS: HOME\n");
  }

  // BIOS: Select disk
  void bios_seldsk() {
    int drive = cpu->get_reg8(qkz80::reg_C);

    if (debug) fprintf(stderr, "BIOS: SELDSK drive=%d\n", drive);

    if (drive < 0 || drive >= MAX_DRIVES || !drives[drive]) {
      // Return 0 (HL=0) to indicate error
      cpu->set_reg16(0, qkz80::regp_HL);
      return;
    }

    current_drive = drive;

    // Return address of DPH
    cpu->set_reg16(dph_addresses[drive], qkz80::regp_HL);
  }

  // BIOS: Set track
  void bios_settrk() {
    current_track = cpu->get_reg16(qkz80::regp_BC);
    if (debug) fprintf(stderr, "BIOS: SETTRK track=%d\n", current_track);
  }

  // BIOS: Set sector
  void bios_setsec() {
    current_sector = cpu->get_reg16(qkz80::regp_BC);
    if (debug) fprintf(stderr, "BIOS: SETSEC sector=%d\n", current_sector);
  }

  // BIOS: Set DMA address
  void bios_setdma() {
    current_dma = cpu->get_reg16(qkz80::regp_BC);
    if (debug) fprintf(stderr, "BIOS: SETDMA dma=0x%04X\n", current_dma);
  }

  // BIOS: Read sector
  void bios_read() {
    if (debug) {
      fprintf(stderr, "BIOS: READ drive=%d track=%d sector=%d dma=0x%04X\n",
              current_drive, current_track, current_sector, current_dma);
    }

    if (!drives[current_drive]) {
      cpu->set_reg8(1, qkz80::reg_A);  // Error
      return;
    }

    uint8_t buffer[1024];  // Large enough for any sector
    if (!drives[current_drive]->read_sector(current_track, current_head,
                                             current_sector, buffer)) {
      cpu->set_reg8(1, qkz80::reg_A);  // Error
      return;
    }

    // Copy to DMA
    char* m = mem->get_mem();
    int sector_size = drives[current_drive]->geometry().sector_size;
    memcpy(&m[current_dma], buffer, sector_size);

    cpu->set_reg8(0, qkz80::reg_A);  // Success
  }

  // BIOS: Write sector
  void bios_write() {
    if (debug) {
      fprintf(stderr, "BIOS: WRITE drive=%d track=%d sector=%d dma=0x%04X\n",
              current_drive, current_track, current_sector, current_dma);
    }

    if (!drives[current_drive]) {
      cpu->set_reg8(1, qkz80::reg_A);  // Error
      return;
    }

    if (drives[current_drive]->is_read_only()) {
      cpu->set_reg8(2, qkz80::reg_A);  // Write protected
      return;
    }

    char* m = mem->get_mem();
    if (!drives[current_drive]->write_sector(current_track, current_head,
                                              current_sector,
                                              (uint8_t*)&m[current_dma])) {
      cpu->set_reg8(1, qkz80::reg_A);  // Error
      return;
    }

    cpu->set_reg8(0, qkz80::reg_A);  // Success
  }

  // BIOS: List status
  void bios_listst() {
    cpu->set_reg8(console_printer_ready() ? 0xFF : 0x00, qkz80::reg_A);
  }

  // BIOS: Sector translation
  void bios_sectran() {
    uint16_t logical_sector = cpu->get_reg16(qkz80::regp_BC);
    uint16_t xlt_addr = cpu->get_reg16(qkz80::regp_DE);

    if (debug) {
      fprintf(stderr, "BIOS: SECTRAN logical=%d xlt=0x%04X\n",
              logical_sector, xlt_addr);
    }

    uint16_t physical_sector;
    if (xlt_addr == 0) {
      // No translation
      physical_sector = logical_sector;
    } else {
      // Use translation table
      char* m = mem->get_mem();
      physical_sector = (uint8_t)m[xlt_addr + logical_sector];
    }

    cpu->set_reg16(physical_sector, qkz80::regp_HL);
  }
};

// Main program
int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "CP/M BIOS-Level Emulator\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: %s [options] <disk_a.imd|.dsk> [disk_b] ...\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --8080        Run in 8080 mode (default)\n");
    fprintf(stderr, "  --z80         Run in Z80 mode\n");
    fprintf(stderr, "  --debug       Enable debug output\n");
    fprintf(stderr, "  --load-memory=FILE  Load memory image (from cpmemu --save-memory)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "This emulator boots real CP/M from disk images.\n");
    fprintf(stderr, "Supports .IMD (ImageDisk) and .dsk (raw) formats.\n");
    return 1;
  }

  // Parse command line
  int arg_offset = 1;
  bool mode_8080 = true;  // Default to 8080 for CP/M
  bool debug = false;
  const char* load_memory_file = nullptr;

  while (arg_offset < argc && argv[arg_offset][0] == '-') {
    if (strcmp(argv[arg_offset], "--8080") == 0) {
      mode_8080 = true;
      arg_offset++;
    } else if (strcmp(argv[arg_offset], "--z80") == 0) {
      mode_8080 = false;
      arg_offset++;
    } else if (strcmp(argv[arg_offset], "--debug") == 0) {
      debug = true;
      arg_offset++;
    } else if (strncmp(argv[arg_offset], "--load-memory=", 14) == 0) {
      load_memory_file = argv[arg_offset] + 14;
      arg_offset++;
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[arg_offset]);
      return 1;
    }
  }

  if (arg_offset >= argc && !load_memory_file) {
    fprintf(stderr, "Error: No disk image specified\n");
    return 1;
  }

  // Initialize console
  console_init();
  console_enable_raw_mode();

  // Create CPU and memory
  qkz80_cpu_mem memory;
  qkz80 cpu(&memory);
  cpu.set_cpu_mode(mode_8080 ? qkz80::MODE_8080 : qkz80::MODE_Z80);
  fprintf(stderr, "CPU mode: %s\n", mode_8080 ? "8080" : "Z80");

  // Create emulator
  CPMBiosEmulator emu(&cpu, &memory, debug);

  // Handle memory loading or disk boot
  if (load_memory_file) {
    // Load pre-built memory image (from cpmemu --save-memory after running MOVCPM)
    FILE* fp = fopen(load_memory_file, "rb");
    if (!fp) {
      fprintf(stderr, "Cannot open memory image: %s\n", load_memory_file);
      return 1;
    }

    char* m = memory.get_mem();
    // SAVE command saves memory starting at 0x100, so load there
    size_t loaded = fread(&m[0x100], 1, 65536 - 0x100, fp);
    fclose(fp);
    fprintf(stderr, "Loaded %zu bytes from %s at 0x0100\n", loaded, load_memory_file);

    // The MOVCPM image has CCP+BDOS in low memory (0x0880) and expects to run
    // at higher addresses. Copy to runtime location.
    // MOVCPM for this Altair builds for 48K by default (CCP at 0x9000/0xBD00)
    // We need to relocate from image location to runtime location.

    // MOVCPM places CP/M image at 0x900, with CCP at 0x980
    // The image is already relocated for the target memory size
    // CCP signature: C3 xx xx C3 xx xx where xx xx are high memory addresses
    uint16_t ccp_src = 0x0980;  // Standard MOVCPM CCP location

    // Verify CCP signature and get target addresses
    if ((uint8_t)m[ccp_src] == 0xC3 && (uint8_t)m[ccp_src+3] == 0xC3) {
      uint16_t ccp_cold = (uint8_t)m[ccp_src+1] | ((uint8_t)m[ccp_src+2] << 8);
      uint16_t ccp_warm = (uint8_t)m[ccp_src+4] | ((uint8_t)m[ccp_src+5] << 8);

      // CCP entry points: cold at CCP+5C, warm at CCP+58
      // Standard layout: CCP then BDOS then BIOS
      uint16_t ccp_dest = ccp_cold - 0x5C;
      uint16_t bdos_dest = ccp_dest + 0x0800;  // CCP is 2K, BDOS follows
      uint16_t bios_dest = bdos_dest + 0x0E00;  // BDOS is ~3.5K, BIOS follows
      uint16_t bdos_entry = bdos_dest + 0x06;  // BDOS entry is BDOS+6

      fprintf(stderr, "CP/M layout from MOVCPM image:\n");
      fprintf(stderr, "  CCP:  %04X (cold=%04X, warm=%04X)\n", ccp_dest, ccp_cold, ccp_warm);
      fprintf(stderr, "  BDOS: %04X (entry=%04X)\n", bdos_dest, bdos_entry);
      fprintf(stderr, "  BIOS: %04X\n", bios_dest);

      // System size: CCP(2K) + BDOS(3.5K) + BIOS stub
      // Copy entire image from 0x980 to end
      uint16_t system_size = 0x1600;  // ~5.5K for CCP+BDOS

      // Copy from source to destination
      fprintf(stderr, "Relocating CCP+BDOS: 0x%04X -> 0x%04X (%d bytes)\n",
              ccp_src, ccp_dest, system_size);
      memmove(&m[ccp_dest], &m[ccp_src], system_size);

      // Set up page zero for this configuration
      // JMP WBOOT at 0x0000 (BIOS+3)
      m[0] = 0xC3;
      m[1] = (bios_dest + 3) & 0xFF;
      m[2] = ((bios_dest + 3) >> 8) & 0xFF;
      m[3] = 0;  // IOBYTE
      m[4] = 0;  // Current drive/user
      // JMP BDOS at 0x0005
      m[5] = 0xC3;
      m[6] = bdos_entry & 0xFF;
      m[7] = (bdos_entry >> 8) & 0xFF;

      // Set up BIOS jump table with our traps at bios_dest
      // BIOS_MAGIC is 0xFF00 - addresses that we trap
      for (int i = 0; i < 17; i++) {
        uint16_t addr = bios_dest + (i * 3);
        uint16_t magic = 0xFF00 + i;  // Match CPMBiosEmulator::BIOS_MAGIC
        m[addr] = 0xC3;  // JMP
        m[addr + 1] = magic & 0xFF;
        m[addr + 2] = (magic >> 8) & 0xFF;
      }

      // Tell emulator about CP/M addresses for warm_boot()
      emu.set_cpm_addresses(ccp_dest, bdos_dest, bios_dest);

      // Start at CCP
      cpu.regs.PC.set_pair16(ccp_dest);
      cpu.regs.SP.set_pair16(ccp_dest);  // SP just below CCP
    } else {
      fprintf(stderr, "Error: CCP signature not found at 0x%04X\n", ccp_src);
      fprintf(stderr, "Bytes: %02X %02X %02X %02X\n",
              (uint8_t)m[ccp_src], (uint8_t)m[ccp_src+1],
              (uint8_t)m[ccp_src+2], (uint8_t)m[ccp_src+3]);
      return 1;
    }

    // Mount any disk images specified
    for (int i = 0; i + arg_offset < argc && i < MAX_DRIVES; i++) {
      emu.mount_drive(i, argv[arg_offset + i]);
    }

    // Also set up disk parameter structures
    emu.setup_disk_parameters();
  } else {
    // Mount disk images
    for (int i = 0; i + arg_offset < argc && i < MAX_DRIVES; i++) {
      if (!emu.mount_drive(i, argv[arg_offset + i])) {
        return 1;
      }
    }

    // Cold boot - loads CCP+BDOS from disk and sets up BIOS traps
    if (!emu.cold_boot()) {
      fprintf(stderr, "Cold boot failed\n");
      return 1;
    }
    // Note: cold_boot() now sets up BIOS traps at the correct location
  }

  // Debug: show what's at the BIOS area after setup
  if (debug) {
    char* m = memory.get_mem();
    fprintf(stderr, "BIOS jump table at 0x%04X:\n", BIOS_BASE);
    for (int i = 0; i < 6; i++) {
      uint16_t addr = BIOS_BASE + (i * 3);
      fprintf(stderr, "  %04X: %02X %02X %02X\n", addr,
              (uint8_t)m[addr], (uint8_t)m[addr+1], (uint8_t)m[addr+2]);
    }
    fprintf(stderr, "Starting at PC=0x%04X SP=0x%04X\n",
            cpu.regs.PC.get_pair16(), cpu.regs.SP.get_pair16());
  }

  fprintf(stderr, "CP/M booting...\n");

  // Run
  long long instruction_count = 0;
  long long max_instructions = 9000000000LL;

  while (true) {
    uint16_t pc = cpu.regs.PC.get_pair16();

    // Debug trace - focus on BIOS calls
    if (debug) {
      // Show first 200, and any access to BIOS area (0xF200+) or trap area (0xFF00+)
      if (instruction_count < 200) {
        char* m = memory.get_mem();
        fprintf(stderr, "[%lld] PC=%04X: %02X %02X %02X\n", instruction_count, pc,
                (uint8_t)m[pc], (uint8_t)m[pc+1], (uint8_t)m[pc+2]);
      } else if (pc >= 0xF200 || pc >= 0xFF00) {
        fprintf(stderr, "[%lld] BIOS/TRAP PC=%04X\n", instruction_count, pc);
      }
    }

    // Check for BIOS calls
    if (emu.handle_pc(pc)) {
      continue;
    }

    // Execute one instruction
    cpu.execute();

    instruction_count++;
    if (instruction_count >= max_instructions) {
      fprintf(stderr, "Reached instruction limit\n");
      break;
    }
  }

  return 0;
}
