/*
 * CP/M 2.2 Emulator - WebAssembly Version
 *
 * Compiles with Emscripten to run in browser.
 * Console I/O via JavaScript callbacks, disk images preloaded.
 *
 * Uses assembled BIOS (bios.bin) for disk parameter tables.
 * Addresses are read from bios.sym at build time.
 */

#include "../src/qkz80.h"
#include "../src/qkz80_mem.h"
#include <emscripten.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <vector>

// CP/M constants - these match cpm22.asm built for 63K
constexpr uint16_t CPM_LOAD_ADDR = 0xE000;   // CCP+BDOS load address
constexpr uint16_t BIOS_BASE = 0xF600;       // BIOS entry points

// BIOS layout from bios.sym (assembled from bios.asm)
// Jump table: F600-F632 (17 entries * 3 bytes = 51 bytes) - PROTECTED
// XLTTAB:   F633 (26 bytes, static skew table) - PROTECTED
// DPB0:     F64D (15 bytes, disk parameter block) - PROTECTED
// DPH0:     F65C (16 bytes, has BDOS scratch areas) - scratch writable
// DPH1:     F66C (16 bytes, has BDOS scratch areas) - scratch writable
// DPH2:     F67C (16 bytes, has BDOS scratch areas) - scratch writable
// DPH3:     F68C (16 bytes, has BDOS scratch areas) - scratch writable
// DIRBUF:   F69C (128 bytes, work area) - MUST BE WRITABLE
// CSV0-3:   F71C-F74B (16 bytes each) - MUST BE WRITABLE
// ALV0-3:   F75C-F7D7 (31 bytes each) - MUST BE WRITABLE
// End:      F7D8 (472 bytes total)
constexpr uint16_t BIOS_END = 0xF7D8;        // End of BIOS area
constexpr uint16_t DPH0_ADDR = 0xF65C;       // From bios.sym
constexpr uint16_t DPH1_ADDR = 0xF66C;       // From bios.sym
constexpr uint16_t DPH2_ADDR = 0xF67C;       // From bios.sym
constexpr uint16_t DPH3_ADDR = 0xF68C;       // From bios.sym
constexpr uint16_t DIRBUF_ADDR = 0xF69C;     // Work areas start here (writable)

// BIOS entry point offsets (relative to BIOS_BASE)
enum BiosEntry {
  BIOS_BOOT    = 0x00, BIOS_WBOOT   = 0x03, BIOS_CONST   = 0x06,
  BIOS_CONIN   = 0x09, BIOS_CONOUT  = 0x0C, BIOS_LIST    = 0x0F,
  BIOS_PUNCH   = 0x12, BIOS_READER  = 0x15, BIOS_HOME    = 0x18,
  BIOS_SELDSK  = 0x1B, BIOS_SETTRK  = 0x1E, BIOS_SETSEC  = 0x21,
  BIOS_SETDMA  = 0x24, BIOS_READ    = 0x27, BIOS_WRITE   = 0x2A,
  BIOS_PRSTAT  = 0x2D, BIOS_SECTRN  = 0x30,
};

// Disk geometry for 8" SSSD (from assembled BIOS DPB)
// Standard IBM 8" single-sided single-density format:
//   77 tracks, 26 sectors/track, 128 bytes/sector = 256,256 bytes
//   No sector skew (disk images stored in sequential order)
constexpr int TRACKS = 77;
constexpr int SECTORS = 26;
constexpr int SECTOR_SIZE = 128;
constexpr int TRACK_SIZE = SECTORS * SECTOR_SIZE;
constexpr int DISK_SIZE = TRACKS * TRACK_SIZE;

// Memory class with write protection for BIOS area
class cpm_mem : public qkz80_cpu_mem {
  uint16_t protect_start = 0;
  uint16_t protect_end = 0;
  bool protection_enabled = false;
  bool protection_fatal = true;

public:
  void set_write_protection(uint16_t start, uint16_t end, bool fatal = true) {
    protect_start = start;
    protect_end = end;
    protection_enabled = true;
    protection_fatal = fatal;
  }

