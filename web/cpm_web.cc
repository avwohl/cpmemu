/*
 * CP/M 2.2 Emulator - WebAssembly Version
 *
 * Compiles with Emscripten to run in browser.
 * Console I/O via JavaScript callbacks, disk images preloaded.
 */

#include "../src/qkz80.h"
#include "../src/qkz80_mem.h"
#include <emscripten.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <vector>

// CP/M constants
constexpr uint16_t CPM_LOAD_ADDR = 0xE000;
constexpr uint16_t BIOS_BASE = 0xF600;

// BIOS entry points
enum BiosEntry {
  BIOS_BOOT    = 0x00, BIOS_WBOOT   = 0x03, BIOS_CONST   = 0x06,
  BIOS_CONIN   = 0x09, BIOS_CONOUT  = 0x0C, BIOS_LIST    = 0x0F,
  BIOS_PUNCH   = 0x12, BIOS_READER  = 0x15, BIOS_HOME    = 0x18,
  BIOS_SELDSK  = 0x1B, BIOS_SETTRK  = 0x1E, BIOS_SETSEC  = 0x21,
  BIOS_SETDMA  = 0x24, BIOS_READ    = 0x27, BIOS_WRITE   = 0x2A,
  BIOS_PRSTAT  = 0x2D, BIOS_SECTRN  = 0x30,
};

// Disk geometry for 8" SSSD
constexpr int TRACKS = 77;
constexpr int SECTORS = 26;
constexpr int SECTOR_SIZE = 128;
constexpr int TRACK_SIZE = SECTORS * SECTOR_SIZE;
constexpr int DISK_SIZE = TRACKS * TRACK_SIZE;

// Disk Parameter Block for 8" SSSD - placed high to avoid cpm22.sys overlap
constexpr uint16_t DPB_ADDR = 0xFE00;
constexpr uint16_t DIRBUF_ADDR = 0xFE20;
constexpr uint16_t ALV_ADDR = 0xFEA0;
constexpr uint16_t CSV_ADDR = 0xFEC0;
constexpr uint16_t DPH_BASE = 0xFEE0;

// Memory class
class cpm_mem : public qkz80_cpu_mem {
public:
  bool is_bios_trap(uint16_t pc) {
    return pc >= BIOS_BASE && pc < BIOS_BASE + 0x33;
  }
};

// Global state
static cpm_mem memory;
static qkz80 cpu(&memory);
static std::queue<int> input_queue;
static std::vector<uint8_t> disk_a;
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

// Initialize disk parameter tables
static void init_disk_tables() {
  char* mem = cpu.get_mem();

  // Zero DIRBUF, ALV, and CSV areas only
  memset(&mem[DIRBUF_ADDR], 0, 128);  // DIRBUF
  memset(&mem[ALV_ADDR], 0, 32);       // ALV (243 blocks / 8 = 31 bytes)
  memset(&mem[CSV_ADDR], 0, 16);       // CSV

  // DPB for 8" SSSD: SPT=26, BSH=3, BLM=7, EXM=0, DSM=242, DRM=63, AL0=0xC0, AL1=0, CKS=16, OFF=2
  uint8_t dpb[] = {26, 0, 3, 7, 0, 242, 0, 63, 0, 0xC0, 0x00, 16, 0, 2, 0};
  memcpy(&mem[DPB_ADDR], dpb, sizeof(dpb));

  // DPH for drive A (16 bytes): XLT, 3x scratch, DIRBUF, DPB, CSV, ALV
  mem[DPH_BASE + 0] = 0; mem[DPH_BASE + 1] = 0;   // XLT = 0
  mem[DPH_BASE + 2] = 0; mem[DPH_BASE + 3] = 0;   // scratch
  mem[DPH_BASE + 4] = 0; mem[DPH_BASE + 5] = 0;   // scratch
  mem[DPH_BASE + 6] = 0; mem[DPH_BASE + 7] = 0;   // scratch
  mem[DPH_BASE + 8] = DIRBUF_ADDR & 0xFF; mem[DPH_BASE + 9] = DIRBUF_ADDR >> 8;
  mem[DPH_BASE + 10] = DPB_ADDR & 0xFF; mem[DPH_BASE + 11] = DPB_ADDR >> 8;
  mem[DPH_BASE + 12] = CSV_ADDR & 0xFF; mem[DPH_BASE + 13] = CSV_ADDR >> 8;
  mem[DPH_BASE + 14] = ALV_ADDR & 0xFF; mem[DPH_BASE + 15] = ALV_ADDR >> 8;
}

// Debug output
EM_JS(void, js_debug, (int track, int sector, int dma, int first_byte), {
  console.log('READ T:' + track + ' S:' + sector + ' DMA:' + dma.toString(16) + ' [' + first_byte.toString(16) + ']');
});

EM_JS(void, js_seldsk, (int disk), {
  console.log('SELDSK drive=' + disk + ' (' + String.fromCharCode(65 + disk) + ':)');
});

