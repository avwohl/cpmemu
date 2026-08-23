/*
 * CP/M 2.2 Emulator for qkz80
 *
 * This emulator provides a complete CP/M 2.2 environment including:
 * - Proper memory layout with BDOS and BIOS emulation
 * - File I/O translation to Unix filesystem
 * - Support for command-line arguments
 * - File mapping from CP/M 8.3 format to Unix long paths
 * - BIOS vector table for programs like MBASIC that call BIOS directly
 */

#include "qkz80.h"
#include "os/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>

// Helper function to expand environment variables in strings
// Supports both $VAR and ${VAR} syntax
static std::string expand_env_vars(const std::string& str) {
  std::string result;
  size_t i = 0;

  while (i < str.length()) {
    if (str[i] == '$') {
      // Found a variable reference
      i++;  // Skip the $

      std::string var_name;

      // Check for ${VAR} syntax
      if (i < str.length() && str[i] == '{') {
        i++;  // Skip the {

        // Read until }
        while (i < str.length() && str[i] != '}') {
          var_name += str[i++];
        }
        if (i < str.length() && str[i] == '}') {
          i++;  // Skip the }
        }
      } else {
        // $VAR syntax - read alphanumeric and underscore
        while (i < str.length() && (isalnum(str[i]) || str[i] == '_')) {
          var_name += str[i++];
        }
      }

      // Get environment variable value
      const char* env_value = getenv(var_name.c_str());
      if (env_value) {
        result += env_value;
      }
      // If variable not found, leave it empty (or could keep original)
    } else {
      result += str[i++];
    }
  }

  return result;
}

// ^C exit handling - 5 consecutive ^C characters exit the emulator
static int consecutive_ctrl_c = 0;
static const int CTRL_C_EXIT_COUNT = 5;
static bool ctrl_c_exit_enabled = true;   // default on: a raw-mode CLI
                                          // emulator needs an escape hatch
static bool ctrl_c_exit_from_cli = false; // a CLI flag outranks the config file
static const long long CTRL_C_EXIT_WINDOW_MS = 2000;
static long long first_ctrl_c_ms = 0;

// Milliseconds from a monotonic clock.  steady_clock never runs backwards,
// so an NTP step or a daylight-saving change cannot widen the ^C window.
static long long steady_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Memory save support for MOVCPM/SYSGEN
static const char* save_memory_file = nullptr;
static uint16_t save_memory_start = 0x0000;
static uint16_t save_memory_end = 0x0000;  // 0 = full 64K
static qkz80* save_memory_cpu = nullptr;

static void do_save_memory() {
  if (!save_memory_file || !save_memory_cpu) return;

  qkz80_uint8* mem = save_memory_cpu->get_mem();
  uint16_t start = save_memory_start;
  uint16_t end = save_memory_end ? save_memory_end : 0xFFFF;
  size_t size = (end >= start) ? (end - start + 1) : (0x10000 - start);

  FILE* fp = fopen(save_memory_file, "wb");
  if (!fp) {
    fprintf(stderr, "Failed to save memory to %s: %s\n", save_memory_file, strerror(errno));
    return;
  }

  size_t written = fwrite(&mem[start], 1, size, fp);
  fclose(fp);

  fprintf(stderr, "Saved %zu bytes (0x%04X-0x%04X) to %s\n",
          written, start, (uint16_t)(start + size - 1), save_memory_file);
}

// Check for ^C and handle exit logic
// Always returns false - ^C reaches the CP/M program either way.
// The exit is the escape hatch a raw-mode emulator needs, but WordStar binds
// ^C to page-down, so it is switchable: the 'ctrl_c_exit' config directive
// and the --ctrl-c-exit / --no-ctrl-c-exit flags.  All five must land inside
// CTRL_C_EXIT_WINDOW_MS of the first, measured across the whole run and not
// keystroke to keystroke, so that page-downs arriving at a reading pace never
// accumulate into an exit.  It is still only a second line of defence: a fast
// enough reader can page five times in two seconds.  The switch is the fix.
static bool check_ctrl_c_exit(int ch) {
  // A ^C synthesized from a special key (a Windows PgDn translated to the
  // WordStar page-down code) is not a user asking to leave, so it must never
  // count toward the exit.
  if (ch != 0x03 || platform::console_last_char_synthesized()) {
    consecutive_ctrl_c = 0;  // Reset counter on any other input
    return false;
  }

  // A run that has gone stale restarts from this keystroke.  Expiring it here
  // rather than at the fifth ^C matters: leftovers from an old run would
  // otherwise stay armed and eat into a genuine burst, so someone who pressed
  // ^C once minutes ago would need more than five to get out.
  long long now = steady_ms();
  if (consecutive_ctrl_c == 0 || (now - first_ctrl_c_ms) > CTRL_C_EXIT_WINDOW_MS) {
    consecutive_ctrl_c = 0;
    first_ctrl_c_ms = now;
  }
  consecutive_ctrl_c++;

  // Reaching the count here means all of them landed inside the window, since
  // every one of them was measured against first_ctrl_c_ms on the way in
  if (consecutive_ctrl_c >= CTRL_C_EXIT_COUNT) {
    if (ctrl_c_exit_enabled) {
      fprintf(stderr, "\n[Exiting: %d consecutive ^C received]\n", CTRL_C_EXIT_COUNT);
      do_save_memory();
      platform::disable_raw_mode();
      exit(0);
    }
    consecutive_ctrl_c = 0;  // Switched off - keep the counter bounded
  }
  return false;  // Pass ^C through to CP/M program
}

// Read a console character for a blocking read site.
// The platform layer reports a special key it has no translation for as a
// synthesized 0.  That is exactly right for the polled BDOS 6 path, where 0
// means "no character", but BDOS 1, BDOS 10 and BIOS CONIN are waiting for a
// keystroke and must not be handed a NUL the user never typed - so skip it and
// wait for the next key.  On POSIX nothing is ever synthesized and this is a
// straight pass-through.
static int console_getchar_blocking() {
  int ch = platform::console_getchar();
  while (ch == 0 && platform::console_last_char_synthesized()) {
    ch = platform::console_getchar();
  }
  return ch;
}

// CP/M Memory Layout Constants
#define TPA_START      0x0100
#define BOOT_ADDR      0x0000
#define IOBYTE_ADDR    0x0003
#define DRVUSER_ADDR   0x0004
#define BDOS_ENTRY     0x0005
#define DEFAULT_FCB    0x005C
#define DEFAULT_FCB2   0x006C
#define DEFAULT_DMA    0x0080
#define DMA_SIZE       128
#define CPM_EOF        0x1A  // ^Z

// BIOS/BDOS placement (for 64K system)
// No real BDOS/BIOS code — just trap addresses intercepted by handle_pc()
#define BIOS_BASE      0xFE00  // BIOS jump table (17 entries * 3 = 51 bytes)
#define BDOS_BASE      0xFD00  // BDOS entry (trapped, no code in memory)

// BIOS function offsets from BIOS_BASE
#define BIOS_BOOT      0
#define BIOS_WBOOT     3
#define BIOS_CONST     6   // Console status
#define BIOS_CONIN     9   // Console input
#define BIOS_CONOUT    12  // Console output
#define BIOS_LIST      15  // List output
#define BIOS_PUNCH     18  // Punch output
#define BIOS_READER    21  // Reader input
#define BIOS_HOME      24  // Home disk
#define BIOS_SELDSK    27  // Select disk
#define BIOS_SETTRK    30  // Set track
#define BIOS_SETSEC    33  // Set sector
#define BIOS_SETDMA    36  // Set DMA
#define BIOS_READ      39  // Read sector
#define BIOS_WRITE     42  // Write sector

// Disk tables packed above BIOS jump table (0xFE00 + 51 = 0xFE33)
// This keeps them out of TPA so large programs can't overwrite them
#define DPH_ADDR       0xFE33  // Disk Parameter Header (16 bytes)
#define DPB_ADDR       0xFE43  // Disk Parameter Block (15 bytes)
#define DIRBUF_ADDR    0xFE52  // Directory buffer (128 bytes)
#define ALV_ADDR       0xFED2  // Allocation Vector (64 bytes)
#define CSV_ADDR       0xFF12  // Check Vector (64 bytes, ends at 0xFF51)
#define BIOS_LISTST    45  // List status
#define BIOS_SECTRAN   48  // Sector translate

// File modes
enum FileMode {
  MODE_BINARY,
  MODE_TEXT,
  MODE_AUTO
};

// File mapping entry
struct FileMapping {
  std::string cpm_pattern;
  std::string unix_pattern;
  FileMode mode;
  bool eol_convert;

  FileMapping() : mode(MODE_AUTO), eol_convert(true) {}
};

// FCB structure
struct FCB {
  qkz80_uint8 drive;        // 0 = default, 1 = A:, 2 = B:, etc.
  char name[8];             // Filename, space-padded
  char ext[3];              // Extension, space-padded
  qkz80_uint8 ex;           // Extent number
  qkz80_uint8 s1;           // Reserved
  qkz80_uint8 s2;           // Reserved
  qkz80_uint8 rc;           // Record count
  qkz80_uint8 al[16];       // Allocation map
  qkz80_uint8 cr;           // Current record
  qkz80_uint8 r0, r1, r2;   // Random record number
};

// Open file tracking
struct OpenFile {
  FILE* fp;
  std::string unix_path;
  std::string cpm_name;
  FileMode mode;
  bool eol_convert;
  int position;  // Current record position
  bool eof_seen;
  bool write_mode;
  std::vector<uint8_t> write_buffer;  // Buffer for EOL conversion on write

  OpenFile() : fp(nullptr), mode(MODE_BINARY), eol_convert(false),
    position(0), eof_seen(false), write_mode(false) {}
};

class CPMEmulator {
private:
  qkz80* cpu;
  qkz80_uint8 current_drive;
  qkz80_uint8 current_user;
  // Host directory backing each CP/M drive, index 0 = A: through 15 = P:.
  // An empty string means the drive is not configured, which is the default
  // for all sixteen and makes every lookup behave exactly as it did before
  // drives existed: relative to the process working directory.
  std::string drive_dirs[16];
  // Bit 0 = A: .. bit 15 = P:.  A drive is logged in once it is configured
  // or selected.  A: is always logged in, as on a real machine that booted.
  qkz80_uint16 login_vector;
  qkz80_uint16 current_dma;
  bool debug;
  FileMode default_mode;
  bool default_eol_convert;

  // File mapping with patterns and modes
  std::vector<FileMapping> file_mappings;

  // Legacy simple file mapping for backward compatibility
  std::map<std::string, std::string> file_map;

  // Open files indexed by FCB address
  std::map<qkz80_uint16, OpenFile> open_files;

  // Command line arguments
  std::vector<std::string> args;

  // Device redirection files
  FILE* printer_file;      // LST: device (LPRINT)
  FILE* aux_in_file;       // RDR: device (Auxiliary input)
  FILE* aux_out_file;      // PUN: device (Auxiliary output)
  qkz80_uint8 iobyte;      // IOBYTE for device mapping
  bool printer_echo;       // ^P: mirror console output to printer_file

  // Directory search state for BDOS 17/18
  // One directory hit.  The host path and the CP/M name are kept separately
  // on purpose: the path is what gets stat'ed and opened, the name is what
  // goes into the directory entry.  Deriving the name from the path - which
  // is what this used to do - loses it whenever the two differ, and they
  // always differ for a mapping or a drive directory.
  struct SearchResult {
    std::string path;
    char name[8];
    char ext[3];
  };
  std::vector<SearchResult> search_results;  // List of matching files
  static SearchResult make_search_result(const std::string& path,
                                         const char name[8], const char ext[3]) {
    SearchResult r;
    r.path = path;
    memcpy(r.name, name, 8);
    memcpy(r.ext, ext, 3);
    return r;
  }
  // Write one 32-byte CP/M directory entry at the current DMA address.
  void write_dir_entry(const SearchResult& r);
  size_t search_index;                       // Current position in search
  std::string search_pattern;                // FCB pattern for search
  qkz80_uint8 search_user;                   // User number for search

public:
  // Program name from config file
  std::string config_program;

  // Public debug settings for selective debugging
  std::set<int> debug_bdos_funcs;  // Which BDOS functions to debug
  std::set<int> debug_bios_offsets; // Which BIOS offsets to debug

  // Disk BIOS behavior: 0=ok, 1=fail, 2=error
  int bios_disk_mode;

  CPMEmulator(qkz80* acpu, bool adebug = false)
    : cpu(acpu), current_drive(0), current_user(0), login_vector(0x0001),
      current_dma(DEFAULT_DMA), debug(adebug),
      default_mode(MODE_AUTO), default_eol_convert(true),
      printer_file(nullptr), aux_in_file(nullptr),
      aux_out_file(nullptr), iobyte(0), printer_echo(false),
      search_index(0), search_user(0), bios_disk_mode(0) {
  }

  ~CPMEmulator() {
    // Close device files
    if (printer_file) fclose(printer_file);
    if (aux_in_file) fclose(aux_in_file);
    if (aux_out_file) fclose(aux_out_file);
  }

  void setup_memory();
  void setup_command_line(int argc, char** argv, int program_arg_index = 1);
  void add_file_mapping(const std::string& cpm_name, const std::string& unix_path);
  void add_file_mapping_ex(const std::string& cpm_pattern, const std::string& unix_pattern,
                           FileMode mode = MODE_AUTO, bool eol_convert = true);
  bool load_config_file(const std::string& cfg_path);
  bool handle_pc(qkz80_uint16 pc);

  // Device redirection
  void set_printer_file(const std::string& path);
  void set_aux_input_file(const std::string& path);
  void set_aux_output_file(const std::string& path);

  // Debug mode
  void set_debug(bool d) { debug = d; }

private:
  // File I/O helpers
  FileMode detect_file_mode(const std::string& filename, const std::string& unix_path);
  std::string find_unix_file_ex(const std::string& cpm_name, FileMode* mode_out, bool* eol_out,
                                qkz80_uint8 fcb_drive);
  bool match_pattern(const std::string& pattern, const std::string& text);