  void disable_write_protection() {
    protection_enabled = false;
  }

  void store_mem(qkz80_uint16 addr, qkz80_uint8 abyte) override {
    if (protection_enabled && addr >= protect_start && addr < protect_end) {
      // Write to protected memory!
      fprintf(stderr, "\n*** WRITE PROTECT VIOLATION ***\n");
      fprintf(stderr, "Address: 0x%04X, Value: 0x%02X\n", addr, abyte);
      fprintf(stderr, "Protected range: 0x%04X - 0x%04X\n", protect_start, protect_end);
      if (protection_fatal) {
        fprintf(stderr, "FATAL: Exiting.\n");
        exit(1);
      }
      return;  // Silently ignore if not fatal
    }
    qkz80_cpu_mem::store_mem(addr, abyte);
  }

  bool is_bios_trap(uint16_t pc) {
    return pc >= BIOS_BASE && pc < BIOS_BASE + 0x33;
  }
};

// Global state
static cpm_mem memory;
static qkz80 cpu(&memory);
static std::queue<int> input_queue;
static std::vector<uint8_t> disk_a;
static std::vector<uint8_t> disk_b;
static int current_disk = 0;
static int current_track = 0;
static int current_sector = 1;
static uint16_t dma_addr = 0x0080;
static bool running = false;
static bool waiting_for_input = false;

// JavaScript callbacks
EM_JS(void, js_console_output, (int ch), {
  if (Module.onConsoleOutput) Module.onConsoleOutput(ch);
});

EM_JS(void, js_status, (const char* msg), {
  if (Module.onStatus) Module.onStatus(UTF8ToString(msg));
});

// Debug output
EM_JS(void, js_debug, (int track, int sector, int dma, int first_byte), {
  console.log('READ T:' + track + ' S:' + sector + ' DMA:' + dma.toString(16) + ' [' + first_byte.toString(16) + ']');
});

EM_JS(void, js_seldsk, (int disk, int dph, int e_reg, int bc_val), {
  console.log('SELDSK C=' + disk + ' (' + String.fromCharCode(65 + disk) + ':) BC=' + bc_val.toString(16) + ' E=' + e_reg + ' -> DPH=' + dph.toString(16));
});

// No sector skew - disk image is in logical order (sectors 0-25)
// BIOS receives sectors 1-26 after SECTRN adds 1, so we just subtract 1

// Get current disk image based on selected drive
static std::vector<uint8_t>* get_current_disk() {
  switch (current_disk) {
    case 0: return &disk_a;
    case 1: return &disk_b;
    default: return nullptr;
  }
}

// Disk read - sector is 1-based (SECTRN adds 1), disk image is 0-based sequential
static int disk_read(int track, int sector) {
  std::vector<uint8_t>* disk = get_current_disk();
  if (!disk || disk->empty()) return 1;
  // Convert 1-based sector (1-26) to 0-based (0-25) for disk image access
  int logical_sector = sector - 1;
  int offset = track * TRACK_SIZE + logical_sector * SECTOR_SIZE;
  if (offset < 0 || offset + SECTOR_SIZE > (int)disk->size()) return 1;
  char* mem = cpu.get_mem();
  memcpy(&mem[dma_addr], &(*disk)[offset], SECTOR_SIZE);
  js_debug(track, sector, dma_addr, (uint8_t)(*disk)[offset]);
  return 0;
}

