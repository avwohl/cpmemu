/*
 * CP/M 2.2 Emulator - Command Line Version
 *
 * For testing and debugging. Same logic as web version.
 */

#include "../src/qkz80.h"
#include "../src/qkz80_mem.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <queue>
#include <vector>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

// CP/M constants - these match cpm22.asm built for 63K
constexpr uint16_t CPM_LOAD_ADDR = 0xE000;   // CCP+BDOS load address
constexpr uint16_t BIOS_BASE = 0xF600;       // BIOS entry points

// BIOS layout from bios.sym
constexpr uint16_t DPH0_ADDR = 0xF65C;
constexpr uint16_t DPH1_ADDR = 0xF66C;
constexpr uint16_t DPH2_ADDR = 0xF67C;
constexpr uint16_t DPH3_ADDR = 0xF68C;

// BIOS entry point offsets (relative to BIOS_BASE)
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

// Memory class with write protection
class cpm_mem : public qkz80_cpu_mem {
  uint16_t protect_start = 0;
  uint16_t protect_end = 0;
  bool protection_enabled = false;

public:
  void set_write_protection(uint16_t start, uint16_t end) {
    protect_start = start;
    protect_end = end;
    protection_enabled = true;
  }

  void store_mem(qkz80_uint16 addr, qkz80_uint8 abyte) override {
    if (protection_enabled && addr >= protect_start && addr < protect_end) {
      fprintf(stderr, "\n*** WRITE PROTECT: 0x%04X = 0x%02X ***\n", addr, abyte);
      return;
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
static int current_disk = 0;
static int current_track = 0;
static int current_sector = 1;
static uint16_t dma_addr = 0x0080;
static bool running = false;
static bool debug_disk = false;

// Terminal handling
static struct termios orig_termios;
static bool raw_mode = false;

void disable_raw_mode() {
  if (raw_mode) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode = false;
  }
}

void enable_raw_mode() {
  tcgetattr(STDIN_FILENO, &orig_termios);
  atexit(disable_raw_mode);

  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  raw_mode = true;
}

int kbhit() {
  struct timeval tv = {0, 0};
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

int getch() {
  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) == 1) return c;
  return -1;
}

// No sector skew - disk image is in logical order (sectors 0-25)
// BIOS receives sectors 1-26 after SECTRN adds 1, so we just subtract 1

// Disk read - sector is 1-based (SECTRN adds 1), disk image is 0-based sequential
static int disk_read(int track, int sector) {
  if (disk_a.empty()) return 1;
  // Convert 1-based sector (1-26) to 0-based (0-25) for disk image access
  int logical_sector = sector - 1;
  int offset = track * TRACK_SIZE + logical_sector * SECTOR_SIZE;
  if (offset < 0 || offset + SECTOR_SIZE > (int)disk_a.size()) return 1;
  char* mem = cpu.get_mem();
  memcpy(&mem[dma_addr], &disk_a[offset], SECTOR_SIZE);
  if (debug_disk) {
    fprintf(stderr, "[READ T:%d S:%d DMA:%04X]\n", track, sector, dma_addr);
  }
  return 0;
}

// Disk write - sector is 1-based (SECTRN adds 1), disk image is 0-based sequential
static int disk_write(int track, int sector) {
  if (disk_a.empty()) return 1;
  // Convert 1-based sector (1-26) to 0-based (0-25) for disk image access
  int logical_sector = sector - 1;
  int offset = track * TRACK_SIZE + logical_sector * SECTOR_SIZE;
  if (offset < 0 || offset + SECTOR_SIZE > (int)disk_a.size()) return 1;
  char* mem = cpu.get_mem();
  memcpy(&disk_a[offset], &mem[dma_addr], SECTOR_SIZE);
  if (debug_disk) {
    fprintf(stderr, "[WRITE T:%d S:%d DMA:%04X]\n", track, sector, dma_addr);
  }
  return 0;
}

// BIOS trap handler
static bool handle_bios(uint16_t pc) {
  int offset = pc - BIOS_BASE;
  char* mem = cpu.get_mem();

  auto do_ret = [&]() {
    uint16_t sp = cpu.regs.SP.get_pair16();
    uint16_t ret_addr = (uint8_t)mem[sp] | ((uint8_t)mem[sp+1] << 8);
    cpu.regs.SP.set_pair16(sp + 2);
    cpu.regs.PC.set_pair16(ret_addr);
  };

  switch (offset) {
    case BIOS_BOOT: {
      mem[0x0000] = 0xC3;
      mem[0x0001] = static_cast<char>((BIOS_BASE + BIOS_WBOOT) & 0xFF);
      mem[0x0002] = static_cast<char>((BIOS_BASE + BIOS_WBOOT) >> 8);
      mem[0x0003] = 0x00;
      mem[0x0004] = 0x00;
      mem[0x0005] = 0xC3;
      mem[0x0006] = 0x06;
      mem[0x0007] = 0xE8;

      current_disk = 0;
      current_track = 0;
      current_sector = 1;
      dma_addr = 0x0080;

      cpu.regs.BC.set_pair16(0x0000);
      cpu.regs.PC.set_pair16(CPM_LOAD_ADDR);
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
      cpu.regs.BC.set_pair16(static_cast<uint8_t>(mem[0x0004]) & 0x0F);
      cpu.regs.PC.set_pair16(CPM_LOAD_ADDR + 3);
      return true;
    }

    case BIOS_CONST: {
      // Check for keyboard input
      if (input_queue.empty() && kbhit()) {
        int ch = getch();
        if (ch >= 0) input_queue.push(ch);
      }
      cpu.set_reg8(input_queue.empty() ? 0x00 : 0xFF, qkz80::reg_A);
      do_ret();
      return true;
    }

    case BIOS_CONIN: {
      while (input_queue.empty()) {
        if (kbhit()) {
          int ch = getch();
          if (ch >= 0) {
            if (ch == '\n') ch = '\r';
            input_queue.push(ch);
          }
        }
        usleep(1000);
      }
      int ch = input_queue.front();
      input_queue.pop();
      cpu.set_reg8(ch & 0x7F, qkz80::reg_A);
      do_ret();
      return true;
    }

    case BIOS_CONOUT: {
      int ch = cpu.get_reg8(qkz80::reg_C) & 0x7F;
      if (ch == '\r') {
        putchar('\r');
        putchar('\n');
      } else {
        putchar(ch);
      }
      fflush(stdout);
      do_ret();
      return true;
    }

    case BIOS_LIST:
    case BIOS_PUNCH:
      do_ret();
      return true;

    case BIOS_READER:
      cpu.set_reg8(0x1A, qkz80::reg_A);
      do_ret();
      return true;

    case BIOS_HOME:
      current_track = 0;
      do_ret();
      return true;

    case BIOS_SELDSK: {
      int disk = cpu.get_reg8(qkz80::reg_C);
      int e_reg = cpu.get_reg8(qkz80::reg_E);
      uint16_t dph = 0;

      static const uint16_t dph_table[4] = { DPH0_ADDR, DPH1_ADDR, DPH2_ADDR, DPH3_ADDR };
      if (disk < 4) {
        dph = dph_table[disk];
        current_disk = disk;
      }

      if (debug_disk || disk >= 4) {
        fprintf(stderr, "[SELDSK %d (%c:) E=%d -> DPH=%04X]\n",
                disk, (disk < 26 ? 'A' + disk : '?'), e_reg, dph);
        // Dump FCB at 0x5C
        fprintf(stderr, "  FCB@5C: drive=%d name=", (uint8_t)mem[0x5C]);
        for (int i = 1; i <= 8; i++) fprintf(stderr, "%c", mem[0x5C+i] & 0x7F);
        fprintf(stderr, ".");
        for (int i = 9; i <= 11; i++) fprintf(stderr, "%c", mem[0x5C+i] & 0x7F);
        fprintf(stderr, "\n");
      }
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
      uint16_t logical = cpu.regs.BC.get_pair16();
      uint16_t xlt = cpu.regs.DE.get_pair16();
      uint16_t physical;

      if (xlt == 0) {
        physical = logical + 1;
      } else {
        physical = (uint8_t)mem[xlt + logical];
      }
      cpu.regs.HL.set_pair16(physical);
      do_ret();
      return true;
    }
  }
  return false;
}

int main(int argc, char* argv[]) {
  const char* bios_file = "bios.sys";
  const char* sys_file = "../cpm22.sys";
  const char* disk_file = "../drivea.img";

  // Parse args
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-d") == 0) {
      debug_disk = true;
    } else if (strcmp(argv[i], "-b") == 0 && i+1 < argc) {
      bios_file = argv[++i];
    } else if (strcmp(argv[i], "-s") == 0 && i+1 < argc) {
      sys_file = argv[++i];
    } else if (strcmp(argv[i], "-a") == 0 && i+1 < argc) {
      disk_file = argv[++i];
    } else if (strcmp(argv[i], "-h") == 0) {
      printf("Usage: %s [-d] [-b bios.sys] [-s cpm22.sys] [-a disk.img]\n", argv[0]);
      printf("  -d  Debug disk operations\n");
      return 0;
    }
  }

  char* mem = cpu.get_mem();

  // Load BIOS
  FILE* f = fopen(bios_file, "rb");
  if (!f) {
    fprintf(stderr, "Error: Cannot open %s\n", bios_file);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  fread(&mem[BIOS_BASE], 1, size, f);
  fclose(f);
  fprintf(stderr, "Loaded %s: %ld bytes at 0x%04X\n", bios_file, size, BIOS_BASE);

  // Load CP/M system
  f = fopen(sys_file, "rb");
  if (!f) {
    fprintf(stderr, "Error: Cannot open %s\n", sys_file);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  fread(&mem[CPM_LOAD_ADDR], 1, size, f);
  fclose(f);
  fprintf(stderr, "Loaded %s: %ld bytes at 0x%04X\n", sys_file, size, CPM_LOAD_ADDR);

  // Load disk
  f = fopen(disk_file, "rb");
  if (!f) {
    fprintf(stderr, "Error: Cannot open %s\n", disk_file);
    return 1;
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
  fprintf(stderr, "Loaded %s: %ld bytes\n", disk_file, size);

  // Initialize CPU
  cpu.set_cpu_mode(qkz80::MODE_8080);
  cpu.regs.AF.set_pair16(0);
  cpu.regs.BC.set_pair16(0);
  cpu.regs.DE.set_pair16(0);
  cpu.regs.HL.set_pair16(0);
  cpu.regs.PC.set_pair16(BIOS_BASE);
  cpu.regs.SP.set_pair16(CPM_LOAD_ADDR);

  // Enable write protection
  memory.set_write_protection(BIOS_BASE, DPH0_ADDR);

  enable_raw_mode();
  fprintf(stderr, "Starting CP/M...\n");

  running = true;
  while (running) {
    uint16_t pc = cpu.regs.PC.get_pair16();

    if (memory.is_bios_trap(pc)) {
      if (!handle_bios(pc)) {
        fprintf(stderr, "Unhandled BIOS call at 0x%04X\n", pc);
        break;
      }
      continue;
    }

    cpu.execute();
  }

  disable_raw_mode();
  return 0;
}