  // Decode an FCB drive byte to a 0-based drive index.
  // The two CP/M drive encodings are different and must not be conflated:
  // FCB byte 0 is 1-based with 0 meaning "whatever drive is selected"
  // (0 = default, 1 = A:, ... 16 = P:), while BDOS 14 and BDOS 25 are
  // 0-based (0 = A:).  Anything out of range - including the 0x3F '?' that
  // a search FCB may carry to mean "match every entry" - falls back to the
  // current drive rather than indexing off the end of the table.
  int fcb_drive_index(qkz80_uint8 dr) const {
    if (dr == 0 || dr > 16) return current_drive;
    return dr - 1;
  }
  // Configured host directory for a decoded index, or "" when unconfigured.
  const std::string& drive_dir(int idx) const { return drive_dirs[idx & 15]; }
  // Join a drive directory to a leaf name.  Forward slash on purpose: every
  // path in the configs is written that way and Windows accepts it.
  static std::string join_path(const std::string& dir, const std::string& leaf) {
    if (dir.empty()) return leaf;
    std::string d = dir;
    while (d.size() > 1 && (d[d.size() - 1] == '/' || d[d.size() - 1] == '\\')) {
      d.erase(d.size() - 1);
    }
    return d + "/" + leaf;
  }

  // EOL and EOF handling
  size_t read_with_conversion(OpenFile& of, uint8_t* buffer, size_t size);
  size_t write_with_conversion(OpenFile& of, const uint8_t* buffer, size_t size);
  void pad_to_128(uint8_t* buffer, size_t actual_size);

private:
  // BDOS functions
  void bdos_call(qkz80_uint8 func);
  void bdos_write_console(qkz80_uint8 ch);
  void bdos_write_string();
  void bdos_read_console();
  void bdos_read_console_buffer();
  void bdos_aux_input();
  void bdos_aux_output();
  void bdos_list_output();
  void bdos_get_iobyte();
  void bdos_set_iobyte();
  void bdos_console_status();
  void bdos_get_version();
  void bdos_direct_console_io();
  void bdos_reset_disk();
  void bdos_get_set_dma();
  void bdos_open_file();
  void bdos_close_file();
  void bdos_read_sequential();
  void bdos_write_sequential();
  void bdos_make_file();
  void bdos_rename_file();
  void bdos_delete_file();
  void bdos_read_random();
  void bdos_write_random();
  void bdos_file_size();
  void bdos_set_random_record();
  void bdos_search_first();
  void bdos_search_next();
  void bdos_get_current_drive();
  void bdos_set_drive();
  void bdos_get_set_user();
  void bdos_get_login_vector();
  void bdos_get_allocation_vector();
  void bdos_write_protect_disk();
  void bdos_get_readonly_vector();
  void bdos_set_file_attributes();
  void bdos_get_dpb();
  void bdos_reset_drive();
  void bdos_write_random_zero_fill();

  // BIOS functions
  void bios_call(int offset);
  void bios_const();   // Console status
  void bios_conin();   // Console input
  void bios_conout();  // Console output
  void bios_list();    // List (printer) output
  void bios_punch();   // Punch (aux output)
  void bios_reader();  // Reader (aux input)
  void bios_listst();  // List status

  // ADM-3A to ANSI terminal translator
  enum TermState { TERM_NORMAL, TERM_ESC, TERM_ESC_EQ, TERM_ESC_EQ_ROW, TERM_ESC_G };
  TermState term_state = TERM_NORMAL;
  int term_saved_row = 0;

  void console_output(qkz80_uint8 ch);

  // BDOS function 10 line-editor echo - the single place ^P hooks into
  void rdbuf_echo(int ch);
  void rdbuf_echo_stored(qkz80_uint8 ch);

  // Helper functions
  std::string fcb_to_filename(qkz80_uint16 fcb_addr);
  void filename_to_fcb(const std::string& filename, qkz80_uint16 fcb_addr);
  void read_fcb(qkz80_uint16 addr, FCB* fcb);
  void write_fcb(qkz80_uint16 addr, const FCB* fcb);
  std::string normalize_cpm_filename(const std::string& name);
  bool match_wildcard(const std::string& pattern, const std::string& text);
};

void CPMEmulator::setup_memory() {
  qkz80_uint8* mem = cpu->get_mem();

  // Setup jump at 0x0000 to WBOOT (warm boot)
  mem[0x0000] = 0xC3;  // JMP opcode
  mem[0x0001] = (BIOS_BASE + BIOS_WBOOT) & 0xFF;
  mem[0x0002] = ((BIOS_BASE + BIOS_WBOOT) >> 8) & 0xFF;

  // IOBYTE
  mem[IOBYTE_ADDR] = 0x00;

  // Current drive and user (drive 0 = A:, user 0)
  mem[DRVUSER_ADDR] = 0x00;

  // Setup jump at 0x0005 to BDOS
  mem[BDOS_ENTRY] = 0xC3;  // JMP opcode
  mem[BDOS_ENTRY + 1] = BDOS_BASE & 0xFF;
  mem[BDOS_ENTRY + 2] = (BDOS_BASE >> 8) & 0xFF;

  // Setup BIOS jump table at BIOS_BASE
  // Each BIOS function is a 3-byte JMP to a magic address
  // We'll use addresses starting at 0xFF00 for BIOS traps
  qkz80_uint16 bios_magic = 0xFF00;
  for (int i = 0; i < 17; i++) {
    qkz80_uint16 addr = BIOS_BASE + (i * 3);
    mem[addr] = 0xC3;  // JMP opcode
    mem[addr + 1] = (bios_magic + i) & 0xFF;
    mem[addr + 2] = ((bios_magic + i) >> 8) & 0xFF;
  }

  // Initialize DMA to default
  current_dma = DEFAULT_DMA;

  // Clear default FCBs
  memset(&mem[DEFAULT_FCB], 0, 36);
  memset(&mem[DEFAULT_FCB2], 0, 20);

  // Initialize Disk Parameter Header (DPH) - 16 bytes
  // This is what BIOS SELDSK returns a pointer to
  uint8_t* dph = (uint8_t*)&mem[DPH_ADDR];
  dph[0] = 0x00; dph[1] = 0x00;  // XLT - no sector translation
  dph[2] = 0x00; dph[3] = 0x00;  // Scratch area (BDOS workspace)
  dph[4] = 0x00; dph[5] = 0x00;
  dph[6] = 0x00; dph[7] = 0x00;
  dph[8] = DIRBUF_ADDR & 0xFF;          // DIRBUF low
  dph[9] = (DIRBUF_ADDR >> 8) & 0xFF;   // DIRBUF high
  dph[10] = DPB_ADDR & 0xFF;            // DPB low
  dph[11] = (DPB_ADDR >> 8) & 0xFF;     // DPB high
  dph[12] = CSV_ADDR & 0xFF;            // CSV low
  dph[13] = (CSV_ADDR >> 8) & 0xFF;     // CSV high
  dph[14] = ALV_ADDR & 0xFF;            // ALV low
  dph[15] = (ALV_ADDR >> 8) & 0xFF;     // ALV high

  // Initialize Disk Parameter Block (DPB) for a simulated 8MB drive
  // This is a standard CP/M 2.2 DPB structure
  // Format: SPT, BSH, BLM, EXM, DSM, DRM, AL0, AL1, CKS, OFF
  uint8_t* dpb = (uint8_t*)&mem[DPB_ADDR];
  dpb[0] = 128;  // SPT - sectors per track (low byte)
  dpb[1] = 0;    // SPT high byte
  dpb[2] = 4;    // BSH - block shift factor (2KB blocks = 2^(7+4) = 2048)
  dpb[3] = 15;   // BLM - block mask (2^BSH - 1 = 15)
  dpb[4] = 0;    // EXM - extent mask
  dpb[5] = 0xFF; // DSM - max block number (low) - 4095 blocks = ~8MB
  dpb[6] = 0x0F; // DSM high byte
  dpb[7] = 0xFF; // DRM - max directory entry (low) - 1024 entries
  dpb[8] = 0x03; // DRM high byte
  dpb[9] = 0xFF; // AL0 - allocation bitmap for directory
  dpb[10] = 0x00; // AL1
  dpb[11] = 0x00; // CKS - check vector size (low) - no removable media
  dpb[12] = 0x00; // CKS high byte
  dpb[13] = 0x00; // OFF - track offset (low)
  dpb[14] = 0x00; // OFF high byte

  // Initialize directory buffer
  memset(&mem[DIRBUF_ADDR], 0xE5, 128);  // Empty directory entries

  // Initialize allocation vector - mark everything as free
  // Each bit represents one block, 0=free, 1=allocated
  // For 4096 blocks we need 512 bytes, but we'll just init first 64
  memset(&mem[ALV_ADDR], 0x00, 64);  // All blocks free

  // Set stack pointer
  cpu->regs.SP.set_pair16(0xFFF0);
}

void CPMEmulator::setup_command_line(int argc, char** argv, int program_arg_index) {
  qkz80_uint8* mem = cpu->get_mem();

  if (argc < program_arg_index + 1) {
    mem[DEFAULT_DMA] = 0;  // No command line
    return;
  }

  // Parse filenames into default FCBs first (before writing command tail,
  // since FCB2 at 0x6C overlaps with the DMA buffer region)
  if (argc >= program_arg_index + 2) {
    filename_to_fcb(argv[program_arg_index + 1], DEFAULT_FCB);
  }
  if (argc >= program_arg_index + 3) {
    filename_to_fcb(argv[program_arg_index + 2], DEFAULT_FCB2);
  }

  // Build command line from arguments
  // CP/M requires a leading space before the first argument
  std::string cmdline;
  for (int i = program_arg_index + 1; i < argc; i++) {  // Skip program name and any switches
    cmdline += " ";  // Space before each argument (CP/M convention)

    // Check if argument looks like a Unix path (starts with / or ./)
    // vs a CP/M filename with options (like "TEST,TEST.COM/N/E")
    const char* arg_base = argv[i];
    bool looks_like_path = (argv[i][0] == '/') ||
                           (argv[i][0] == '.' && argv[i][1] == '/');
    if (looks_like_path) {
      // Extract basename for Unix paths
      const char* slash = strrchr(argv[i], '/');
      arg_base = slash ? slash + 1 : argv[i];
    }

    // Convert to uppercase
    std::string arg_upper;
    for (const char* p = arg_base; *p; p++) {
      arg_upper += toupper(*p);
    }

    // For CP/M arguments, don't truncate - pass as-is
    // CP/M programs expect the full command line string
    cmdline += arg_upper;

    args.push_back(argv[i]);
  }

  // Store command tail at DEFAULT_DMA (0x80)
  // Written after FCBs to ensure it isn't corrupted
  mem[DEFAULT_DMA] = std::min((int)cmdline.length(), 127);
  for (size_t i = 0; i < cmdline.length() && i < 127; i++) {
    mem[DEFAULT_DMA + 1 + i] = toupper(cmdline[i]);
  }

  if (debug) {
    fprintf(stderr, "Command line (%d bytes): '%s'\n", (int)cmdline.length(), cmdline.c_str());
  }
}

void CPMEmulator::add_file_mapping(const std::string& cpm_name, const std::string& unix_path) {
  std::string normalized = normalize_cpm_filename(cpm_name);
  file_map[normalized] = unix_path;

  if (debug) {
    fprintf(stderr, "File mapping: '%s' -> '%s'\n", normalized.c_str(), unix_path.c_str());
  }
}

void CPMEmulator::add_file_mapping_ex(const std::string& cpm_pattern, const std::string& unix_pattern,
                                      FileMode mode, bool eol_convert) {
  FileMapping mapping;
  mapping.cpm_pattern = normalize_cpm_filename(cpm_pattern);
  mapping.unix_pattern = unix_pattern;
  mapping.mode = mode;
  mapping.eol_convert = eol_convert;
  file_mappings.push_back(mapping);

  if (debug) {
    fprintf(stderr, "File mapping: '%s' -> '%s' (mode: %s, eol: %s)\n",
            mapping.cpm_pattern.c_str(), unix_pattern.c_str(),
            mode == MODE_TEXT ? "text" : mode == MODE_BINARY ? "binary" : "auto",
            eol_convert ? "yes" : "no");
  }
}

FileMode CPMEmulator::detect_file_mode(const std::string& filename, const std::string& unix_path) {
  // Check extension
  std::string upper = filename;
  for (char& c : upper) c = toupper(c);

  // Known text extensions
  const char* text_exts[] = {".BAS", ".MAC", ".ASM", ".TXT", ".DOC", ".LST", ".PRN", ".Z80", ".LIB", nullptr};
  for (int i = 0; text_exts[i]; i++) {
    if (upper.find(text_exts[i]) != std::string::npos) {
      return MODE_TEXT;
    }
  }

  // Known binary extensions
  const char* binary_exts[] = {".COM", ".EXE", ".OVL", ".OVR", ".SYS", ".BIN", ".DAT",
                               ".SPR", ".REL", ".PRL", ".RSP", nullptr};
  for (int i = 0; binary_exts[i]; i++) {
    if (upper.find(binary_exts[i]) != std::string::npos) {
      return MODE_BINARY;
    }
  }

  // Default to binary for unknown extensions - safer than heuristic detection
  // which can misidentify binary files with low control char counts
  return MODE_BINARY;
}

bool CPMEmulator::match_pattern(const std::string& pattern, const std::string& text) {
  // Simple wildcard matching (case-insensitive)
  std::string pat_upper = pattern;
  std::string text_upper = text;
  for (char& c : pat_upper) c = toupper(c);
  for (char& c : text_upper) c = toupper(c);

  // Simple implementation - just check for exact match or * wildcard
  if (pat_upper == text_upper) return true;
  if (pat_upper == "*" || pat_upper == "*.*") return true;

  // Check for *.EXT pattern
  if (pat_upper[0] == '*' && pat_upper.find('.') != std::string::npos) {
    size_t dot = text_upper.find('.');
    if (dot != std::string::npos) {
      std::string text_ext = text_upper.substr(dot);
      std::string pat_ext = pat_upper.substr(pat_upper.find('.'));
      return text_ext == pat_ext;
    }
  }

  return false;
}