// Disk write - sector is 1-based (SECTRN adds 1), disk image is 0-based sequential
static int disk_write(int track, int sector) {
  std::vector<uint8_t>* disk = get_current_disk();
  if (!disk || disk->empty()) return 1;
  // Convert 1-based sector (1-26) to 0-based (0-25) for disk image access
  int logical_sector = sector - 1;
  int offset = track * TRACK_SIZE + logical_sector * SECTOR_SIZE;
  if (offset < 0 || offset + SECTOR_SIZE > (int)disk->size()) return 1;
  char* mem = cpu.get_mem();
  memcpy(&(*disk)[offset], &mem[dma_addr], SECTOR_SIZE);
  return 0;
}

// BIOS trap handler
static bool handle_bios(uint16_t pc) {
  int offset = pc - BIOS_BASE;
  char* mem = cpu.get_mem();

  // Return helper
  auto do_ret = [&]() {
    uint16_t sp = cpu.regs.SP.get_pair16();
    uint16_t ret_addr = (uint8_t)mem[sp] | ((uint8_t)mem[sp+1] << 8);
    cpu.regs.SP.set_pair16(sp + 2);
    cpu.regs.PC.set_pair16(ret_addr);
  };

  switch (offset) {
    case BIOS_BOOT: {
      // Cold boot - disk tables are already loaded from bios.bin
      // Set up page zero
      mem[0x0000] = 0xC3;  // JMP WBOOT
      mem[0x0001] = static_cast<char>((BIOS_BASE + BIOS_WBOOT) & 0xFF);
      mem[0x0002] = static_cast<char>((BIOS_BASE + BIOS_WBOOT) >> 8);
      mem[0x0003] = 0x00;  // IOBYTE
      mem[0x0004] = 0x00;  // Current drive/user
      mem[0x0005] = 0xC3;  // JMP BDOS
      mem[0x0006] = 0x06;  // BDOS entry at E806 (from cpm22.sym)
      mem[0x0007] = 0xE8;

      current_disk = 0;
      current_track = 0;
      current_sector = 1;
      dma_addr = 0x0080;

      cpu.regs.BC.set_pair16(0x0000);
      cpu.regs.PC.set_pair16(CPM_LOAD_ADDR);
      js_status("CP/M Cold Boot");
      return true;
    }

    case BIOS_WBOOT: {
      mem[0x0000] = 0xC3;
      mem[0x0001] = static_cast<char>((BIOS_BASE + BIOS_WBOOT) & 0xFF);
      mem[0x0002] = static_cast<char>((BIOS_BASE + BIOS_WBOOT) >> 8);
      mem[0x0005] = 0xC3;
      mem[0x0006] = 0x06;
      mem[0x0007] = 0xE8;
      dma_addr = 0x0080;
      // C register = current drive (from location 4, low nibble)
      // Warm boot enters CCP at offset +3
      cpu.regs.BC.set_pair16(static_cast<uint8_t>(mem[0x0004]) & 0x0F);
      cpu.regs.PC.set_pair16(CPM_LOAD_ADDR + 3);  // Warm boot entry
      return true;
    }

    case BIOS_CONST: {
      cpu.set_reg8(input_queue.empty() ? 0x00 : 0xFF, qkz80::reg_A);
      do_ret();
      return true;
    }

    case BIOS_CONIN: {
      if (input_queue.empty()) {
        waiting_for_input = true;
        return true;  // Will retry
      }
      int ch = input_queue.front();
      input_queue.pop();
      cpu.set_reg8(ch & 0x7F, qkz80::reg_A);
      do_ret();
      return true;
    }

    case BIOS_CONOUT: {
      js_console_output(cpu.get_reg8(qkz80::reg_C) & 0x7F);
      do_ret();
      return true;
    }

    case BIOS_LIST:
    case BIOS_PUNCH:
      do_ret();
      return true;

    case BIOS_READER:
      cpu.set_reg8(0x1A, qkz80::reg_A);  // EOF
      do_ret();
      return true;

    case BIOS_HOME:
      current_track = 0;
      do_ret();
      return true;

    case BIOS_SELDSK: {
      int disk = cpu.get_reg8(qkz80::reg_C);
      int e_reg = cpu.get_reg8(qkz80::reg_E);  // E=0 means first select, E=1 means already logged in
      uint16_t dph = 0;

      // Return DPH address from assembled BIOS tables (4 drives: A-D)
      static const uint16_t dph_table[4] = { DPH0_ADDR, DPH1_ADDR, DPH2_ADDR, DPH3_ADDR };
      if (disk < 4) {
        dph = dph_table[disk];
        current_disk = disk;
      } else {
        dph = 0;  // Invalid disk - return 0 to signal error
      }

      js_seldsk(disk, dph, e_reg, cpu.regs.BC.get_pair16());
      cpu.regs.HL.set_pair16(dph);
      do_ret();
      return true;
    }

    case BIOS_SETTRK:
      current_track = cpu.regs.BC.get_pair16();
      do_ret();
      return true;

    case BIOS_SETSEC:
      current_sector = cpu.regs.BC.get_pair16();
      do_ret();
      return true;

    case BIOS_SETDMA:
      dma_addr = cpu.regs.BC.get_pair16();
      do_ret();
      return true;

    case BIOS_READ:
      cpu.set_reg8(disk_read(current_track, current_sector), qkz80::reg_A);
      do_ret();
      return true;

    case BIOS_WRITE:
      cpu.set_reg8(disk_write(current_track, current_sector), qkz80::reg_A);
      do_ret();
      return true;

    case BIOS_PRSTAT:
      cpu.set_reg8(0xFF, qkz80::reg_A);
      do_ret();
      return true;

    case BIOS_SECTRN: {
      // Sector translation - BC=logical sector, DE=XLT table address
      // XLTTAB at F633 contains the IBM 8" skew table
      uint16_t logical = cpu.regs.BC.get_pair16();
      uint16_t xlt = cpu.regs.DE.get_pair16();
      uint16_t physical;

      if (xlt == 0) {
        // No translation table - use 1-based physical sector
        physical = logical + 1;
      } else {
        // Use the translation table from assembled BIOS
        physical = (uint8_t)mem[xlt + logical];
      }
      cpu.regs.HL.set_pair16(physical);
      do_ret();
      return true;
    }
  }
  return false;
}

