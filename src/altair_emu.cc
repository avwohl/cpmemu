/*
 * Altair 8800 Hardware Emulator for 4K/8K BASIC
 *
 * This emulator operates at the hardware level, handling IN/OUT instructions
 * to emulate:
 * - 88-2SIO serial ports (ports 0x00-0x01 or 0x10-0x11)
 * - Sense switches (port 0xFF) for memory size configuration
 * - ROM protection for memory size detection
 *
 * Target: Run MITS 4K and 8K BASIC
 */

#include "qkz80.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <map>
#include <set>
#include <string>

// Memory subclass with ROM protection for Altair BASIC memory detection
class altair_mem : public qkz80_cpu_mem {
  qkz80_uint16 rom_start = 0;
public:
  void set_rom_start(qkz80_uint16 addr) { rom_start = addr; }
  qkz80_uint16 get_rom_start() const { return rom_start; }

  void store_mem(qkz80_uint16 addr, qkz80_uint8 abyte) override {
    if (rom_start != 0 && addr >= rom_start) {
      return;  // Silently ignore writes to ROM region
    }
    qkz80_cpu_mem::store_mem(addr, abyte);
  }
};

// Terminal state management
static struct termios original_termios;
static bool termios_saved = false;

static void disable_raw_mode() {
  if (termios_saved) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
    termios_saved = false;
  }
}