std::string CPMEmulator::find_unix_file_ex(const std::string& cpm_name, FileMode* mode_out,
                                           bool* eol_out, qkz80_uint8 fcb_drive) {
  std::string normalized = normalize_cpm_filename(cpm_name);

  // Check new file mappings with patterns
  for (const auto& mapping : file_mappings) {
    if (match_pattern(mapping.cpm_pattern, normalized)) {
      if (platform::get_file_type(mapping.unix_pattern.c_str()) != platform::FileType::NotFound) {
        *mode_out = mapping.mode;
        *eol_out = mapping.eol_convert;

        // Auto-detect if needed
        if (*mode_out == MODE_AUTO) {
          *mode_out = detect_file_mode(normalized, mapping.unix_pattern);
        }

        return mapping.unix_pattern;
      }
    }
  }

  // Check legacy file map
  auto it = file_map.find(normalized);
  if (it != file_map.end()) {
    *mode_out = detect_file_mode(normalized, it->second);
    *eol_out = default_eol_convert;
    return it->second;
  }

  // A configured drive is a directory, and the search is confined to it.
  // The confinement is the point: without the early return below, opening
  // B:MISSING.TXT would fall through to the working directory and quietly
  // succeed on an unrelated file, which reads to the guest as success.
  // An unconfigured drive skips this block entirely, so with no drive_X in
  // the config every lookup resolves exactly as it did before drives.
  const std::string& ddir = drive_dir(fcb_drive_index(fcb_drive));
  if (!ddir.empty()) {
    std::string lower_leaf;
    for (char c : normalized) lower_leaf += tolower(c);
    const std::string candidates[2] = { join_path(ddir, lower_leaf),
                                        join_path(ddir, normalized) };
    for (int i = 0; i < 2; i++) {
      if (platform::get_file_type(candidates[i].c_str()) != platform::FileType::NotFound) {
        *mode_out = detect_file_mode(normalized, candidates[i]);
        *eol_out = default_eol_convert;
        return candidates[i];
      }
    }
    return "";  // Confined: never fall back to the working directory
  }

  // Try lowercase version in current directory
  std::string lowercase;
  for (char c : normalized) {
    lowercase += tolower(c);
  }

  if (platform::get_file_type(lowercase.c_str()) != platform::FileType::NotFound) {
    *mode_out = detect_file_mode(normalized, lowercase);
    *eol_out = default_eol_convert;
    return lowercase;
  }

  // Try as-is
  if (platform::get_file_type(normalized.c_str()) != platform::FileType::NotFound) {
    *mode_out = detect_file_mode(normalized, normalized);
    *eol_out = default_eol_convert;
    return normalized;
  }

  return "";  // Not found
}

size_t CPMEmulator::read_with_conversion(OpenFile& of, uint8_t* buffer, size_t size) {
  if (of.eof_seen) {
    return 0;
  }

  if (of.mode == MODE_BINARY || !of.eol_convert) {
    // Binary mode or no conversion - read directly
    size_t nread = fread(buffer, 1, size, of.fp);

    // Check for ^Z EOF in text mode
    if (of.mode == MODE_TEXT) {
      for (size_t i = 0; i < nread; i++) {
        if (buffer[i] == CPM_EOF) {
          of.eof_seen = true;
          return i;  // Return only data up to ^Z
        }
      }
    }

    // If we got less than requested, we're at EOF
    if (nread < size) {
      of.eof_seen = true;
    }

    return nread;
  }

  // Text mode with EOL conversion: Unix \n -> CP/M \r\n
  // But don't double-convert files that already have \r\n
  size_t out_pos = 0;
  bool last_was_cr = false;

  while (out_pos < size) {
    int ch = fgetc(of.fp);

    if (ch == EOF) {
      of.eof_seen = true;
      break;
    }

    if (ch == '\n') {
      if (last_was_cr) {
        // File already has \r\n - don't add another \r
        buffer[out_pos++] = '\n';
      } else {
        // Bare \n - convert to \r\n
        if (out_pos + 1 < size) {
          buffer[out_pos++] = '\r';
          buffer[out_pos++] = '\n';
        } else {
          // Not enough space, put back
          ungetc(ch, of.fp);
          break;
        }
      }
      last_was_cr = false;
    } else if (ch == CPM_EOF) {
      // EOF marker
      of.eof_seen = true;
      break;
    } else {
      buffer[out_pos++] = (uint8_t)ch;
      last_was_cr = (ch == '\r');
    }
  }

  return out_pos;
}

size_t CPMEmulator::write_with_conversion(OpenFile& of, const uint8_t* buffer, size_t size) {
  if (of.mode == MODE_BINARY || !of.eol_convert) {
    // Binary mode - write directly
    return fwrite(buffer, 1, size, of.fp);
  }

  // Text mode with EOL conversion: CP/M \r\n -> Unix \n
  size_t written = 0;

  for (size_t i = 0; i < size; i++) {
    uint8_t ch = buffer[i];

    if (ch == CPM_EOF) {
      // Stop at ^Z in text files
      break;
    }

    if (ch == '\r') {
      // Skip \r if next char is \n
      if (i + 1 < size && buffer[i + 1] == '\n') {
        continue;  // Skip the \r
      }
      // Otherwise write it
      if (fputc(ch, of.fp) == EOF) break;
      written++;
    } else {
      if (fputc(ch, of.fp) == EOF) break;
      written++;
    }
  }

  fflush(of.fp);
  return written;
}

void CPMEmulator::pad_to_128(uint8_t* buffer, size_t actual_size) {
  if (actual_size < 128) {
    // Pad with ^Z for CP/M compatibility
    memset(buffer + actual_size, CPM_EOF, 128 - actual_size);
  }
}

bool CPMEmulator::load_config_file(const std::string& cfg_path) {
  std::ifstream cfg(cfg_path.c_str());
  if (!cfg.is_open()) {
    fprintf(stderr, "Cannot open config file: %s\n", cfg_path.c_str());
    return false;
  }

  std::string line;
  int line_num = 0;

  while (std::getline(cfg, line)) {
    line_num++;

    // Remove comments
    size_t comment = line.find('#');
    if (comment != std::string::npos) {
      line = line.substr(0, comment);
    }

    // Trim whitespace
    size_t start = line.find_first_not_of(" \t\r\n");
    size_t end = line.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) continue;  // Empty line
    line = line.substr(start, end - start + 1);

    // Parse key = value
    size_t eq = line.find('=');
    if (eq == std::string::npos) {
      fprintf(stderr, "Config line %d: invalid format (missing =)\n", line_num);
      continue;
    }

    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);

    // Trim key and value
    // find_first_not_of returns npos for an all-blank field, and substr(npos)
    // throws.  A directive with an empty value is a plausible typo, so trim
    // defensively rather than aborting the emulator on it.
    size_t kb = key.find_first_not_of(" \t");
    key = (kb == std::string::npos) ? "" : key.substr(kb, key.find_last_not_of(" \t") - kb + 1);
    size_t vb = value.find_first_not_of(" \t");
    value = (vb == std::string::npos) ? "" : value.substr(vb, value.find_last_not_of(" \t") - vb + 1);

    if (key.empty()) {
      fprintf(stderr, "Config line %d: missing key before '='\n", line_num);
      continue;
    }

    // Expand environment variables in value
    value = expand_env_vars(value);

    // Parse configuration directives
    if (key == "program") {
      // Store program name for retrieval by main()
      config_program = value;
    } else if (key == "cd" || key == "chdir") {
      // Change working directory
      if (platform::change_directory(value.c_str()) != 0) {
        fprintf(stderr, "Config line %d: Cannot change directory to '%s': %s\n",
                line_num, value.c_str(), strerror(errno));
      } else if (debug) {
        fprintf(stderr, "Changed directory to: %s\n", value.c_str());
      }
    } else if (key == "default_mode") {
      if (value == "text") default_mode = MODE_TEXT;
      else if (value == "binary") default_mode = MODE_BINARY;
      else default_mode = MODE_AUTO;
    } else if (key == "debug") {
      debug = (value == "true" || value == "1" || value == "yes");
    } else if (key == "eol_convert") {
      default_eol_convert = (value == "true" || value == "1" || value == "yes");
    } else if (key == "ctrl_c_exit") {
      // A --ctrl-c-exit / --no-ctrl-c-exit flag outranks the config file
      if (!ctrl_c_exit_from_cli) {
        ctrl_c_exit_enabled = (value == "true" || value == "1" || value == "yes");
      }
    } else if (key == "printer") {
      set_printer_file(value);
    } else if (key == "aux_input") {
      set_aux_input_file(value);
    } else if (key == "aux_output") {
      set_aux_output_file(value);
    } else if (key.size() == 7 && (key[5] == '_' || key[5] == ' ') &&
               (key.compare(0, 5, "drive") == 0 || key.compare(0, 5, "DRIVE") == 0 ||
                key.compare(0, 5, "Drive") == 0)) {
      // drive_A .. drive_P: back a CP/M drive letter with a host directory.
      // Matched before the file-mapping fallback below, which is where these
      // used to land - a drive_B line silently became a mapping for a CP/M
      // file called DRIVE_B and did nothing.
      char letter = toupper(key[6]);
      if (letter < 'A' || letter > 'P') {
        fprintf(stderr, "Config line %d: '%s' is not a drive between A and P\n",
                line_num, key.c_str());
      } else if (value.empty()) {
        fprintf(stderr, "Config line %d: drive %c: given no directory\n",
                line_num, letter);
      } else {
        int idx = letter - 'A';
        if (platform::get_file_type(value.c_str()) != platform::FileType::Directory) {
          // Not fatal: the directory may be created before the guest runs.
          fprintf(stderr, "Config line %d: warning: drive %c: '%s' is not a directory\n",
                  line_num, letter, value.c_str());
        }
        drive_dirs[idx] = value;
        login_vector |= (qkz80_uint16)(1u << idx);
        if (debug) {
          fprintf(stderr, "Drive %c: -> %s\n", letter, value.c_str());
        }
      }
    } else {
      // Assume it's a file mapping: pattern = path [mode]
      FileMode mode = default_mode;
      bool eol_convert = default_eol_convert;

      // Check for mode specification
      size_t space = value.find_last_of(' ');
      if (space != std::string::npos) {
        std::string mode_str = value.substr(space + 1);
        if (mode_str == "text") {
          mode = MODE_TEXT;
          value = value.substr(0, space);
        } else if (mode_str == "binary") {
          mode = MODE_BINARY;
          value = value.substr(0, space);
          eol_convert = false;
        }
      }

      add_file_mapping_ex(key, value, mode, eol_convert);
    }
  }

  return true;
}

void CPMEmulator::set_printer_file(const std::string& path) {
  if (printer_file) fclose(printer_file);
  printer_file = fopen(path.c_str(), "w");
  if (!printer_file) {
    fprintf(stderr, "Warning: Cannot open printer file '%s': %s\n",
            path.c_str(), strerror(errno));
  } else if (debug) {
    fprintf(stderr, "Printer output redirected to: %s\n", path.c_str());
  }
}

void CPMEmulator::set_aux_input_file(const std::string& path) {
  if (aux_in_file) fclose(aux_in_file);
  aux_in_file = fopen(path.c_str(), "r");
  if (!aux_in_file) {
    fprintf(stderr, "Warning: Cannot open aux input file '%s': %s\n",
            path.c_str(), strerror(errno));
  } else if (debug) {
    fprintf(stderr, "Auxiliary input redirected from: %s\n", path.c_str());
  }
}

void CPMEmulator::set_aux_output_file(const std::string& path) {
  if (aux_out_file) fclose(aux_out_file);
  aux_out_file = fopen(path.c_str(), "w");
  if (!aux_out_file) {
    fprintf(stderr, "Warning: Cannot open aux output file '%s': %s\n",
            path.c_str(), strerror(errno));
  } else if (debug) {
    fprintf(stderr, "Auxiliary output redirected to: %s\n", path.c_str());
  }
}

std::string CPMEmulator::normalize_cpm_filename(const std::string& name) {
  std::string result;

  // Convert to uppercase and trim
  for (char c : name) {
    if (c != ' ') {
      result += toupper(c);
    }
  }

  return result;
}

// Check if a character is valid in CP/M filenames.
// Per the DRI CP/M manual, valid filename characters are printable
// 7-bit ASCII (0x21-0x7E) EXCEPT the following forbidden characters:
//   < > . , ; : = ? * [ ] ^ % | ( ) / backslash
// Space (0x20) is also invalid as it's the padding character.
static bool is_valid_cpm_char(char c) {
  if (c < 0x21 || c > 0x7E) return false;
  switch (c) {
    case '<': case '>': case '.': case ',': case ';':
    case ':': case '=': case '?': case '*': case '[':
    case ']': case '^': case '%': case '|': case '(':
    case ')': case '/': case '\\':
      return false;
    default:
      return true;
  }
}

// Validate FCB filename bytes at the given address.
// Checks bytes 1-8 (name) and 9-11 (extension) for valid CP/M characters.
// Each byte has its high bit stripped (attribute flags).
// Name must have at least one non-space character.
// All characters must be spaces (padding) or valid CP/M characters.
static bool validate_fcb_name(qkz80_uint8* mem, qkz80_uint16 fcb_addr) {
  bool has_nonspace = false;
  // Check name bytes (1-8) and extension bytes (9-11)
  for (int i = 1; i <= 11; i++) {
    char c = mem[fcb_addr + i] & 0x7F;  // Strip high bit
    if (c == ' ') continue;
    if (c == '?') continue;  // Wildcard, valid in search patterns
    if (!is_valid_cpm_char(c)) return false;
    if (i <= 8) has_nonspace = true;
  }
  return has_nonspace;  // Must have at least one non-space char in name
}