// Run one batch of instructions
static void run_batch() {
  if (!running || waiting_for_input) return;

  for (int i = 0; i < 10000; i++) {
    uint16_t pc = cpu.regs.PC.get_pair16();

    if (memory.is_bios_trap(pc)) {
      if (!handle_bios(pc)) break;
      if (waiting_for_input) return;
      continue;
    }

    cpu.execute();
  }
}

// Main loop callback
static void main_loop() {
  run_batch();
}

// Exported: Send key input
extern "C" EMSCRIPTEN_KEEPALIVE
void cpm_key_input(int ch) {
  if (ch == '\n') ch = '\r';
  input_queue.push(ch);
  waiting_for_input = false;
}

// Exported: Load CP/M system (cpm22.sys with CCP+BDOS)
extern "C" EMSCRIPTEN_KEEPALIVE
int cpm_load_system(const char* data, int size) {
  if (size > 0x2000) size = 0x2000;  // Max 8KB for CCP+BDOS
  char* mem = cpu.get_mem();
  memcpy(&mem[CPM_LOAD_ADDR], data, size);
  js_status("System loaded");
  return 0;
}

// Exported: Load BIOS (bios.bin assembled from bios.asm)
extern "C" EMSCRIPTEN_KEEPALIVE
int cpm_load_bios(const char* data, int size) {
  char* mem = cpu.get_mem();
  memcpy(&mem[BIOS_BASE], data, size);
  fprintf(stderr, "BIOS loaded: %d bytes at 0x%04X\n", size, BIOS_BASE);
  return 0;
}