// Disk read
static int disk_read(int track, int sector) {
  if (disk_a.empty()) return 1;
  int offset = track * TRACK_SIZE + (sector - 1) * SECTOR_SIZE;
  if (offset < 0 || offset + SECTOR_SIZE > (int)disk_a.size()) return 1;
  char* mem = cpu.get_mem();
  memcpy(&mem[dma_addr], &disk_a[offset], SECTOR_SIZE);
  js_debug(track, sector, dma_addr, (uint8_t)disk_a[offset]);
  return 0;
}

// Disk write
static int disk_write(int track, int sector) {
  if (disk_a.empty()) return 1;
  int offset = track * TRACK_SIZE + (sector - 1) * SECTOR_SIZE;
  if (offset < 0 || offset + SECTOR_SIZE > (int)disk_a.size()) return 1;
  char* mem = cpu.get_mem();
  memcpy(&disk_a[offset], &mem[dma_addr], SECTOR_SIZE);
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
      init_disk_tables();
      mem[0x0000] = 0xC3; mem[0x0001] = 0x03; mem[0x0002] = 0xF6;
      mem[0x0003] = 0x00; mem[0x0004] = 0x00;
      mem[0x0005] = 0xC3; mem[0x0006] = 0x06; mem[0x0007] = 0xE8;
      cpu.regs.BC.set_pair16(0x0000);
      cpu.regs.PC.set_pair16(CPM_LOAD_ADDR);
      js_status("CP/M Cold Boot");
      return true;
    }

    case BIOS_WBOOT: {
      mem[0x0000] = 0xC3; mem[0x0001] = 0x03; mem[0x0002] = 0xF6;
      mem[0x0005] = 0xC3; mem[0x0006] = 0x06; mem[0x0007] = 0xE8;
      cpu.regs.BC.set_pair16(mem[0x0004] & 0x0F);
      cpu.regs.PC.set_pair16(CPM_LOAD_ADDR);
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
      js_seldsk(disk);
      cpu.regs.HL.set_pair16(disk == 0 ? DPH_BASE : 0);
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
      uint16_t logical = cpu.regs.BC.get_pair16();
      cpu.regs.HL.set_pair16(logical + 1);  // 1-based physical
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

    cpu.execute();  // Execute one instruction
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

// Exported: Load CP/M system
extern "C" EMSCRIPTEN_KEEPALIVE
int cpm_load_system(const char* data, int size) {
  if (size > 0x2000) size = 0x2000;  // Max 8KB
  char* mem = cpu.get_mem();
  memcpy(&mem[CPM_LOAD_ADDR], data, size);

  // Fill BIOS area with RET instructions
  for (int i = 0; i < 0x33; i += 3) {
    mem[BIOS_BASE + i] = 0xC9;  // RET
  }

  js_status("System loaded");
  return 0;
}

// Exported: Load disk image
extern "C" EMSCRIPTEN_KEEPALIVE
int cpm_load_disk(const char* data, int size) {
  disk_a.assign(data, data + size);
  // Pad to full disk size if needed
  if (disk_a.size() < DISK_SIZE) {
    disk_a.resize(DISK_SIZE, 0xE5);
  }
  js_status("Disk loaded");
  return 0;
}

// Exported: Start emulation
extern "C" EMSCRIPTEN_KEEPALIVE
void cpm_start() {
  // Initialize CPU state
  cpu.set_cpu_mode(qkz80::MODE_8080);
  cpu.regs.AF.set_pair16(0);
  cpu.regs.BC.set_pair16(0);
  cpu.regs.DE.set_pair16(0);
  cpu.regs.HL.set_pair16(0);
  cpu.regs.PC.set_pair16(BIOS_BASE);
  cpu.regs.SP.set_pair16(CPM_LOAD_ADDR);
  running = true;
  waiting_for_input = false;
  js_status("Starting CP/M...");
}

// Exported: Stop emulation
extern "C" EMSCRIPTEN_KEEPALIVE
void cpm_stop() {
  running = false;
}

// Exported: Get disk data for download
extern "C" EMSCRIPTEN_KEEPALIVE
const uint8_t* cpm_get_disk_data() {
  return disk_a.data();
}

extern "C" EMSCRIPTEN_KEEPALIVE
int cpm_get_disk_size() {
  return disk_a.size();
}

// Exported: Auto-start with bundled files
extern "C" EMSCRIPTEN_KEEPALIVE
int cpm_autostart() {
  // Load bundled cpm22.sys
  FILE* f = fopen("/cpm22.sys", "rb");
  if (!f) {
    js_status("Error: cpm22.sys not found");
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* mem = cpu.get_mem();
  fread(&mem[CPM_LOAD_ADDR], 1, size, f);
  fclose(f);

  // Fill BIOS area with RET instructions
  for (int i = 0; i < 0x33; i += 3) {
    mem[BIOS_BASE + i] = 0xC9;
  }

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

  // Pad disk if needed
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