std::string CPMEmulator::fcb_to_filename(qkz80_uint16 fcb_addr) {
  qkz80_uint8* mem = cpu->get_mem();
  std::string filename;

  // Extract name (8 chars)
  for (int i = 0; i < 8; i++) {
    char c = mem[fcb_addr + 1 + i] & 0x7F;  // Strip high bit
    if (c != ' ') {
      filename += c;
    }
  }

  // Check for extension
  bool has_ext = false;
  for (int i = 0; i < 3; i++) {
    if ((mem[fcb_addr + 9 + i] & 0x7F) != ' ') {
      has_ext = true;
      break;
    }
  }

  if (has_ext) {
    filename += '.';
    for (int i = 0; i < 3; i++) {
      char c = mem[fcb_addr + 9 + i] & 0x7F;
      if (c != ' ') {
        filename += c;
      }
    }
  }

  return filename;
}

void CPMEmulator::filename_to_fcb(const std::string& filename, qkz80_uint16 fcb_addr) {
  qkz80_uint8* mem = cpu->get_mem();

  // Clear FCB header (16 bytes: drive + name[8] + ext[3] + ex + s1 + s2 + rc)
  // Only clear the header portion, matching CP/M CCP behavior.
  // Clearing 36 bytes from FCB2 (0x6C) would corrupt the DMA buffer at 0x80.
  memset(&mem[fcb_addr], 0, 16);

  // Extract basename if argument looks like a Unix path
  std::string base_name = filename;
  if (filename.length() > 0 && (filename[0] == '/' || (filename[0] == '.' && filename.length() > 1 && filename[1] == '/'))) {
    size_t slash = filename.rfind('/');
    if (slash != std::string::npos) {
      base_name = filename.substr(slash + 1);
    }
  }

  // Parse filename
  std::string upper_name;
  for (char c : base_name) {
    upper_name += toupper(c);
  }

  // Check for drive letter
  size_t name_start = 0;
  if (upper_name.length() >= 2 && upper_name[1] == ':') {
    char drive = upper_name[0];
    if (drive >= 'A' && drive <= 'P') {
      mem[fcb_addr] = drive - 'A' + 1;
      name_start = 2;
    }
  }

  // Find extension
  size_t dot_pos = upper_name.find('.', name_start);

  // Fill name field (8 chars, space-padded), validating characters
  size_t name_len = (dot_pos != std::string::npos) ? (dot_pos - name_start) : (upper_name.length() - name_start);
  if (name_len > 8) {
    name_len = 8;
  }

  for (size_t i = 0; i < 8; i++) {
    if (i < name_len) {
      char c = upper_name[name_start + i];
      if (!is_valid_cpm_char(c)) {
        fprintf(stderr, "Warning: invalid CP/M character '%c' in filename '%s'\n", c, filename.c_str());
        c = '_';  // Replace with underscore
      }
      mem[fcb_addr + 1 + i] = c;
    } else {
      mem[fcb_addr + 1 + i] = ' ';
    }
  }

  // Fill extension field (3 chars, space-padded), validating characters
  if (dot_pos != std::string::npos) {
    size_t ext_start = dot_pos + 1;
    size_t ext_len = upper_name.length() - ext_start;
    if (ext_len > 3) {
      ext_len = 3;
    }

    for (size_t i = 0; i < 3; i++) {
      if (i < ext_len) {
        char c = upper_name[ext_start + i];
        if (!is_valid_cpm_char(c)) {
          fprintf(stderr, "Warning: invalid CP/M character '%c' in filename '%s'\n", c, filename.c_str());
          c = '_';  // Replace with underscore
        }
        mem[fcb_addr + 9 + i] = c;
      } else {
        mem[fcb_addr + 9 + i] = ' ';
      }
    }
  } else {
    // No extension
    for (int i = 0; i < 3; i++) {
      mem[fcb_addr + 9 + i] = ' ';
    }
  }
}

bool CPMEmulator::handle_pc(qkz80_uint16 pc) {
  // Check for JMP 0 (exit)
  if (pc == 0) {
    fprintf(stderr, "Program exit via JMP 0\n");
    do_save_memory();
    exit(0);
  }

  // Check for BDOS call (trap at BDOS_BASE where jump from 0x0005 lands)
  if (pc == BDOS_BASE) {
    qkz80_uint8 func = cpu->get_reg8(qkz80::reg_C);
    bdos_call(func);

    // Simulate RET from BDOS
    qkz80_uint16 ret_addr = cpu->pop_word();
    cpu->regs.PC.set_pair16(ret_addr);
    return true;
  }

  // Check for BIOS calls (magic addresses 0xFF00-0xFF10)
  if (pc >= 0xFF00 && pc < 0xFF20) {
    int bios_func = (pc - 0xFF00) * 3;
    bios_call(bios_func);

    // Simulate RET from BIOS
    qkz80_uint16 ret_addr = cpu->pop_word();
    cpu->regs.PC.set_pair16(ret_addr);
    return true;
  }

  return false;
}

void CPMEmulator::bdos_call(qkz80_uint8 func) {
  if (debug || debug_bdos_funcs.count(func)) {
    fprintf(stderr, "BDOS call %d\n", func);
  }

  switch (func) {
  case 0:  // System Reset
    fprintf(stderr, "System reset\n");
    exit(0);
    break;

  case 1:  // Console Input
    bdos_read_console();
    break;

  case 2:  // Console Output
    bdos_write_console(cpu->get_reg8(qkz80::reg_E));
    break;

  case 3:  // Auxiliary Input
    bdos_aux_input();
    break;

  case 4:  // Auxiliary Output
    bdos_aux_output();
    break;

  case 5:  // List Output (Printer)
    bdos_list_output();
    break;

  case 6:  // Direct Console I/O
    bdos_direct_console_io();
    break;

  case 7:  // Get IOBYTE
    bdos_get_iobyte();
    break;

  case 8:  // Set IOBYTE
    bdos_set_iobyte();
    break;

  case 9:  // Print String
    bdos_write_string();
    break;

  case 10: // Read Console Buffer
    bdos_read_console_buffer();
    break;

  case 11: // Console Status
    bdos_console_status();
    break;

  case 12: // Get Version
    bdos_get_version();
    break;

  case 13: // Reset Disk System
    bdos_reset_disk();
    break;

  case 14: // Select Disk
    bdos_set_drive();
    break;

  case 15: // Open File
    bdos_open_file();
    break;

  case 16: // Close File
    bdos_close_file();
    break;

  case 17: // Search First
    bdos_search_first();
    break;

  case 18: // Search Next
    bdos_search_next();
    break;

  case 19: // Delete File
    bdos_delete_file();
    break;

  case 20: // Read Sequential
    bdos_read_sequential();
    break;

  case 21: // Write Sequential
    bdos_write_sequential();
    break;

  case 22: // Make File
    bdos_make_file();
    break;

  case 23: // Rename File
    bdos_rename_file();
    break;

  case 24: // Get Login Vector
    bdos_get_login_vector();
    break;

  case 25: // Get Current Drive
    bdos_get_current_drive();
    break;

  case 26: // Set DMA Address
    bdos_get_set_dma();
    break;

  case 27: // Get Allocation Vector
    bdos_get_allocation_vector();
    break;

  case 28: // Write Protect Disk
    bdos_write_protect_disk();
    break;

  case 29: // Get Read-Only Vector
    bdos_get_readonly_vector();
    break;

  case 30: // Set File Attributes
    bdos_set_file_attributes();
    break;

  case 31: // Get Disk Parameter Block
    bdos_get_dpb();
    break;

  case 32: // Get/Set User Number
    bdos_get_set_user();
    break;

  case 33: // Read Random
    bdos_read_random();
    break;

  case 34: // Write Random
    bdos_write_random();
    break;

  case 35: // Compute File Size
    bdos_file_size();
    break;

  case 36: // Set Random Record
    bdos_set_random_record();
    break;

  case 37: // Reset Drive
    bdos_reset_drive();
    break;

  case 38: // Access Free Space
    // Return A=0 indicating success
    cpu->set_reg8(0, qkz80::reg_A);
    break;

  case 39: // Free Space
    // No operation - just return
    break;

  case 40: // Write Random with Zero Fill
    bdos_write_random_zero_fill();
    break;

  case 48: // Flush Buffers (CP/M 3+)
    // Our emulator writes directly to files, so just return success
    cpu->set_reg8(0, qkz80::reg_A);
    break;

  default:
    fprintf(stderr, "Unimplemented BDOS function %d\n", func);
    cpu->set_reg8(0xFF, qkz80::reg_A);
    break;
  }
}

void CPMEmulator::bdos_write_console(qkz80_uint8 ch) {
  console_output(ch);
}

void CPMEmulator::bdos_write_string() {
  qkz80_uint16 addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();

  while (mem[addr] != '$') {
    console_output(mem[addr]);
    addr++;
  }
}

void CPMEmulator::bdos_read_console() {
  int ch = console_getchar_blocking();
  if (ch == -1 || ch == EOF) ch = '\r';  // EOF becomes CR (Enter) for non-interactive use
  check_ctrl_c_exit(ch);  // Track ^C for exit, pass through to program
  if (ch == '\n') ch = '\r';  // Convert LF to CR for CP/M
  cpu->set_reg8(ch & 0x7F, qkz80::reg_A);
}

// Columns one stored byte occupies when echoed: 1 for a printable character,
// 2 for the ^x form every other byte is shown in
static int rdbuf_echo_width(qkz80_uint8 ch) {
  return (ch >= 0x20 && ch < 0x7F) ? 1 : 2;
}

// Address of byte 'offset' of the function 10 buffer, wrapped into the 64K the
// guest actually has.  A buffer placed near 0FFFFh must wrap the way the CPU
// does rather than run off the end of the emulator's memory block.
static qkz80_uint16 rdbuf_at(qkz80_uint16 buf_addr, int offset) {
  return (qkz80_uint16)((buf_addr + offset) & 0xFFFF);
}

// Echo one byte from the BDOS function 10 line editor.
// Deliberately putchar() and not console_output(): this is the user's own
// keystroke coming back, so it must not be run through the ADM-3A translator.
// ^P mirrors it to the printer file named by CPM_PRINTER or the 'printer'
// config directive; with no printer file open there is nowhere to send it.
void CPMEmulator::rdbuf_echo(int ch) {
  putchar(ch);
  if (printer_echo && printer_file) {
    fputc(ch & 0x7F, printer_file);
    fflush(printer_file);
  }
}

// Echo one buffered byte the way CP/M shows it: printable as itself,
// anything else - TAB and ESC included - as '^' plus the letter it is made of
void CPMEmulator::rdbuf_echo_stored(qkz80_uint8 ch) {
  if (ch >= 0x20 && ch < 0x7F) {
    rdbuf_echo(ch);
  } else {
    rdbuf_echo('^');
    rdbuf_echo(ch + 0x40);
  }
}

void CPMEmulator::bdos_read_console_buffer() {
  // BDOS function 10: Read Console Buffer
  // DE points to buffer:
  //   Byte 0: Maximum characters to read (1-255, but typically <=127)
  //   Byte 1: Actual characters read (filled by this function)
  //   Bytes 2+: Characters read (up to max)
  //
  // These are the only keys the editor consumes, matching CP/M 2.2 RDBUF:
  //   CR, LF    End the line
  //   RUB, ^H   Delete the last character and erase its echoed width
  //   ^U        Cancel the whole line: echo '#' and start a fresh one
  //   ^X        Cancel the current physical line, erasing it off the screen
  //   ^E        Physical end of line - echo CR LF and keep collecting
  //   ^R        Retype the line so far on a fresh physical line
  //   ^P        Toggle console-to-printer echo
  //   ^S        Consumed and ignored (it pauses output on real hardware)
  // Every other byte is stored for the program and echoed - a printable
  // character as itself, any other control character as ^x.
  // The read ends on CR, LF, EOF, or a full buffer.

  qkz80_uint16 buf_addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();

  qkz80_uint8 max_chars = mem[buf_addr] & 0xFF;
  if (max_chars == 0) {
    mem[rdbuf_at(buf_addr, 1)] = 0;
    cpu->set_reg8(0, qkz80::reg_A);
    return;
  }

  int count = 0;       // Characters stored in the buffer (bytes 2+)
  int line_start = 0;  // First stored character echoed on the current physical
                       // line - ^E, ^U and ^X move it, and RUB stops there

  for (;;) {
    int ch = console_getchar_blocking();
    // Ask before anything else reads it: the flag describes the character we
    // just took, and any further console call would overwrite it
    bool synthesized = platform::console_last_char_synthesized();
    if (ch == -1 || ch == EOF) break;  // EOF ends the line; a typed ^Z does not

    check_ctrl_c_exit(ch);

    // Every test below is on the raw byte and only what gets stored is masked,
    // matching the other three read sites.  Masking first would turn an 8-bit
    // 0x8D into a CR and silently submit the line someone was still typing -
    // reachable now that raw mode no longer strips the 8th bit.
    //
    // A byte the platform layer synthesized from a special key is the key the
    // user pressed, not an instruction to this editor.  A Windows arrow key
    // arrives here as a WordStar diamond code, and Down must not cancel the
    // line being typed the way a typed ^X does, so those go straight to the
    // program.
    if (!synthesized) {
      if (ch == '\r' || ch == '\n') {
        // End of line - echo CR LF and finish
        rdbuf_echo('\r');
        rdbuf_echo('\n');
        fflush(stdout);
        break;
      }
      if (ch == 0x7F || ch == 0x08) {  // RUB or ^H - delete last character
        if (count > line_start) {
          count--;
          // Erase what it echoed as: 1 column printable, 2 for a ^x
          int width = rdbuf_echo_width(mem[rdbuf_at(buf_addr, 2 + count)]);
          for (int i = 0; i < width; i++) {
            rdbuf_echo('\b');
            rdbuf_echo(' ');
            rdbuf_echo('\b');
          }
          fflush(stdout);
        }
        continue;
      }
      if (ch == 0x15) {  // ^U - cancel line
        // Authentic CP/M 2.2: mark the abandoned line with '#' and continue on
        // a fresh physical line, rather than erasing it in place.  ^U drops the
        // whole logical line, including anything a ^E left on the line above.
        rdbuf_echo('#');
        rdbuf_echo('\r');
        rdbuf_echo('\n');
        fflush(stdout);
        count = 0;
        line_start = 0;
        continue;
      }
      if (ch == 0x18) {  // ^X - cancel line, erasing it off the screen
        // Only back to the start of the physical line, and the buffer drops
        // exactly the characters that were erased - otherwise text left
        // visible above a ^E would vanish from the buffer while still on screen
        while (count > line_start) {
          count--;
          int width = rdbuf_echo_width(mem[rdbuf_at(buf_addr, 2 + count)]);
          for (int i = 0; i < width; i++) {
            rdbuf_echo('\b');
            rdbuf_echo(' ');
            rdbuf_echo('\b');
          }
        }
        fflush(stdout);
        continue;
      }
      if (ch == 0x05) {  // ^E - physical end of line
        // Break the screen line but keep collecting the same logical line
        rdbuf_echo('\r');
        rdbuf_echo('\n');
        fflush(stdout);
        line_start = count;  // Backspacing must not walk onto the line above
        continue;
      }
      if (ch == 0x12) {  // ^R - retype the line
        rdbuf_echo('#');
        rdbuf_echo('\r');
        rdbuf_echo('\n');
        for (int i = 0; i < count; i++) {
          rdbuf_echo_stored(mem[rdbuf_at(buf_addr, 2 + i)]);
        }
        fflush(stdout);
        line_start = 0;  // The whole buffer is now on this physical line
        continue;
      }
      if (ch == 0x10) {  // ^P - toggle console-to-printer echo
        // The destination is whatever CPM_PRINTER or the 'printer' config
        // directive selected; with none configured this does nothing
        printer_echo = !printer_echo;
        continue;
      }
      if (ch == 0x13) {  // ^S - consumed and ignored
        // ^S pauses console output on real hardware, which means nothing while
        // we are collecting a line
        continue;
      }
    }

    // Everything else is stored for the program and echoed.
    // ^C lands here too: real RDBUF warm boots on a ^C in column one, but a
    // warm boot in this emulator is program termination, and the five-^C hatch
    // already covers the escape case - so ^C is stored and echoed as ^C like
    // any other control character and the program decides what it means.
    qkz80_uint8 stored = (qkz80_uint8)(ch & 0x7F);
    mem[rdbuf_at(buf_addr, 2 + count)] = stored;
    count++;
    rdbuf_echo_stored(stored);
    fflush(stdout);

    // A full buffer ends the read, the same as a CR - "console input is
    // terminated when either the input buffer overflows or a carriage return
    // or line feed is typed".  No CR LF is echoed here, matching the terminal
    // the guest is left looking at on real CP/M.
    if (count >= max_chars) break;
  }

  // Store actual count
  mem[rdbuf_at(buf_addr, 1)] = count;
  cpu->set_reg8(0, qkz80::reg_A);
}