static void enable_raw_mode() {
  if (!isatty(STDIN_FILENO)) {
    return;
  }

  if (!termios_saved) {
    tcgetattr(STDIN_FILENO, &original_termios);
    termios_saved = true;
    atexit(disable_raw_mode);
  }

  struct termios raw = original_termios;
  raw.c_lflag &= ~(ICANON | ECHO | ISIG);
  raw.c_cc[VMIN] = 0;  // Non-blocking
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Check if input is available
static bool stdin_has_data() {
  fd_set readfds;
  struct timeval tv;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0;
}

// ^C exit handling
static int consecutive_ctrl_c = 0;
static const int CTRL_C_EXIT_COUNT = 5;

static bool check_ctrl_c_exit(int ch) {
  if (ch == 0x03) {
    consecutive_ctrl_c++;
    if (consecutive_ctrl_c >= CTRL_C_EXIT_COUNT) {
      fprintf(stderr, "\n[Exiting: %d consecutive ^C received]\n", CTRL_C_EXIT_COUNT);
      disable_raw_mode();
      exit(0);
    }
    return false;
  } else {
    consecutive_ctrl_c = 0;
    return false;
  }
}

class AltairEmulator {
private:
  qkz80* cpu;
  bool debug;

  // I/O port tracking
  std::map<uint8_t, int> port_in_counts;
  std::map<uint8_t, int> port_out_counts;
  std::set<uint8_t> unknown_ports;

  // Serial port state (88-2SIO)
  // Port A: status=0x10, data=0x11 (or 0x00/0x01 for some configs)
  // Port B: status=0x12, data=0x13
  uint8_t sio_status_a;
  uint8_t sio_status_b;
  int input_char;  // Buffered input character (-1 = none)

  // Sense switches (directly accessible from front panel, read via port 0xFF)
  uint8_t sense_switches;

  // Memory configuration
  uint16_t ram_top;        // Top of RAM (for ROM detection)
  bool protect_high_page;  // Protect writes to 0xFF00-0xFFFF

  // Cassette interface (88-ACR) - ports 0x06/0x07
  FILE* cassette_out;      // Output file for CSAVE
  FILE* cassette_in;       // Input file for CLOAD
  std::string cassette_filename;
  int cassette_leader_count;  // Simulate leader before data

public:
  AltairEmulator(qkz80* acpu, bool adebug = false)
    : cpu(acpu), debug(adebug),
      sio_status_a(0x02), sio_status_b(0x02),  // TX ready by default
      input_char(-1),
      sense_switches(0x00),  // Will be set based on memory size
      ram_top(0xFFFF),
      protect_high_page(true),
      cassette_out(nullptr),
      cassette_in(nullptr),
      cassette_leader_count(0) {
  }

  ~AltairEmulator() {
    if (cassette_out) {
      fclose(cassette_out);
      fprintf(stderr, "Cassette output saved to: %s\n", cassette_filename.c_str());
    }
    if (cassette_in) fclose(cassette_in);
  }

  void set_cassette_output(const std::string& filename) {
    cassette_filename = filename;
  }

  bool set_cassette_input(const std::string& filename) {
    cassette_in = fopen(filename.c_str(), "rb");
    if (!cassette_in) {
      fprintf(stderr, "Cannot open cassette input: %s\n", filename.c_str());
      return false;
    }
    fprintf(stderr, "Cassette input: %s\n", filename.c_str());
    return true;
  }

  void set_memory_size(int kb) {
    // Configure sense switches based on memory size
    // Altair BASIC uses sense switches to determine memory size
    // Common configurations:
    //   4K BASIC: needs at least 4K RAM
    //   8K BASIC: needs at least 8K RAM
    //
    // Sense switch bits (directly influence BASIC's behavior):
    //   Different versions interpret these differently
    //   For now, we'll discover what the binaries actually read

    ram_top = (kb * 1024) - 1;
    if (ram_top > 0xFEFF) ram_top = 0xFEFF;  // Reserve top page

    // Set sense switches - bits interpreted by BASIC
    // This may need adjustment based on what the binaries expect
    sense_switches = 0x00;

    fprintf(stderr, "Memory size: %dK, RAM top: 0x%04X\n", kb, ram_top);
  }

  void set_sense_switches(uint8_t val) {
    sense_switches = val;
    fprintf(stderr, "Sense switches set to: 0x%02X\n", sense_switches);
  }

  // Handle IN instruction - returns value read from port
  uint8_t handle_in(uint8_t port) {
    port_in_counts[port]++;

    uint8_t value = 0x00;

    switch (port) {
      // 88-2SIO Port A - commonly used configuration
      case 0x00:  // Status port (alternate address)
      case 0x10:  // Status port (standard address)
        // Bit 0: Input device ready (RDRF - Receive Data Register Full)
        // Bit 1: Transmit buffer empty (TDRE - Transmit Data Register Empty)
        // Bit 2-4: Error flags (always 0)
        // Bit 5: Data Carrier Detect (always 0)
        // Bit 7: IRQ flag (always 0)
        value = 0x02;  // TDRE always set (can always transmit)
        if (input_char >= 0 || stdin_has_data()) {
          value |= 0x01;  // RDRF set - data available
        }
        if (debug) {
          fprintf(stderr, "[IN port 0x%02X (SIO-A status) = 0x%02X]\n", port, value);
        }
        break;

      case 0x01:  // Data port (alternate address)
      case 0x11:  // Data port (standard address)
        // Read character from input
        if (input_char >= 0) {
          value = input_char & 0x7F;
          input_char = -1;
        } else if (stdin_has_data()) {
          int ch = getchar();
          if (ch == EOF) ch = 0;
          check_ctrl_c_exit(ch);
          if (ch == '\n') ch = '\r';
          value = ch & 0x7F;
        } else {
          value = 0x00;
        }
        if (debug) {
          fprintf(stderr, "[IN port 0x%02X (SIO-A data) = 0x%02X '%c']\n",
                  port, value, isprint(value) ? value : '.');
        }
        break;

      // 88-2SIO Port B
      case 0x12:  // Status port B
        value = 0x02;  // TDRE set
        break;

      case 0x13:  // Data port B
        value = 0x00;
        break;

      // Sense switches - directly readable from front panel
      case 0xFF:
        value = sense_switches;
        if (debug) {
          fprintf(stderr, "[IN port 0xFF (sense switches) = 0x%02X]\n", value);
        }
        break;

      // 88-ACR Cassette interface (88-SIO at ports 0x06/0x07)
      case 0x06:  // Cassette status register
        // 88-SIO status bits (different from MC6850!):
        // Bit 0: Input device ready (1 = CPU can write/transmit)
        // Bit 1: Transmitter buffer empty (1 = data received and ready to read)
        // Bit 2: Parity error
        // Bit 3: Framing error
        // Bit 4: Data overflow
        // Bit 5: Data available for writing (always 0)
        // Bit 6: Not used
        // Bit 7: Output device ready (always 0)
        value = 0x01;  // Bit 0 = 1: ready to transmit (for CSAVE)
        if (cassette_in) {
          // Check if data available from cassette input file
          int ch = fgetc(cassette_in);
          if (ch != EOF) {
            ungetc(ch, cassette_in);
            value = 0x02;  // ONLY Bit 1 = 1: data available to read
          } else {
            value = 0x00;  // No data, end of tape
          }
        }
        if (debug) {
          fprintf(stderr, "[IN port 0x06 (cassette status) = 0x%02X]\n", value);
        }
        break;

      case 0x07:  // Cassette data register (read)
        // Read byte from cassette input file
        if (cassette_in) {
          int ch = fgetc(cassette_in);
          if (ch != EOF) {
            value = ch & 0xFF;
          }
        }
        if (debug) {
          fprintf(stderr, "[IN port 0x07 (cassette data) = 0x%02X]\n", value);
        }
        break;

      default:
        // Unknown port - log it
        if (unknown_ports.find(port) == unknown_ports.end()) {
          fprintf(stderr, "[WARNING: Unknown IN port 0x%02X]\n", port);
          unknown_ports.insert(port);
        }
        value = 0x00;
        break;
    }

    return value;
  }

  // Handle OUT instruction
  void handle_out(uint8_t port, uint8_t value) {
    port_out_counts[port]++;

    switch (port) {
      // 88-2SIO Port A
      case 0x00:  // Control port (alternate address)
      case 0x10:  // Control port (standard address)
        // Control register write - configure SIO
        // Bit 0-1: Counter divide select
        // Bit 2-4: Word select
        // Bit 5-6: Transmit control
        // Bit 7: Receive interrupt enable
        if (debug) {
          fprintf(stderr, "[OUT port 0x%02X (SIO-A control) = 0x%02X]\n", port, value);
        }
        break;

      case 0x01:  // Data port (alternate address)
      case 0x11:  // Data port (standard address)
        // Output character - filter nulls (teletype timing padding)
        if ((value & 0x7F) != 0x00) {
          putchar(value & 0x7F);
          fflush(stdout);
        }
        if (debug) {
          fprintf(stderr, "[OUT port 0x%02X (SIO-A data) = 0x%02X '%c']\n",
                  port, value, isprint(value & 0x7F) ? (value & 0x7F) : '.');
        }
        break;

      // 88-2SIO Port B
      case 0x12:  // Control port B
        break;

      case 0x13:  // Data port B
        break;

      // Front panel switches/LEDs
      case 0xFF:
        if (debug) {
          fprintf(stderr, "[OUT port 0xFF = 0x%02X]\n", value);
        }
        break;

      // 88-ACR Cassette interface
      case 0x06:  // Cassette control
        if (debug) {
          fprintf(stderr, "[OUT port 0x06 (cassette control) = 0x%02X]\n", value);
        }
        break;

      case 0x07:  // Cassette data output
        // Open cassette file on first write if not already open
        if (!cassette_out) {
          std::string fname = cassette_filename.empty() ? "cassette.cas" : cassette_filename;
          cassette_out = fopen(fname.c_str(), "wb");
          if (cassette_out) {
            fprintf(stderr, "[Cassette recording to: %s]\n", fname.c_str());
          } else {
            fprintf(stderr, "[WARNING: Cannot open cassette file: %s]\n", fname.c_str());
          }
        }
        if (cassette_out) {
          fputc(value, cassette_out);
          fflush(cassette_out);
        }
        if (debug) {
          fprintf(stderr, "[OUT port 0x07 (cassette data) = 0x%02X]\n", value);
        }
        break;

      default:
        // Unknown port - log it
        if (unknown_ports.find(port) == unknown_ports.end()) {
          fprintf(stderr, "[WARNING: Unknown OUT port 0x%02X value 0x%02X]\n", port, value);
          unknown_ports.insert(port);
        }
        break;
    }
  }

  // Check if memory write should be allowed (for ROM detection)
  bool allow_memory_write(uint16_t addr) {
    if (protect_high_page && addr >= 0xFF00) {
      if (debug) {
        fprintf(stderr, "[BLOCKED write to protected address 0x%04X]\n", addr);
      }
      return false;
    }
    return true;
  }

  void print_port_stats() {
    fprintf(stderr, "\n=== I/O Port Statistics ===\n");
    fprintf(stderr, "IN ports accessed:\n");
    for (auto& p : port_in_counts) {
      fprintf(stderr, "  Port 0x%02X: %d times\n", p.first, p.second);
    }
    fprintf(stderr, "OUT ports accessed:\n");
    for (auto& p : port_out_counts) {
      fprintf(stderr, "  Port 0x%02X: %d times\n", p.first, p.second);
    }
  }
};

// Global emulator instance for callbacks
static AltairEmulator* g_emu = nullptr;

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s [options] <binary.bin> [load_address]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --debug         Enable debug output\n");
    fprintf(stderr, "  --mem=N         Set memory size in KB (default: 64)\n");
    fprintf(stderr, "  --sense=0xNN    Set sense switch value\n");
    fprintf(stderr, "  --load=0xNNNN   Load address (default: 0x0000)\n");
    fprintf(stderr, "  --start=0xNNNN  Start address (default: load address)\n");
    fprintf(stderr, "  --tape-in=FILE  Cassette input file (for CLOAD)\n");
    fprintf(stderr, "  --tape-out=FILE Cassette output file (for CSAVE, default: cassette.cas)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s 4kbas40.bin                  # Load at 0x0000, run\n", argv[0]);
    fprintf(stderr, "  %s --mem=16 --sense=0x00 8kbas.bin\n", argv[0]);
    fprintf(stderr, "  %s --tape-out=prog.cas 8kbas.bin  # Save to prog.cas with CSAVE\n", argv[0]);
    return 1;
  }

  // Parse arguments
  const char* binary = nullptr;
  uint16_t load_addr = 0x0000;
  uint16_t start_addr = 0x0000;
  bool start_addr_set = false;
  bool debug = false;
  int mem_kb = 64;
  int sense = -1;  // -1 = not set
  const char* tape_in = nullptr;
  const char* tape_out = nullptr;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--debug") == 0) {
      debug = true;
    } else if (strncmp(argv[i], "--mem=", 6) == 0) {
      mem_kb = atoi(argv[i] + 6);
    } else if (strncmp(argv[i], "--sense=", 8) == 0) {
      sense = strtol(argv[i] + 8, nullptr, 0);
    } else if (strncmp(argv[i], "--tape-in=", 10) == 0) {
      tape_in = argv[i] + 10;
    } else if (strncmp(argv[i], "--tape-out=", 11) == 0) {
      tape_out = argv[i] + 11;
    } else if (strncmp(argv[i], "--load=", 7) == 0) {
      load_addr = strtol(argv[i] + 7, nullptr, 0);
    } else if (strncmp(argv[i], "--start=", 8) == 0) {
      start_addr = strtol(argv[i] + 8, nullptr, 0);
      start_addr_set = true;
    } else if (argv[i][0] != '-') {
      binary = argv[i];
    }
  }

  if (!binary) {
    fprintf(stderr, "Error: No binary file specified\n");
    return 1;
  }

  if (!start_addr_set) {
    start_addr = load_addr;
  }

  // Create memory with ROM protection and CPU in 8080 mode
  altair_mem memory;
  qkz80 cpu(&memory);
  cpu.set_cpu_mode(qkz80::MODE_8080);
  fprintf(stderr, "CPU mode: 8080\n");

  // Create emulator
  AltairEmulator emu(&cpu, debug);
  g_emu = &emu;

  emu.set_memory_size(mem_kb);
  if (sense >= 0) {
    emu.set_sense_switches(sense & 0xFF);
  }

  // Set up cassette files
  if (tape_out) {
    emu.set_cassette_output(tape_out);
  }
  if (tape_in) {
    emu.set_cassette_input(tape_in);
  }

  // Set ROM protection to simulate limited RAM
  // BASIC probes memory by writing byte 0 of each page and reading back
  // For 64K, set ROM at 0xFF00 so BASIC finds ~64K of RAM
  uint16_t rom_addr = 0xFF00;  // Protect top page
  memory.set_rom_start(rom_addr);
  fprintf(stderr, "ROM starts at: 0x%04X (for memory detection)\n", rom_addr);

  // Enable raw terminal mode
  enable_raw_mode();

  // Load binary file
  FILE* fp = fopen(binary, "rb");
  if (!fp) {
    fprintf(stderr, "Cannot open %s: %s\n", binary, strerror(errno));
    return 1;
  }

  char* mem = cpu.get_mem();

  // Clear memory first
  memset(mem, 0, 65536);

  // Load the binary
  fseek(fp, 0, SEEK_END);
  size_t file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  size_t loaded = fread(&mem[load_addr], 1, file_size, fp);
  fclose(fp);

  fprintf(stderr, "Loaded %zu bytes from %s at 0x%04X\n", loaded, binary, load_addr);
  fprintf(stderr, "Starting execution at 0x%04X\n", start_addr);

  // Set PC to start address
  cpu.regs.PC.set_pair16(start_addr);

  // Set SP to top of RAM (at ROM boundary, grows down into RAM)
  cpu.regs.SP.set_pair16(0xFF00);

  // Main execution loop
  long long instruction_count = 0;
  long long max_instructions = 1000000000LL;  // 1 billion max

  while (true) {
    uint16_t pc = cpu.regs.PC.get_pair16();
    uint8_t opcode = mem[pc] & 0xFF;

    // Check for HLT instruction
    if (opcode == 0x76) {
      fprintf(stderr, "\nHLT instruction at 0x%04X after %lld instructions\n", pc, instruction_count);
      emu.print_port_stats();
      break;
    }

    // Handle IN instruction (0xDB)
    if (opcode == 0xDB) {
      uint8_t port = mem[pc + 1] & 0xFF;
      uint8_t value = emu.handle_in(port);
      cpu.set_reg8(value, qkz80::reg_A);
      cpu.regs.PC.set_pair16(pc + 2);
      instruction_count++;
      continue;
    }

    // Handle OUT instruction (0xD3)
    if (opcode == 0xD3) {
      uint8_t port = mem[pc + 1] & 0xFF;
      uint8_t value = cpu.get_reg8(qkz80::reg_A);
      emu.handle_out(port, value);
      cpu.regs.PC.set_pair16(pc + 2);
      instruction_count++;
      continue;
    }

    // Check for writes to protected memory (for ROM detection)
    // This is a simplified check - a full implementation would intercept
    // all memory write instructions (STA, STAX, MOV M, SHLD, etc.)
    // For now, we'll mark the top page as ROM-like

    // Execute one instruction
    cpu.execute();
    instruction_count++;

    // Progress report every 100M instructions
    if (instruction_count % 100000000 == 0) {
      fprintf(stderr, "Progress: %lldM instructions, PC=0x%04X\n",
              instruction_count / 1000000, cpu.regs.PC.get_pair16());
    }

    if (instruction_count >= max_instructions) {
      fprintf(stderr, "\nReached instruction limit at PC=0x%04X\n",
              cpu.regs.PC.get_pair16());
      emu.print_port_stats();
      break;
    }
  }

  fprintf(stderr, "\nTotal instructions executed: %lld\n", instruction_count);

  return 0;
}