// Exported: Load disk A image
extern "C" EMSCRIPTEN_KEEPALIVE
int cpm_load_disk(const char* data, int size) {
  disk_a.assign(data, data + size);
  if (disk_a.size() < DISK_SIZE) {
    disk_a.resize(DISK_SIZE, 0xE5);
  }
  js_status("Disk A loaded");
  return 0;
}

// Exported: Load disk B image
extern "C" EMSCRIPTEN_KEEPALIVE
int cpm_load_disk_b(const char* data, int size) {
  disk_b.assign(data, data + size);
  if (disk_b.size() < DISK_SIZE) {
    disk_b.resize(DISK_SIZE, 0xE5);
  }
  js_status("Disk B loaded");
  return 0;
}

// Exported: Start emulation
extern "C" EMSCRIPTEN_KEEPALIVE
void cpm_start() {
  cpu.set_cpu_mode(qkz80::MODE_8080);
  cpu.regs.AF.set_pair16(0);
  cpu.regs.BC.set_pair16(0);
  cpu.regs.DE.set_pair16(0);
  cpu.regs.HL.set_pair16(0);
  cpu.regs.PC.set_pair16(BIOS_BASE);  // Start at BIOS BOOT
  cpu.regs.SP.set_pair16(CPM_LOAD_ADDR);

  // Enable write protection on BIOS code and static tables only
  // DPH scratch areas, DIRBUF, CSV, ALV must be writable by BDOS
  // Protect: jump table (F600-F632), XLT (F633-F64C), DPB (F64D-F65B)
  memory.set_write_protection(BIOS_BASE, DPH0_ADDR, true);

  running = true;
  waiting_for_input = false;
  js_status("Starting CP/M...");
}

// Exported: Stop emulation
extern "C" EMSCRIPTEN_KEEPALIVE
void cpm_stop() {
  running = false;
}

// Exported: Get disk A data for download
extern "C" EMSCRIPTEN_KEEPALIVE
const uint8_t* cpm_get_disk_data() {
  return disk_a.data();
}

extern "C" EMSCRIPTEN_KEEPALIVE
int cpm_get_disk_size() {
  return disk_a.size();
}

// Exported: Get disk B data for download
extern "C" EMSCRIPTEN_KEEPALIVE
const uint8_t* cpm_get_disk_b_data() {
  return disk_b.data();
}

extern "C" EMSCRIPTEN_KEEPALIVE
int cpm_get_disk_b_size() {
  return disk_b.size();
}

// Exported: Auto-start with bundled files
extern "C" EMSCRIPTEN_KEEPALIVE
int cpm_autostart() {
  char* mem = cpu.get_mem();

  // Load bundled BIOS (bios.sys) first
  FILE* f = fopen("/bios.sys", "rb");
  if (!f) {
    js_status("Error: bios.sys not found");
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  fread(&mem[BIOS_BASE], 1, size, f);
  fclose(f);
  fprintf(stderr, "Loaded bios.sys: %ld bytes at 0x%04X\n", size, BIOS_BASE);

  // Load bundled cpm22.sys (CCP+BDOS)
  f = fopen("/cpm22.sys", "rb");
  if (!f) {
    js_status("Error: cpm22.sys not found");
    return -1;
  }
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  fread(&mem[CPM_LOAD_ADDR], 1, size, f);
  fclose(f);
  fprintf(stderr, "Loaded cpm22.sys: %ld bytes at 0x%04X\n", size, CPM_LOAD_ADDR);

  // Load bundled disk
  f = fopen("/drivea", "rb");
  if (!f) {
    js_status("Error: drivea not found");
    return -1;
  }
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  disk_a.resize(size);
  fread(disk_a.data(), 1, size, f);
  fclose(f);

  if (disk_a.size() < DISK_SIZE) {
    disk_a.resize(DISK_SIZE, 0xE5);
  }

  // Start emulation
  cpm_start();
  return 0;
}

int main() {
  js_status("CP/M Emulator ready");
  emscripten_set_main_loop(main_loop, 0, 0);
  return 0;
}