void CPMEmulator::bdos_aux_input() {
  // Auxiliary (Reader) input
  if (aux_in_file) {
    int ch = fgetc(aux_in_file);
    if (ch == EOF) ch = 0x1A;  // ^Z
    cpu->set_reg8(ch & 0x7F, qkz80::reg_A);
  } else {
    // No aux input configured - return ^Z
    cpu->set_reg8(0x1A, qkz80::reg_A);
  }
}

void CPMEmulator::bdos_aux_output() {
  // Auxiliary (Punch) output
  qkz80_uint8 ch = cpu->get_reg8(qkz80::reg_E);
  if (aux_out_file) {
    fputc(ch & 0x7F, aux_out_file);
    fflush(aux_out_file);
  }
  // If no file, silently ignore
}

void CPMEmulator::bdos_list_output() {
  // List (Printer) output - LPRINT uses this!
  qkz80_uint8 ch = cpu->get_reg8(qkz80::reg_E);
  if (printer_file) {
    fputc(ch & 0x7F, printer_file);
    fflush(printer_file);
  } else {
    // No printer file - output to stdout with prefix
    fprintf(stdout, "[PRINTER] %c", ch & 0x7F);
    fflush(stdout);
  }
}

void CPMEmulator::bdos_get_iobyte() {
  cpu->set_reg8(iobyte, qkz80::reg_A);
}

void CPMEmulator::bdos_set_iobyte() {
  iobyte = cpu->get_reg8(qkz80::reg_E);
}

void CPMEmulator::bdos_console_status() {
  // Return 0xFF if character ready, 0x00 if not
  cpu->set_reg8(platform::stdin_has_data() ? 0xFF : 0x00, qkz80::reg_A);
}

void CPMEmulator::bdos_get_version() {
  // CP/M 2.2 version
  cpu->set_reg8(0x22, qkz80::reg_A);
  cpu->set_reg8(0x22, qkz80::reg_L);
  cpu->set_reg8(0x00, qkz80::reg_B);
  cpu->set_reg8(0x00, qkz80::reg_H);
}

void CPMEmulator::bdos_get_set_dma() {
  current_dma = cpu->get_reg16(qkz80::regp_DE);
  if (debug) {
    fprintf(stderr, "Set DMA to 0x%04X\n", current_dma);
  }
}

void CPMEmulator::bdos_get_current_drive() {
  cpu->set_reg8(current_drive, qkz80::reg_A);
}

void CPMEmulator::bdos_set_drive() {
  // E is 0-based here (0 = A:), unlike the 1-based FCB drive byte.
  current_drive = cpu->get_reg8(qkz80::reg_E) & 0x0F;
  login_vector |= (qkz80_uint16)(1u << current_drive);
  // The low nibble of 0x0004 is the current drive and the BDOS keeps it up
  // to date, so a program that reads it there sees the same answer BDOS 25
  // gives.  It was written once at startup and never updated before.
  qkz80_uint8* mem = cpu->get_mem();
  mem[DRVUSER_ADDR] = (mem[DRVUSER_ADDR] & 0xF0) | current_drive;
  if (debug) {
    fprintf(stderr, "Set drive to %c:\n", 'A' + current_drive);
  }
}

void CPMEmulator::bdos_get_set_user() {
  qkz80_uint8 code = cpu->get_reg8(qkz80::reg_E);

  if (code == 0xFF) {
    // Get user number
    cpu->set_reg8(current_user, qkz80::reg_A);
  } else {
    // Set user number
    current_user = code & 0x0F;
  }
}

void CPMEmulator::bdos_open_file() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);

  if (!validate_fcb_name(cpu->get_mem(), fcb_addr)) {
    if (debug || debug_bdos_funcs.count(15)) {
      fprintf(stderr, "BDOS Open: rejected invalid FCB filename\n");
    }
    cpu->set_reg8(0xFF, qkz80::reg_A);
    return;
  }

  std::string filename = fcb_to_filename(fcb_addr);

  FileMode mode;
  bool eol_convert;
  std::string unix_path = find_unix_file_ex(filename, &mode, &eol_convert,
                                            cpu->get_mem()[fcb_addr]);

  if (debug || debug_bdos_funcs.count(15)) {
    fprintf(stderr, "BDOS Open: '%s' -> '%s' (mode: %s)\n", filename.c_str(),
            unix_path.empty() ? "(not found)" : unix_path.c_str(),
            mode == MODE_TEXT ? "text" : "binary");
  }

  if (unix_path.empty()) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // File not found
    return;
  }

  FILE* fp = fopen(unix_path.c_str(), "r+b");
  if (!fp) {
    fp = fopen(unix_path.c_str(), "rb");
    if (!fp) {
      cpu->set_reg8(0xFF, qkz80::reg_A);
      return;
    }
  }

  OpenFile of;
  of.fp = fp;
  of.unix_path = unix_path;
  of.cpm_name = filename;
  of.mode = mode;
  of.eol_convert = eol_convert;
  of.position = 0;
  of.eof_seen = false;
  of.write_mode = false;
  open_files[fcb_addr] = of;

  // Clear extent and record count
  qkz80_uint8* mem = cpu->get_mem();
  mem[fcb_addr + 12] = 0;  // EX
  mem[fcb_addr + 15] = 0x80;  // RC (128 records max per extent)

  cpu->set_reg8(0, qkz80::reg_A);  // Success
}

void CPMEmulator::bdos_close_file() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);

  if (debug || debug_bdos_funcs.count(16)) {
    fprintf(stderr, "Close file: FCB at %04X\n", fcb_addr);
  }

  auto it = open_files.find(fcb_addr);
  if (it != open_files.end()) {
    // Flush any pending writes
    if (it->second.write_mode && it->second.write_buffer.size() > 0) {
      write_with_conversion(it->second, it->second.write_buffer.data(),
                            it->second.write_buffer.size());
    }

    if (debug || debug_bdos_funcs.count(16)) {
      fprintf(stderr, "Close file: closing '%s'\n", it->second.cpm_name.c_str());
    }
    fclose(it->second.fp);
    open_files.erase(it);
  } else {
    if (debug || debug_bdos_funcs.count(16)) {
      fprintf(stderr, "Close file: file not open (OK)\n");
    }
  }
  // Always return success - CP/M close is idempotent
  // Only return 0xFF if there's an actual disk error writing the directory
  cpu->set_reg8(0, qkz80::reg_A);

  if (debug || debug_bdos_funcs.count(16)) {
    fprintf(stderr, "Close file: returning A=%02X\n", cpu->get_reg8(qkz80::reg_A));
  }
}

void CPMEmulator::bdos_read_sequential() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();

  auto it = open_files.find(fcb_addr);
  if (it == open_files.end()) {
    if (debug || debug_bdos_funcs.count(20)) {
      fprintf(stderr, "Read sequential: FCB %04X not open\n", fcb_addr);
    }
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error: file not open
    return;
  }

  // Read 128 bytes to DMA with conversion
  uint8_t buffer[128];
  size_t nread = read_with_conversion(it->second, buffer, 128);

  // CP/M convention: return A=0 (success) when data is available,
  // return A=1 (EOF) only when no more data can be read.
  // For partial records at end of file, return success with Ctrl-Z padding.
  if (nread == 0) {
    cpu->set_reg8(1, qkz80::reg_A);  // EOF - no data available
    if (debug || debug_bdos_funcs.count(20)) {
      fprintf(stderr, "Read sequential: FCB %04X file '%s' -> EOF (no data)\n", fcb_addr, it->second.cpm_name.c_str());
    }
  } else {
    // Pad to 128 bytes if needed
    if (nread < 128) {
      pad_to_128(buffer, nread);
    }

    // Copy to DMA
    memcpy(&mem[current_dma], buffer, 128);
    cpu->set_reg8(0, qkz80::reg_A);  // Success

    if (debug || debug_bdos_funcs.count(20)) {
      fprintf(stderr, "Read sequential: FCB %04X file '%s' read %zu bytes, returning A=0\n",
              fcb_addr, it->second.cpm_name.c_str(), nread);
    }
  }

  // Update current record in FCB
  mem[fcb_addr + 32]++;
}

void CPMEmulator::bdos_write_sequential() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();

  auto it = open_files.find(fcb_addr);
  if (it == open_files.end()) {
    // File not open - try to open it for writing
    if (debug || debug_bdos_funcs.count(21)) {
      fprintf(stderr, "Write sequential: FCB %04X not open, trying to open\n", fcb_addr);
    }
    bdos_open_file();
    it = open_files.find(fcb_addr);
    if (it == open_files.end()) {
      if (debug || debug_bdos_funcs.count(21)) {
        fprintf(stderr, "Write sequential: FCB %04X failed to open\n", fcb_addr);
      }
      cpu->set_reg8(0xFF, qkz80::reg_A);
      return;
    }
  }

  it->second.write_mode = true;

  // Write 128 bytes from DMA with conversion
  size_t nwritten = write_with_conversion(it->second, (uint8_t*)&mem[current_dma], 128);

  if (nwritten > 0) {
    cpu->set_reg8(0, qkz80::reg_A);  // Success
  } else {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
  }

  // Update current record in FCB
  mem[fcb_addr + 32]++;
}

void CPMEmulator::bdos_make_file() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);

  if (!validate_fcb_name(cpu->get_mem(), fcb_addr)) {
    if (debug || debug_bdos_funcs.count(22)) {
      fprintf(stderr, "Make file: rejected invalid FCB filename\n");
    }
    cpu->set_reg8(0xFF, qkz80::reg_A);
    return;
  }

  std::string filename = fcb_to_filename(fcb_addr);

  if (debug || debug_bdos_funcs.count(22)) {
    fprintf(stderr, "Make file: %s\n", filename.c_str());
  }

  // Convert to lowercase for Unix
  std::string unix_name;
  for (char c : filename) {
    unix_name += tolower(c);
  }

  // Make does not go through find_unix_file_ex - there is nothing to find
  // yet - so it has to apply the drive directory itself.  Without this a
  // guest that makes B:FOO.TXT and then opens B:FOO.TXT gets two different
  // files: the make lands in the working directory, the open looks in B:.
  const std::string& ddir = drive_dir(fcb_drive_index(cpu->get_mem()[fcb_addr]));
  if (!ddir.empty()) {
    unix_name = join_path(ddir, unix_name);
  }

  FILE* fp = fopen(unix_name.c_str(), "w+b");
  if (!fp) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
    return;
  }

  OpenFile of;
  of.fp = fp;
  of.unix_path = unix_name;
  of.cpm_name = filename;
  of.mode = default_mode;
  of.eol_convert = default_eol_convert;
  of.position = 0;
  of.eof_seen = false;
  of.write_mode = true;
  open_files[fcb_addr] = of;

  qkz80_uint8* mem = cpu->get_mem();
  mem[fcb_addr + 12] = 0;  // EX
  mem[fcb_addr + 15] = 0;  // RC

  cpu->set_reg8(0, qkz80::reg_A);  // Success
}

void CPMEmulator::bdos_delete_file() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);

  if (!validate_fcb_name(cpu->get_mem(), fcb_addr)) {
    if (debug || debug_bdos_funcs.count(19)) {
      fprintf(stderr, "Delete file: rejected invalid FCB filename\n");
    }
    cpu->set_reg8(0xFF, qkz80::reg_A);
    return;
  }

  std::string filename = fcb_to_filename(fcb_addr);

  FileMode mode;
  bool eol_convert;
  std::string unix_path = find_unix_file_ex(filename, &mode, &eol_convert,
                                            cpu->get_mem()[fcb_addr]);

  if (debug || debug_bdos_funcs.count(19)) {
    fprintf(stderr, "Delete file: %s -> %s\n", filename.c_str(),
            unix_path.empty() ? "(not found)" : unix_path.c_str());
  }

  if (unix_path.empty() || !platform::delete_file(unix_path.c_str())) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
  } else {
    cpu->set_reg8(0, qkz80::reg_A);  // Success
  }
}

void CPMEmulator::bdos_read_random() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();

  auto it = open_files.find(fcb_addr);
  if (it == open_files.end()) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error: file not open
    return;
  }

  // Get random record number from FCB bytes 33-35 (r0, r1, r2)
  uint32_t record_num = mem[fcb_addr + 33] |
                        (mem[fcb_addr + 34] << 8) |
                        (mem[fcb_addr + 35] << 16);

  // Calculate byte position (each record is 128 bytes)
  long position = record_num * 128L;

  // Seek to position
  if (fseek(it->second.fp, position, SEEK_SET) != 0) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error: seek failed
    return;
  }

  // Read 128 bytes to DMA
  size_t nread = fread(&mem[current_dma], 1, 128, it->second.fp);

  if (nread == 0) {
    cpu->set_reg8(1, qkz80::reg_A);  // EOF
  } else {
    // Pad with ^Z if less than 128 bytes
    if (nread < 128) {
      memset(&mem[current_dma + nread], 0x1A, 128 - nread);
    }
    cpu->set_reg8(0, qkz80::reg_A);  // Success
  }
}

void CPMEmulator::bdos_write_random() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();

  auto it = open_files.find(fcb_addr);
  if (it == open_files.end()) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error: file not open
    return;
  }

  // Get random record number from FCB bytes 33-35 (r0, r1, r2)
  uint32_t record_num = mem[fcb_addr + 33] |
                        (mem[fcb_addr + 34] << 8) |
                        (mem[fcb_addr + 35] << 16);

  // Calculate byte position (each record is 128 bytes)
  long position = record_num * 128L;

  // Seek to position
  if (fseek(it->second.fp, position, SEEK_SET) != 0) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error: seek failed
    return;
  }

  // Write 128 bytes from DMA
  size_t nwritten = fwrite(&mem[current_dma], 1, 128, it->second.fp);
  fflush(it->second.fp);

  if (nwritten != 128) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
  } else {
    cpu->set_reg8(0, qkz80::reg_A);  // Success
  }
}

void CPMEmulator::bdos_file_size() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();

  if (!validate_fcb_name(mem, fcb_addr)) {
    if (debug || debug_bdos_funcs.count(35)) {
      fprintf(stderr, "File size: rejected invalid FCB filename\n");
    }
    cpu->set_reg8(0xFF, qkz80::reg_A);
    return;
  }

  std::string filename = fcb_to_filename(fcb_addr);

  FileMode mode;
  bool eol_convert;
  std::string unix_path = find_unix_file_ex(filename, &mode, &eol_convert,
                                            cpu->get_mem()[fcb_addr]);

  if (unix_path.empty()) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error: file not found
    return;
  }

  int64_t file_size = platform::get_file_size(unix_path.c_str());
  if (file_size < 0) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
    return;
  }

  // File size in 128-byte records (round up)
  uint32_t records = (file_size + 127) / 128;

  // Store in FCB bytes 33-35 (r0, r1, r2)
  mem[fcb_addr + 33] = records & 0xFF;
  mem[fcb_addr + 34] = (records >> 8) & 0xFF;
  mem[fcb_addr + 35] = (records >> 16) & 0xFF;

  cpu->set_reg8(0, qkz80::reg_A);  // Success
}

void CPMEmulator::bdos_set_random_record() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();

  // Convert current sequential position to random record number
  // Record number = (EX * 128) + CR
  uint8_t ex = mem[fcb_addr + 12];  // Extent
  uint8_t cr = mem[fcb_addr + 32];  // Current record

  uint32_t record_num = (ex * 128) + cr;

  // Store in r0-r2
  mem[fcb_addr + 33] = record_num & 0xFF;
  mem[fcb_addr + 34] = (record_num >> 8) & 0xFF;
  mem[fcb_addr + 35] = (record_num >> 16) & 0xFF;

  // No return value for this function
}

void CPMEmulator::bdos_rename_file() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);

  // In CP/M, rename uses a special FCB format:
  // Bytes 0-15: old filename (standard FCB format)
  // Bytes 16-31: new filename

  if (!validate_fcb_name(cpu->get_mem(), fcb_addr) ||
      !validate_fcb_name(cpu->get_mem(), fcb_addr + 16)) {
    if (debug || debug_bdos_funcs.count(23)) {
      fprintf(stderr, "Rename: rejected invalid FCB filename\n");
    }
    cpu->set_reg8(0xFF, qkz80::reg_A);
    return;
  }

  std::string old_name = fcb_to_filename(fcb_addr);

  FileMode mode;
  bool eol_convert;
  // CP/M 2.2 takes the drive from the first FCB only and cannot rename a
  // file onto another drive; the destination keeps the source's directory.
  std::string old_path = find_unix_file_ex(old_name, &mode, &eol_convert,
                                           cpu->get_mem()[fcb_addr]);

  if (old_path.empty()) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error: old file not found
    return;
  }

  // Extract new name from second FCB (at offset +16)
  std::string new_name = fcb_to_filename(fcb_addr + 16);

  // Create new path in same directory as old file
  size_t last_slash = old_path.find_last_of('/');
  std::string new_path;
  if (last_slash != std::string::npos) {
    new_path = old_path.substr(0, last_slash + 1);
  }

  // Convert new name to lowercase for Unix
  for (char c : new_name) {
    new_path += tolower(c);
  }

  if (debug || debug_bdos_funcs.count(23)) {
    fprintf(stderr, "Rename: %s -> %s\n", old_path.c_str(), new_path.c_str());
  }

  if (rename(old_path.c_str(), new_path.c_str()) != 0) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
  } else {
    // Remember where the renamed file went, but only when the drive is not
    // configured.  file_map has no drive dimension and is consulted before
    // the drive directory, so an entry planted here for a file on B: would
    // answer for A:NEWNAME too.  On a configured drive the rename stays
    // inside that directory and the drive lookup finds it without help.
    if (drive_dir(fcb_drive_index(cpu->get_mem()[fcb_addr])).empty()) {
      file_map[normalize_cpm_filename(new_name)] = new_path;
    }
    cpu->set_reg8(0, qkz80::reg_A);  // Success
  }
}

void CPMEmulator::bdos_direct_console_io() {
  qkz80_uint8 e_reg = cpu->get_reg8(qkz80::reg_E);

  if (e_reg == 0xFF) {
    // Input mode - return character if available, 0 if not
    if (platform::stdin_has_data()) {
      int ch = platform::console_getchar();
      if (ch == -1 || ch == EOF) ch = 0;
      check_ctrl_c_exit(ch);  // Track ^C for exit, pass through to program
      if (ch == '\n') ch = '\r';  // Convert LF to CR for CP/M
      cpu->set_reg8(ch & 0x7F, qkz80::reg_A);
    } else {
      cpu->set_reg8(0, qkz80::reg_A);
    }
  } else if (e_reg == 0xFE) {
    // Status check - return 0xFF if char ready, 0 if not
    cpu->set_reg8(platform::stdin_has_data() ? 0xFF : 0, qkz80::reg_A);
  } else {
    // Output mode - send character through terminal translator
    console_output(e_reg);
    // No return value for output
  }
}

void CPMEmulator::bdos_reset_disk() {
  // Reset disk system - close all files
  for (auto& pair : open_files) {
    if (pair.second.fp) {
      fclose(pair.second.fp);
    }
  }
  open_files.clear();

  // A stale search now names files in a specific directory, so it cannot be
  // allowed to outlive the reset.
  search_results.clear();
  search_index = 0;

  // Reset to drive A, user 0
  current_drive = 0;
  current_user = 0;
  // Back to whatever the configuration logged in, dropping drives that were
  // only reachable because BDOS 14 had selected them.
  login_vector = 0x0001;
  for (int i = 0; i < 16; i++) {
    if (!drive_dirs[i].empty()) login_vector |= (qkz80_uint16)(1u << i);
  }
  qkz80_uint8* rmem = cpu->get_mem();
  rmem[DRVUSER_ADDR] = 0x00;

  // No return value
}

// Helper: match FCB-style pattern (with '?' wildcards) against a filename
// Both pattern and filename should be space-padded 8+3 format
static bool match_fcb_pattern(const char* pattern_name, const char* pattern_ext,
                               const char* file_name, const char* file_ext) {
  // Match name (8 chars)
  for (int i = 0; i < 8; i++) {
    char p = pattern_name[i];
    char f = file_name[i];
    if (p != '?' && toupper(p) != toupper(f)) {
      return false;
    }
  }
  // Match extension (3 chars)
  for (int i = 0; i < 3; i++) {
    char p = pattern_ext[i];
    char f = file_ext[i];
    if (p != '?' && toupper(p) != toupper(f)) {
      return false;
    }
  }
  return true;
}

// Helper: convert Unix filename to CP/M 8.3 format (space-padded)
// Returns false if the filename contains illegal CP/M characters
static bool unix_to_cpm_83(const std::string& unix_name,
                            char* name_out, char* ext_out) {
  // Initialize with spaces
  memset(name_out, ' ', 8);
  memset(ext_out, ' ', 3);

  // Find extension
  size_t dot = unix_name.rfind('.');
  std::string name_part, ext_part;

  if (dot != std::string::npos && dot > 0) {
    name_part = unix_name.substr(0, dot);
    ext_part = unix_name.substr(dot + 1);
  } else {
    name_part = unix_name;
  }

  // Validate and copy name (up to 8 chars)
  for (size_t i = 0; i < name_part.length() && i < 8; i++) {
    if (!is_valid_cpm_char(name_part[i])) return false;
    name_out[i] = toupper(name_part[i]);
  }

  // Validate and copy extension (up to 3 chars)
  for (size_t i = 0; i < ext_part.length() && i < 3; i++) {
    if (!is_valid_cpm_char(ext_part[i])) return false;
    ext_out[i] = toupper(ext_part[i]);
  }

  // Reject if name is too long (wouldn't fit in 8.3)
  if (name_part.length() > 8 || ext_part.length() > 3) return false;

  return true;
}

void CPMEmulator::bdos_search_first() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();

  // Extract pattern from FCB
  char pattern_name[8], pattern_ext[3];
  memcpy(pattern_name, &mem[fcb_addr + 1], 8);
  memcpy(pattern_ext, &mem[fcb_addr + 9], 3);

  // A search is scoped to the FCB's drive.  An unconfigured drive scans ".",
  // which is what every search did before drives existed.
  const std::string& search_ddir = drive_dir(fcb_drive_index(mem[fcb_addr]));
  std::string scan_dir = search_ddir.empty() ? std::string(".") : search_ddir;

  // Get user from FCB byte 0 for '?' user matching
  search_user = current_user;

  // Clear previous results and scan directory
  search_results.clear();
  search_index = 0;

  // Store pattern for debug output
  search_pattern = std::string(pattern_name, 8) + "." + std::string(pattern_ext, 3);

  if (debug || debug_bdos_funcs.count(17)) {
    fprintf(stderr, "Search First: pattern='%s'\n", search_pattern.c_str());
  }

  // Track which CP/M names we've already added (to avoid duplicates from mappings + dir)
  std::set<std::string> added_cpm_names;

  // First, check file mappings - these define explicit CP/M names
  for (const auto& mapping : file_mappings) {
    // Check if the Unix file exists and is not a directory
    platform::FileType ftype = platform::get_file_type(mapping.unix_pattern.c_str());
    if (ftype != platform::FileType::Regular) continue;

    // Get the CP/M name from the mapping
    char file_name[8], file_ext[3];
    if (!unix_to_cpm_83(mapping.cpm_pattern, file_name, file_ext)) continue;

    if (match_fcb_pattern(pattern_name, pattern_ext, file_name, file_ext)) {
      search_results.push_back(make_search_result(mapping.unix_pattern, file_name, file_ext));
      // Remember this CP/M name to avoid duplicates
      std::string cpm_name = std::string(file_name, 8) + std::string(file_ext, 3);
      added_cpm_names.insert(cpm_name);
    }
  }

  // Also check legacy file_map
  for (const auto& pair : file_map) {
    // Check if the file exists and is not a directory
    platform::FileType ftype = platform::get_file_type(pair.second.c_str());
    if (ftype != platform::FileType::Regular) continue;

    char file_name[8], file_ext[3];
    if (!unix_to_cpm_83(pair.first, file_name, file_ext)) continue;

    std::string cpm_name = std::string(file_name, 8) + std::string(file_ext, 3);
    if (added_cpm_names.count(cpm_name)) continue;  // Already added

    if (match_fcb_pattern(pattern_name, pattern_ext, file_name, file_ext)) {
      search_results.push_back(make_search_result(pair.second, file_name, file_ext));
      added_cpm_names.insert(cpm_name);
    }
  }

  // Scan current directory for files with valid CP/M names
  std::vector<platform::DirEntry> dir_entries = platform::list_directory(scan_dir.c_str());
  if (dir_entries.empty() && search_results.empty()) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
    return;
  }

  for (const auto& entry : dir_entries) {
    // Skip directories and hidden files
    if (entry.name[0] == '.' || entry.is_directory) continue;

    // Convert to CP/M format - skip files with invalid characters
    char file_name[8], file_ext[3];
    if (!unix_to_cpm_83(entry.name.c_str(), file_name, file_ext)) continue;

    // Check if this CP/M name was already added via mapping
    std::string cpm_name = std::string(file_name, 8) + std::string(file_ext, 3);
    if (added_cpm_names.count(cpm_name)) continue;

    if (match_fcb_pattern(pattern_name, pattern_ext, file_name, file_ext)) {
      search_results.push_back(make_search_result(join_path(scan_dir, entry.name),
                                                 file_name, file_ext));
      added_cpm_names.insert(cpm_name);
    }
  }

  if (debug || debug_bdos_funcs.count(17)) {
    fprintf(stderr, "Search First: found %zu files\n", search_results.size());
  }

  // Return first result
  if (search_results.empty()) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Not found
    return;
  }

  // Build directory entry at DMA address
  // CP/M directory entry: 32 bytes
  // Byte 0: user number (0-15)
  // Bytes 1-8: filename (space padded)
  // Bytes 9-11: extension (space padded)
  // Bytes 12-15: extent info (EX, S1, S2, RC)
  // Bytes 16-31: allocation map

  write_dir_entry(search_results[0]);

  search_index = 1;  // Next call returns second result

  // Return 0 (directory code) to indicate entry found in first 32 bytes of DMA
  cpu->set_reg8(0, qkz80::reg_A);
}

// Write one 32-byte CP/M directory entry at the DMA address.
// Byte 0 is the USER number, not the drive: CP/M 2.2 uses 0-15 for a user
// and 0xE5 for an erased entry, and DIR, STAT and PIP all read it that way.
void CPMEmulator::write_dir_entry(const SearchResult& r) {
  qkz80_uint8* mem = cpu->get_mem();

  int64_t file_size = platform::get_file_size(r.path.c_str());
  if (file_size < 0) file_size = 0;
  int records = (file_size + 127) / 128;  // Number of 128-byte records
  int rc = records > 128 ? 128 : records; // Record count in this extent

  memset(&mem[current_dma], 0, 32);
  mem[current_dma + 0] = search_user;  // User number
  memcpy(&mem[current_dma + 1], r.name, 8);
  memcpy(&mem[current_dma + 9], r.ext, 3);
  mem[current_dma + 12] = 0;  // EX (extent)
  mem[current_dma + 13] = 0;  // S1
  mem[current_dma + 14] = 0;  // S2
  mem[current_dma + 15] = rc; // RC (record count)
  // Allocation map bytes 16-31 can be any non-zero value for existing file
  for (int i = 16; i < 32; i++) {
    mem[current_dma + i] = (i - 16 < (records + 7) / 8) ? 0x01 : 0x00;
  }
}

void CPMEmulator::bdos_search_next() {
  if (debug || debug_bdos_funcs.count(18)) {
    fprintf(stderr, "Search Next: index=%zu/%zu\n", search_index, search_results.size());
  }

  if (search_index >= search_results.size()) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // No more files
    return;
  }

  // Search Next needs no drive state: each result already carries its own
  // path and name, so a BDOS 14 between First and Next cannot redirect it.
  write_dir_entry(search_results[search_index]);

  search_index++;

  // Return directory code 0
  cpu->set_reg8(0, qkz80::reg_A);
}

void CPMEmulator::bdos_get_login_vector() {
  // Bitmap of logged-in drives, bit 0 = A: through bit 15 = P:.  A: is
  // always in it; a drive joins when it is configured or selected.
  // Deliberately not 0xFFFF: claiming all sixteen exist sends STAT DSK:
  // walking drives that have no directory behind them.
  cpu->set_reg8(login_vector & 0xFF, qkz80::reg_L);
  cpu->set_reg8((login_vector >> 8) & 0xFF, qkz80::reg_H);
}

void CPMEmulator::bdos_get_allocation_vector() {
  // Return address of allocation vector
  cpu->set_reg8(ALV_ADDR & 0xFF, qkz80::reg_L);
  cpu->set_reg8((ALV_ADDR >> 8) & 0xFF, qkz80::reg_H);
}

void CPMEmulator::bdos_write_protect_disk() {
  // Write protect current disk
  // Just acknowledge - we don't actually enforce this
}

void CPMEmulator::bdos_get_readonly_vector() {
  // Return bitmap of read-only drives
  // For simplicity, say no drives are read-only
  cpu->set_reg8(0x00, qkz80::reg_L);
  cpu->set_reg8(0x00, qkz80::reg_H);
}

void CPMEmulator::bdos_set_file_attributes() {
  // Set file attributes (R/O, System, Archive)
  // Just return success - we don't actually store attributes
  cpu->set_reg8(0, qkz80::reg_A);
}

void CPMEmulator::bdos_get_dpb() {
  // Get Disk Parameter Block address
  cpu->set_reg8(DPB_ADDR & 0xFF, qkz80::reg_L);
  cpu->set_reg8((DPB_ADDR >> 8) & 0xFF, qkz80::reg_H);
}

void CPMEmulator::bdos_reset_drive() {
  // Reset specified drives (bitmap in DE)
  // Just acknowledge - close files would be proper behavior
  for (auto& pair : open_files) {
    if (pair.second.fp) {
      fclose(pair.second.fp);
    }
  }
  open_files.clear();
}

void CPMEmulator::bdos_write_random_zero_fill() {
  // Write random with zero fill (CP/M 3 feature)
  // Just do a regular random write
  bdos_write_random();
}

void CPMEmulator::bios_call(int offset) {
  if (debug || debug_bios_offsets.count(offset)) {
    fprintf(stderr, "BIOS call offset %d\n", offset);
  }

  switch (offset) {
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

  case BIOS_LISTST:
    bios_listst();
    break;

  case BIOS_WBOOT:
    fprintf(stderr, "BIOS WBOOT called - exiting\n");
    exit(0);
    break;

  // BIOS SELDSK - Select Disk, returns HL=DPH address or 0 if invalid
  case BIOS_SELDSK: {
    qkz80_uint8 drive = cpu->get_reg8(qkz80::reg_C);
    if (debug || debug_bios_offsets.count(offset)) {
      fprintf(stderr, "BIOS SELDSK: drive %c\n", 'A' + drive);
    }
    if (drive < 16) {
      // Every drive letter is selectable, because an unconfigured drive is
      // a synonym for the working directory rather than an absent disk.
      // Telling the BIOS otherwise while the BDOS accepts B: would have the
      // same emulator give two different answers about the same drive.
      cpu->set_reg8(DPH_ADDR & 0xFF, qkz80::reg_L);
      cpu->set_reg8((DPH_ADDR >> 8) & 0xFF, qkz80::reg_H);
    } else {
      // Out of range - return 0
      cpu->set_reg8(0x00, qkz80::reg_L);
      cpu->set_reg8(0x00, qkz80::reg_H);
    }
    break;
  }

  // Other disk I/O functions - behavior controlled by bios_disk_mode
  case BIOS_HOME:
  case BIOS_SETTRK:
  case BIOS_SETSEC:
  case BIOS_SETDMA:
  case BIOS_READ:
  case BIOS_WRITE:
  case BIOS_SECTRAN:
    if (bios_disk_mode == 2) {
      // Error mode - exit emulator
      fprintf(stderr, "FATAL: Unimplemented BIOS disk function at offset %d\n", offset);
      fprintf(stderr, "This emulator handles file I/O at the BDOS level.\n");
      fprintf(stderr, "Set CPM_BIOS_DISK=ok or CPM_BIOS_DISK=fail to change this behavior.\n");
      exit(1);
    } else if (bios_disk_mode == 1) {
      // Fail mode - return error to caller
      cpu->set_reg8(0x00, qkz80::reg_A);  // Return failure
      if (debug || debug_bios_offsets.count(offset)) {
        fprintf(stderr, "BIOS disk function at offset %d - returning failure\n", offset);
      }
    } else {
      // OK mode (default) - return success
      cpu->set_reg8(0x00, qkz80::reg_A);  // Return success (0 = OK for BIOS disk)
      if (debug || debug_bios_offsets.count(offset)) {
        fprintf(stderr, "BIOS disk function at offset %d - returning success\n", offset);
      }
    }
    break;

  default:
    if (debug) {
      fprintf(stderr, "Unimplemented BIOS function at offset %d\n", offset);
    }
    break;
  }
}

void CPMEmulator::bios_const() {
  // Console status - return 0xFF if character ready, 0x00 if not
  cpu->set_reg8(platform::stdin_has_data() ? 0xFF : 0x00, qkz80::reg_A);
}

void CPMEmulator::bios_conin() {
  // Console input
  int ch = console_getchar_blocking();
  if (ch == -1 || ch == EOF) ch = 0x1A;
  check_ctrl_c_exit(ch);  // Track ^C for exit, pass through to program
  if (ch == '\n') ch = '\r';  // Convert LF to CR for CP/M
  cpu->set_reg8(ch & 0x7F, qkz80::reg_A);
}

// ADM-3A to ANSI/VT100 terminal translator
// Translates ADM-3A escape sequences (standard CP/M terminal) to ANSI
// sequences understood by modern terminals (xterm, konsole, etc.)
void CPMEmulator::console_output(qkz80_uint8 ch) {
  ch &= 0x7F;

  // ^P mirrors the console to the printer file named by CPM_PRINTER or the
  // 'printer' config directive; with no printer file open there is nowhere
  // to send it, exactly as on a system with no printer attached
  if (printer_echo && printer_file) {
    fputc(ch, printer_file);
    fflush(printer_file);
  }

  switch (term_state) {
  case TERM_ESC:
    switch (ch) {
    case '=':  // ESC = row col - cursor positioning
      term_state = TERM_ESC_EQ;
      return;
    case '*':  // ESC * - clear screen and home
      fputs("\033[2J\033[H", stdout);
      break;
    case 'T':  // ESC T - clear to end of line
      fputs("\033[K", stdout);
      break;
    case 'Y':  // ESC Y - clear to end of screen
      fputs("\033[J", stdout);
      break;
    case ')':  // ESC ) - start reverse video
      fputs("\033[7m", stdout);
      break;
    case '(':  // ESC ( - end reverse video
      fputs("\033[0m", stdout);
      break;
    case 'G':  // ESC G n - Kaypro/Televideo attribute
      term_state = TERM_ESC_G;
      return;
    default:
      // Unknown escape - pass through as ANSI ESC + char
      fprintf(stdout, "\033%c", ch);
      break;
    }
    term_state = TERM_NORMAL;
    fflush(stdout);
    return;

  case TERM_ESC_G:
    // ESC G n - Kaypro/Televideo attribute byte
    // n: '0'=normal, '4'=reverse, '8'=half-intensity, etc.
    switch (ch) {
    case '0':  // Normal
      fputs("\033[0m", stdout);
      break;
    case '4':  // Reverse (inverse)
      fputs("\033[7m", stdout);
      break;
    case '2':  // Half-intensity (dim)
      fputs("\033[2m", stdout);
      break;
    case '1':  // Underline (some variants)
      fputs("\033[4m", stdout);
      break;
    default:  // Unknown attribute - reset
      fputs("\033[0m", stdout);
      break;
    }
    fflush(stdout);
    term_state = TERM_NORMAL;
    return;

  case TERM_ESC_EQ:
    // Got ESC = , this byte is row + 32
    term_saved_row = ch - 32;
    term_state = TERM_ESC_EQ_ROW;
    return;

  case TERM_ESC_EQ_ROW:
    // Got ESC = row, this byte is col + 32
    // Convert to ANSI: ESC [ row ; col H (1-based)
    fprintf(stdout, "\033[%d;%dH", term_saved_row + 1, (ch - 32) + 1);
    fflush(stdout);
    term_state = TERM_NORMAL;
    return;

  case TERM_NORMAL:
    break;
  }

  // Normal character processing
  switch (ch) {
  case 0x1B:  // ESC - start escape sequence
    term_state = TERM_ESC;
    return;
  case 0x1A:  // Ctrl-Z - clear screen and home (ADM-3A)
    fputs("\033[2J\033[H", stdout);
    break;
  case 0x1E:  // Ctrl-^ - home cursor
    fputs("\033[H", stdout);
    break;
  case 0x0B:  // Ctrl-K - cursor up
    fputs("\033[A", stdout);
    break;
  case 0x0C:  // Ctrl-L - cursor right
    fputs("\033[C", stdout);
    break;
  case 0x07:  // BEL
    putchar(0x07);
    break;
  case 0x08:  // Backspace - cursor left
    putchar(0x08);
    break;
  case 0x0D:  // CR
    putchar('\r');
    break;
  case 0x0A:  // LF - cursor down / newline
    putchar('\n');
    break;
  default:
    if (ch >= 0x20)
      putchar(ch);
    break;
  }
  fflush(stdout);
}

void CPMEmulator::bios_conout() {
  // Console output - character is in C register
  qkz80_uint8 ch = cpu->get_reg8(qkz80::reg_C);
  console_output(ch);
}

void CPMEmulator::bios_list() {
  // List (printer) output - character is in C register
  qkz80_uint8 ch = cpu->get_reg8(qkz80::reg_C);
  if (printer_file) {
    fputc(ch & 0x7F, printer_file);
    fflush(printer_file);
  } else {
    // No printer file - output to stdout with prefix
    fprintf(stdout, "[PRINTER] %c", ch & 0x7F);
    fflush(stdout);
  }
}

void CPMEmulator::bios_punch() {
  // Punch (aux output) - character is in C register
  qkz80_uint8 ch = cpu->get_reg8(qkz80::reg_C);
  if (aux_out_file) {
    fputc(ch & 0x7F, aux_out_file);
    fflush(aux_out_file);
  } else {
    // No aux output file - output to stdout with prefix
    fprintf(stdout, "[PUNCH] %c", ch & 0x7F);
    fflush(stdout);
  }
}

void CPMEmulator::bios_reader() {
  // Reader (aux input) - return character in A register
  if (aux_in_file) {
    int ch = fgetc(aux_in_file);
    if (ch == EOF) ch = 0x1A;  // ^Z on EOF
    cpu->set_reg8(ch & 0x7F, qkz80::reg_A);
  } else {
    // No aux input file - return ^Z
    cpu->set_reg8(0x1A, qkz80::reg_A);
  }
}

void CPMEmulator::bios_listst() {
  // List (printer) status - return 0xFF if ready, 0x00 if not
  // Always return ready (0xFF)
  cpu->set_reg8(0xFF, qkz80::reg_A);
}

// Resolve program name with extension
// If name has extension, use as-is
// If no extension, try .com then .COM
static std::string resolve_program_name(const char* name) {
  std::string base(name);

  // Check if already has an extension (contains '.' after last path separator)
  size_t last_sep = base.find_last_of("/\\");
  size_t dot_pos = base.rfind('.');

  // Has extension if dot exists and is after any path separator
  bool has_extension = (dot_pos != std::string::npos &&
                        (last_sep == std::string::npos || dot_pos > last_sep));

  if (has_extension) {
    // Use as-is
    return base;
  }

  // Try .com first
  std::string with_com = base + ".com";
  if (platform::get_file_type(with_com.c_str()) == platform::FileType::Regular) {
    return with_com;
  }

  // Try .COM
  std::string with_COM = base + ".COM";
  if (platform::get_file_type(with_COM.c_str()) == platform::FileType::Regular) {
    return with_COM;
  }

  // Return original (will fail with appropriate error later)
  return base;
}

// Main program
int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s [options] <program.com|config.cfg> [args...]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --8080              Run in 8080 mode\n");
    fprintf(stderr, "  --z80               Run in Z80 mode (default)\n");
    fprintf(stderr, "  --progress[=N]      Enable progress reporting every N million instructions\n");
    fprintf(stderr, "                      (default N=100 if not specified, off by default)\n");
    fprintf(stderr, "  --save-memory=FILE  Save memory to FILE on exit (for MOVCPM/SYSGEN)\n");
    fprintf(stderr, "  --save-range=S-E    Save only range S to E (hex, e.g., DC00-FFFF)\n");
    fprintf(stderr, "  --int-cycles=N      Enable timer interrupt every N cycles (e.g., 50000)\n");
    fprintf(stderr, "  --int-rst=N         RST number for interrupt (0-7, default 7 = RST 38H)\n");
    fprintf(stderr, "  --ctrl-c-exit       Exit after 5 consecutive typed ^C (default)\n");
    fprintf(stderr, "  --no-ctrl-c-exit    Give every ^C to the program (WordStar page-down)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Environment variables:\n");
    fprintf(stderr, "  CPM_PROGRESS=N      Enable progress reporting every N million instructions\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s program.com              # Run CP/M program (default: Z80 mode)\n", argv[0]);
    fprintf(stderr, "  %s --z80 program.com        # Run in Z80 mode\n", argv[0]);
    fprintf(stderr, "  %s --progress program.com   # With progress reporting (every 100M)\n", argv[0]);
    fprintf(stderr, "  %s --progress=50 prog.com   # Report every 50M instructions\n", argv[0]);
    fprintf(stderr, "  %s program.com file.dat     # With file arguments\n", argv[0]);
    fprintf(stderr, "  %s config.cfg               # With config file\n", argv[0]);
    return 1;
  }

  // Parse command line for CPU mode and options
  int arg_offset = 1;
  bool mode_8080 = false;  // Default to Z80
  long long cli_progress_interval = 0;  // 0 = not set via CLI
  unsigned long long int_cycles = 0;  // 0 = interrupts disabled
  int int_rst = 7;  // Default RST 7 (address 0x38)

  while (arg_offset < argc && argv[arg_offset][0] == '-') {
    if (strcmp(argv[arg_offset], "--8080") == 0) {
      mode_8080 = true;
      arg_offset++;
    } else if (strcmp(argv[arg_offset], "--z80") == 0) {
      mode_8080 = false;
      arg_offset++;
    } else if (strncmp(argv[arg_offset], "--progress=", 11) == 0) {
      cli_progress_interval = atoll(argv[arg_offset] + 11) * 1000000LL;
      arg_offset++;
    } else if (strcmp(argv[arg_offset], "--progress") == 0) {
      cli_progress_interval = 100 * 1000000LL;  // Default to 100M if no value specified
      arg_offset++;
    } else if (strncmp(argv[arg_offset], "--save-memory=", 14) == 0) {
      save_memory_file = argv[arg_offset] + 14;
      arg_offset++;
    } else if (strncmp(argv[arg_offset], "--save-range=", 13) == 0) {
      // Parse range like "DC00-FFFF"
      unsigned int start, end;
      if (sscanf(argv[arg_offset] + 13, "%x-%x", &start, &end) == 2) {
        save_memory_start = start;
        save_memory_end = end;
      }
      arg_offset++;
    } else if (strncmp(argv[arg_offset], "--int-cycles=", 13) == 0) {
      int_cycles = strtoull(argv[arg_offset] + 13, nullptr, 10);
      arg_offset++;
    } else if (strncmp(argv[arg_offset], "--int-rst=", 10) == 0) {
      int_rst = atoi(argv[arg_offset] + 10) & 7;  // Clamp to 0-7
      arg_offset++;
    } else if (strcmp(argv[arg_offset], "--no-ctrl-c-exit") == 0) {
      ctrl_c_exit_enabled = false;
      ctrl_c_exit_from_cli = true;  // Outranks a 'ctrl_c_exit' config line
      arg_offset++;
    } else if (strcmp(argv[arg_offset], "--ctrl-c-exit") == 0) {
      ctrl_c_exit_enabled = true;
      ctrl_c_exit_from_cli = true;  // Outranks a 'ctrl_c_exit' config line
      arg_offset++;
    } else {
      break;  // Unknown option, assume it's the program
    }
  }

  if (argc < arg_offset + 1) {
    fprintf(stderr, "Error: No program specified\n");
    fprintf(stderr, "Usage: %s [options] <program.com|config.cfg> [args...]\n", argv[0]);
    return 1;
  }

  const char* arg1 = argv[arg_offset];
  bool is_config = (strstr(arg1, ".cfg") != nullptr);
  std::string program;

  // Create memory and CPU
  qkz80_cpu_mem memory;
  qkz80 cpu(&memory);
  cpu.set_cpu_mode(mode_8080 ? qkz80::MODE_8080 : qkz80::MODE_Z80);
  fprintf(stderr, "CPU mode: %s\n", mode_8080 ? "8080" : "Z80");

  // Set up memory save if requested
  save_memory_cpu = &cpu;
  if (save_memory_file) {
    fprintf(stderr, "Memory will be saved to %s on exit\n", save_memory_file);
    if (save_memory_start || save_memory_end) {
      fprintf(stderr, "  Range: 0x%04X-0x%04X\n", save_memory_start,
              save_memory_end ? save_memory_end : 0xFFFF);
    }
  }

  // Create emulator
  CPMEmulator cpm(&cpu, false);

  // Initialize platform and enable raw mode for console input
  platform::init();
  platform::enable_raw_mode();

  // If config file, load it first
  if (is_config) {
    if (!cpm.load_config_file(arg1)) {
      return 1;
    }

    // Get program name from config
    if (cpm.config_program.empty()) {
      fprintf(stderr, "No 'program' directive in config file\n");
      return 1;
    }
    program = resolve_program_name(cpm.config_program.c_str());
  } else {
    program = resolve_program_name(arg1);
  }

  // Setup CP/M memory
  cpm.setup_memory();

  // Parse command line arguments
  cpm.setup_command_line(argc, argv, arg_offset);

  // Check for config file settings in environment or command line
  const char* printer_file = getenv("CPM_PRINTER");
  const char* aux_in_file = getenv("CPM_AUX_IN");
  const char* aux_out_file = getenv("CPM_AUX_OUT");

  if (printer_file) {
    cpm.set_printer_file(printer_file);
  }
  if (aux_in_file) {
    cpm.set_aux_input_file(aux_in_file);
  }
  if (aux_out_file) {
    cpm.set_aux_output_file(aux_out_file);
  }

  // Parse BIOS disk mode setting
  const char* bios_disk = getenv("CPM_BIOS_DISK");
  if (bios_disk) {
    if (strcmp(bios_disk, "ok") == 0 || strcmp(bios_disk, "OK") == 0) {
      cpm.bios_disk_mode = 0;
      fprintf(stderr, "BIOS disk functions will return success\n");
    } else if (strcmp(bios_disk, "fail") == 0 || strcmp(bios_disk, "FAIL") == 0) {
      cpm.bios_disk_mode = 1;
      fprintf(stderr, "BIOS disk functions will return failure\n");
    } else if (strcmp(bios_disk, "error") == 0 || strcmp(bios_disk, "ERROR") == 0) {
      cpm.bios_disk_mode = 2;
      fprintf(stderr, "BIOS disk functions will cause emulator to exit\n");
    } else {
      fprintf(stderr, "Warning: Invalid CPM_BIOS_DISK value '%s' (use ok, fail, or error)\n", bios_disk);
    }
  }

  // Check for general debug flag
  const char* debug_env = getenv("CPM_DEBUG");
  if (debug_env && (strcmp(debug_env, "1") == 0 || strcmp(debug_env, "true") == 0 || strcmp(debug_env, "yes") == 0)) {
    cpm.set_debug(true);
    fprintf(stderr, "Debug mode enabled\n");
  }

  // Parse selective debug settings
  const char* debug_bdos = getenv("CPM_DEBUG_BDOS");
  if (debug_bdos) {
    std::stringstream ss(debug_bdos);
    std::string item;
    while (std::getline(ss, item, ',')) {
      int func = atoi(item.c_str());
      cpm.debug_bdos_funcs.insert(func);
    }
    if (!cpm.debug_bdos_funcs.empty()) {
      fprintf(stderr, "Debug enabled for BDOS functions:");
      for (int f : cpm.debug_bdos_funcs) {
        fprintf(stderr, " %d", f);
      }
      fprintf(stderr, "\n");
    }
  }

  const char* debug_bios = getenv("CPM_DEBUG_BIOS");
  if (debug_bios) {
    std::stringstream ss(debug_bios);
    std::string item;
    while (std::getline(ss, item, ',')) {
      int offset = atoi(item.c_str());
      cpm.debug_bios_offsets.insert(offset);
    }
    if (!cpm.debug_bios_offsets.empty()) {
      fprintf(stderr, "Debug enabled for BIOS offsets:");
      for (int o : cpm.debug_bios_offsets) {
        fprintf(stderr, " %d", o);
      }
      fprintf(stderr, "\n");
    }
  }

  // If there are additional files on command line, set up mappings
  for (int i = arg_offset + 1; i < argc; i++) {
    if (platform::get_file_type(argv[i]) == platform::FileType::Regular) {
      // Extract basename for CP/M name
      std::string base = platform::basename(argv[i]);

      // Create uppercase CP/M name (full)
      std::string cpm_name;
      for (size_t j = 0; j < base.length(); j++) {
        cpm_name += toupper(base[j]);
      }

      // Add mapping for full name
      cpm.add_file_mapping(cpm_name, argv[i]);

      // Also add mapping for truncated 8.3 version
      // This handles long Unix filenames that get truncated when put in FCB
      std::string cpm_name_83;
      size_t dot_pos = cpm_name.find('.');
      if (dot_pos != std::string::npos) {
        // Take first 8 chars of name
        cpm_name_83 = cpm_name.substr(0, std::min(dot_pos, (size_t)8));
        // Add extension (up to 3 chars)
        cpm_name_83 += cpm_name.substr(dot_pos, 4); // dot + 3 chars
      } else {
        // No extension - just take first 8 chars
        cpm_name_83 = cpm_name.substr(0, std::min(cpm_name.length(), (size_t)8));
      }

      // Add truncated mapping if different from full name
      if (cpm_name_83 != cpm_name) {
        cpm.add_file_mapping(cpm_name_83, argv[i]);
      }
    }
  }

  // Load .COM file at 0x0100
  FILE* fp = fopen(program.c_str(), "rb");
  if (!fp) {
    fprintf(stderr, "Cannot open %s: %s\n", program.c_str(), strerror(errno));
    return 1;
  }

  qkz80_uint8* mem = cpu.get_mem();
  size_t loaded = fread(&mem[TPA_START], 1, BDOS_BASE - TPA_START, fp);
  fclose(fp);

  fprintf(stderr, "Loaded %zu bytes from %s\n", loaded, program.c_str());

  // Set PC to start of TPA
  cpu.regs.PC.set_pair16(TPA_START);

  // Parse progress reporting setting (default: off)
  // CLI option takes precedence over environment variable
  long long progress_interval = 0;  // 0 = disabled
  if (cli_progress_interval > 0) {
    progress_interval = cli_progress_interval;
  } else {
    const char* progress_env = getenv("CPM_PROGRESS");
    if (progress_env) {
      progress_interval = atoll(progress_env) * 1000000LL;  // Convert millions to actual count
    }
  }
  if (progress_interval > 0) {
    fprintf(stderr, "Progress reporting enabled every %lldM instructions\n", progress_interval / 1000000);
  }

  // Interrupt setup
  unsigned long long next_tick_cycles_ = 0;
  if (int_cycles > 0) {
    fprintf(stderr, "Interrupts enabled: RST %d every %llu cycles\n", int_rst, int_cycles);
    next_tick_cycles_ = int_cycles;
    cpu.regs.IFF1 = 1;  // Enable interrupts
    cpu.regs.IFF2 = 1;
    cpu.regs.IM = 1;    // IM 1 mode (RST 38H style)
  }

  // Run
  long long max_instructions = 9000000000LL;  // Safety limit (5B for Zexall/Zexdoc)
  long long instruction_count = 0;
  long long last_report = 0;

  while (true) {
    qkz80_uint16 pc = cpu.regs.PC.get_pair16();

    // Check for CP/M system calls
    if (cpm.handle_pc(pc)) {
      continue;
    }

    // Check for timer interrupt (cycle-based)
    if (int_cycles > 0 && cpu.cycles >= next_tick_cycles_) {
      next_tick_cycles_ = cpu.cycles + int_cycles;
      cpu.request_rst(int_rst);
    }

    // Deliver any pending interrupts
    cpu.check_interrupts();

    // Execute one instruction
    cpu.execute();

    instruction_count++;

    // Progress report (if enabled)
    if (progress_interval > 0 && instruction_count - last_report >= progress_interval) {
      fprintf(stderr, "Progress: %lldM instructions\n", instruction_count / 1000000);
      last_report = instruction_count;
    }

    if (instruction_count >= max_instructions) {
      fprintf(stderr, "Reached instruction limit\n");
      fprintf(stderr, "PC = 0x%04X\n", cpu.regs.PC.get_pair16());
      break;
    }
  }

  return 0;
}
