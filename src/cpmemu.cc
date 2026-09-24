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
#include <memory>
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

// Close every file the guest has open, for the three ways out that are not
// the program finishing: five ^C, the give-up at end of input, and the
// instruction-limit watchdog.  The text writer holds a record's closing CR
// until it knows whether an LF follows, and those exits dropped it; BDOS 0,
// WBOOT and a jump to 0000h have closed every file first since the branch
// that added the hold.  Set by main() once the emulator exists.
static void (*close_guest_files)() = nullptr;

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

// The three ways a CP/M program finishes: BDOS 0 System Reset, BIOS WBOOT, and
// a jump to 0x0000.  Each used to exit(0) from where it was noticed and only
// the jump saved the memory image, so --save-memory wrote nothing at all for a
// program that ended through BDOS 0 - which is how most CP/M programs end, and
// includes MOVCPM and SYSGEN, the two this flag exists for.  Measured before
// the fix: a three-byte `jp 0` guest wrote the file, a five-byte `ld c,0 / jp
// 5` guest wrote none and said nothing about it.  One exit for all three now.
// The other three writers are elsewhere and stay there, because none of them is
// a program finishing: the five-^C exit, the give-up at end of input, and the
// instruction-limit watchdog at the bottom of main().
//
// disable_raw_mode() is not called here because atexit() covers it:
// enable_raw_mode() registers the handler inside the same block that arms raw
// mode (os/*/platform.cc), so a raw terminal always has one and a terminal that
// was never made raw has nothing to put back.  The ^C and end-of-input paths
// call it directly as belt and braces, not because they are a special case.
static void program_exit(const char* why) {
  fprintf(stderr, "%s\n", why);
  do_save_memory();
  exit(0);
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
      if (close_guest_files) close_guest_files();
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
// Consecutive blocking console reads that came back at end of input.
// Redirected input runs out, and a program that keeps reading past that point
// would otherwise never stop - BDOS 1 hands back CR each time, so the loop is
// not even quiet about it.  Reset by any real keystroke or byte.
static int consecutive_console_eof = 0;
static const int CONSOLE_EOF_LIMIT = 1024;

// Count one read that found end of input, and give up once the guest has asked
// often enough that nothing is going to change.  Shared, because BDOS 6 hits the
// same wall as the blocking reads: it called platform::console_getchar()
// directly and turned the -1 into 0, which is also its value for "nothing
// waiting yet", so the counter neither incremented nor reset and a polling
// guest spun with no diagnostic and had to be killed.  Measured at 200000 reads
// against a pty whose master had closed, while the same guest on the same stdin
// exited cleanly in 0.05s through BDOS 1.
static void note_console_eof() {
  if (++consecutive_console_eof >= CONSOLE_EOF_LIMIT) {
    // Nothing is going to change: stdin is finished and the guest is still
    // asking.  Leaving is better than spinning, and it is what a warm boot
    // amounts to here anyway.
    fprintf(stderr, "\n[Exiting: %d console reads past end of input]\n",
            consecutive_console_eof);
    if (close_guest_files) close_guest_files();
    do_save_memory();
    platform::disable_raw_mode();
    exit(0);
  }
}

static int console_getchar_blocking() {
  int ch = platform::console_getchar();
  while (ch == 0 && platform::console_last_char_synthesized()) {
    ch = platform::console_getchar();
  }
  if (ch == -1 || ch == EOF) {
    note_console_eof();
  } else {
    consecutive_console_eof = 0;
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

// FCB byte offsets for the fields the file calls position with.
enum {
  FCB_EX = 12,  // logical extent, 0-31: 128 records each
  FCB_S1 = 13,
  FCB_S2 = 14,  // module: 32 extents each.  Bit 7 is the BDOS's own flag.
  FCB_RC = 15,
  FCB_CR = 32,  // record within the extent, 0-127, or 128 after its last
  FCB_R0 = 33, FCB_R1 = 34, FCB_R2 = 35
};

// Records an FCB can address.  S2:EX:CR is 6 + 5 + 7 bits, 2^18 records or
// 32 MB, which is CP/M 3's and MP/M II's limit, and a random record at or past
// it is error 6, because no FCB could say where the sequential calls after it
// should go.  2.2 is stricter: its POSITION answers 6 for any record of 65536
// or more (R2 not zero), 8 MB.  This does not, on purpose - the MP/M II and
// CP/M 3 tools this emulator runs, and host files over 8 MB, can use the rest,
// and 4.9.0 read and wrote any record at all.  docs/CPM_SUPPORT.md says so.
static const uint32_t FCB_MAX_RECORDS = 1u << 18;

// The record the extent an FCB is in starts at: the numbering BDOS 33-36 use,
// so a sequential call and a random one agree on what record N is.
static uint32_t fcb_extent_base(const qkz80_uint8* f) {
  return (static_cast<uint32_t>(f[FCB_S2] & 0x3F) << 12) |
         (static_cast<uint32_t>(f[FCB_EX] & 0x1F) << 7);
}

// Step an FCB to record 0 of the next logical extent, EX carrying into S2 the
// way 2.2's GETNEXT does.  False, and the FCB untouched, at the last extent.
static bool fcb_next_extent(qkz80_uint8* f) {
  if (fcb_extent_base(f) + 128 >= FCB_MAX_RECORDS) return false;
  qkz80_uint8 ex = static_cast<qkz80_uint8>((f[FCB_EX] + 1) & 0x1F);
  f[FCB_EX] = ex;
  if (ex == 0) f[FCB_S2] = static_cast<qkz80_uint8>((f[FCB_S2] & 0x3F) + 1);
  f[FCB_CR] = 0;
  return true;
}

// Point an FCB at an absolute record, as a random read or write leaves it:
// CR is the record, and EX and S2 are rewritten only when the extent differs,
// which is 2.2's POSITION (it compares S2 without the flag bit).
static void fcb_set_record(qkz80_uint8* f, uint32_t record) {
  qkz80_uint8 ex = static_cast<qkz80_uint8>((record >> 7) & 0x1F);
  qkz80_uint8 s2 = static_cast<qkz80_uint8>((record >> 12) & 0x3F);
  if (f[FCB_EX] != ex || ((f[FCB_S2] ^ s2) & 0x3F) != 0) {
    f[FCB_EX] = ex;
    f[FCB_S2] = s2;
  }
  f[FCB_CR] = static_cast<qkz80_uint8>(record & 0x7F);
}

// Open file tracking
//
// A CP/M file has no position of its own: every read and write names its
// record in the FCB, and the guest may change EX, S2 and CR between any two
// calls.
//
// A binary file is 128 host bytes to a record and is sought to record * 128
// on every call.  So is a text file without EOL conversion, whose sequential
// reads stop at a ^Z.
//
// A text file with conversion is not: LF becomes CR LF and a ^Z ends it, so
// the host bytes of record n depend on every record before it, and rewriting
// one record can make the host text shorter or longer than what it replaces.
// It is held as a TextImage - the whole file as a CP/M disk would hold it,
// records of converted text padded with ^Z - and every read and write is a
// read or write of that image, which is then written back to the host as
// text.  See TextImage.
struct TextImage;

struct OpenFile {
  FILE* fp;             // binary, and text without conversion
  std::shared_ptr<TextImage> img;  // text with conversion
  std::string unix_path;
  std::string cpm_name;
  FileMode mode;
  bool eol_convert;

  OpenFile() : fp(nullptr), mode(MODE_BINARY), eol_convert(false) {}
  bool is_open() const { return fp != nullptr || img != nullptr; }
};

// A converted text file, as CP/M holds it.
//
// `cpm` is the host text converted - an LF with no CR before it becomes CR LF,
// and the text ends at a ^Z - padded with ^Z to a whole record, and then
// whatever records the guest writes, 128 bytes each, as a disk would take
// them: a record past the end extends it, the gap as NULs.  Reading and
// writing are indexing.
//
// The host file is that image as text: the bytes up to its first ^Z, in the
// host file's own line-end style - CR LF kept as it is if the file's lines
// ended CR LF when it was opened, CR LF to LF otherwise (a lone CR or LF is
// written as it is) - and a ^Z after them if the file had one, padded to a
// record if it was a whole number of records.  Only the text from the line
// the first change is in to the end is written back; the lines before it
// keep their host bytes.  `lines` maps where each line starts in the image
// to where it starts in the host file, from the load and from each write
// back, so that point is found without reading the host file again.
//
// The write back is done at once when it is cheap - the change is in the
// file's last line, and that line and what follows it are under 64 KB, as an
// append or a new file being written always is - and otherwise when an FCB
// on the file closes it, at a disk reset, at the end of the run, at BDOS 48,
// and before a search, a rename or a file size, which look at host files.
// Every FCB open on one host path shares one image, so another FCB reading
// the file sees a change whether or not it has been written back; a make or
// a delete of the path cuts the image loose from the host file.
//
// This replaced a converting stream.  Its text was only ever right read or
// written in order: rewriting a record in place wrote shorter or longer host
// text over the old and left stale bytes after it, or ran into the next
// record's - the append idiom, read to the end, back up a record and write
// it again from its ^Z, left the old last lines after the appended text in a
// CR LF file - and a random read or write was raw host bytes at record * 128.
struct TextImage {
  std::string path;
  std::vector<uint8_t> cpm;
  bool crlf;         // host lines end CR LF: written back as they are
  bool eof_mark;     // host text ended at a ^Z: written back with one
  bool eof_pad;      // ... and a ^Z-padded last record
  bool dirty;        // cpm has changed since the host was written
  bool detached;     // deleted or made over: nothing is written back
  size_t dirty_from; // the first CP/M byte changed, SIZE_MAX when none
  uint64_t host_size;
  std::vector<std::pair<size_t, uint64_t> > lines;  // (image, host) line starts
  size_t zpos;       // the first ^Z in cpm, or SIZE_MAX: kept, not searched for

  TextImage() : crlf(false), eof_mark(false), eof_pad(false), dirty(false),
    detached(false), dirty_from(SIZE_MAX), host_size(0), zpos(SIZE_MAX) {}
  size_t records() const { return cpm.size() / 128; }
  size_t text_end() const { return zpos < cpm.size() ? zpos : cpm.size(); }
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
    uint32_t extent;   // logical extent: S2 * 32 + EX
    uint32_t records;  // the whole file's, from its host size
  };
  std::vector<SearchResult> search_results;  // List of matching files
  static SearchResult make_search_result(const std::string& path,
                                         const char name[8], const char ext[3]) {
    SearchResult r;
    r.path = path;
    memcpy(r.name, name, 8);
    memcpy(r.ext, ext, 3);
    r.extent = 0;
    r.records = 0;
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

  // Every open file closed, for an exit that is not the program's own.
  void close_files_at_exit() { close_all_files(); }

private:
  // File I/O helpers
  FileMode detect_file_mode(const std::string& filename, const std::string& unix_path);
  static FileMode extension_mode(const std::string& filename);
  // What bytes_look_like_text finds: text, not text, or text by every test
  // but the last - 8-bit, not UTF-8, with lines that do not all end in a
  // bare LF - which is text to a close or a rename only if nothing is lost.
  enum TextKind { NOT_TEXT, TEXT, TEXT_8BIT };
  static TextKind text_kind(const uint8_t* p, size_t n, uint64_t size);
  static bool bytes_look_like_text(const uint8_t* p, size_t n, uint64_t size) {
    return text_kind(p, n, size) == TEXT;
  }
  static bool file_looks_like_text(const std::string& unix_path);
  std::string find_unix_file_ex(const std::string& cpm_name, FileMode* mode_out, bool* eol_out,
                                qkz80_uint8 fcb_drive);
  // Substitute the text a CP/M pattern matched into a '*' on the host side,
  // so `*.BAS = /dir/*.bas` reaches /dir/<stem>.bas rather than a file
  // literally called '*.bas'.
  std::string expand_unix_pattern(const std::string& cpm_pattern,
                                  const std::string& unix_pattern,
                                  const std::string& normalized) const;
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

  void pad_to_128(uint8_t* buffer, size_t actual_size);

  // Record I/O.  `record` is absolute, from the FCB; see OpenFile.  A read
  // returns the bytes it got, 0 at the end of the file, and the caller pads
  // a short record with ^Z.  `sequential` stops a text file without
  // conversion at its ^Z; a random read of one is raw, as it always was.
  size_t read_record(OpenFile& of, uint32_t record, uint8_t* buffer, bool sequential);
  bool write_record(OpenFile& of, uint32_t record, const uint8_t* buffer);

  // Converted text files: see TextImage.  One image per host path, shared.
  std::map<std::string, std::shared_ptr<TextImage> > text_images;
  std::shared_ptr<TextImage> text_image(const std::string& path, bool empty);
  static bool load_text_image(TextImage& img, const std::string& path);
  bool write_back(TextImage& img);
  void flush_text_images();
  void forget_text_image(const std::string& path);
  static void image_write(TextImage& img, uint32_t record, const uint8_t* data);
  // The records a text file with conversion holds, from its image if one is
  // open and by converting it if not.
  uint32_t text_record_count(const std::string& path);
  void close_open_file(OpenFile& of);
  // Every open file, closed: on a disk reset, and when the program ends, so
  // that a text file's changes still only in its image reach the host.
  void close_all_files();
  // Open the host file an FCB names into open_files, without touching the
  // FCB.  Returns false, having set A = 0xFF, when it cannot.
  bool open_fcb_file(qkz80_uint16 fcb_addr, int func);
  // The open file an FCB names, opened from its name if this FCB address has
  // none.  nullptr, with A = 0xFF, when there is no such file.
  OpenFile* fcb_open_file(qkz80_uint16 fcb_addr, int func);
  // Copy to and from the DMA buffer, wrapping at 64K as CP/M addresses do.
  void dma_put(const uint8_t* src, size_t n);
  void dma_get(uint8_t* dst, size_t n);
  uint32_t cpm_record_count(const OpenFile& of);
  // How a file BDOS 22 makes is written.  MAKE_AS_MODE: as *mode says, which
  // a mode rule, default_mode or the binary list decided.  The other two are
  // auto with nothing to go on yet, and are written as they come, binary:
  // MAKE_BY_CONTENT, a name on the text list, becomes host text when it is
  // closed if what was written is text; MAKE_GUESSED, a name on neither list,
  // waits for a rename to a name that says, as PIP's X.$$$ does.
  enum MakeKind { MAKE_AS_MODE, MAKE_BY_CONTENT, MAKE_GUESSED };
  void make_file_mode(const std::string& filename, FileMode* mode, bool* eol,
                      MakeKind* kind = nullptr);
  // Host paths of files BDOS 22 made this run under MAKE_GUESSED and under
  // MAKE_BY_CONTENT, still as they were written.  See bdos_rename_file and
  // settle_made_file.
  std::set<std::string> made_guessed;
  std::set<std::string> made_by_content;
  // ... and of those, the ones written at random (BDOS 34 or 40) as well as
  // in sequence: a random file has to read back record for record.
  std::set<std::string> made_random;
  // What convert_made_file_to_text asks of a file besides being text.
  // AS_WRITER: nothing - CR LF to LF, as the text writer always converted.
  // READS_BACK_TEXT: its text reads back as it is, every LF after a CR.
  // READS_BACK_RECORDS: and every record reads back as a text open of the
  // file as written would read it - the text does not end in NULs.
  enum MadeCheck { AS_WRITER, READS_BACK_TEXT, READS_BACK_RECORDS };
  bool convert_made_file_to_text(const std::string& path, const char* who, bool trace,
                                 MadeCheck check = READS_BACK_TEXT);
  void renamed_made_file(const std::string& old_path, const std::string& new_name,
                         const std::string& new_path);
  // A MAKE_BY_CONTENT file whose last stream has closed: host text now, if
  // it is text.
  void settle_made_file(const std::string& path, bool trace);

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
  // Take R0-R2 for BDOS 33/34/40 and point the FCB at the record.  False,
  // with A set, when no FCB can name it.
  bool random_position(qkz80_uint16 fcb_addr, uint32_t* record);

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

  // The default FCBs as the CCP leaves them when the command line names no
  // file: drive 0, a blank name and type in both, EX, S1, S2 and CR zero.
  // 2.2's CCP runs CONVERT for the first and the second name whether or not
  // either is there, and CONVERT blank-fills what it does not find.  These
  // were zeros, and a program that tests for "no second name" the way DRI's
  // tools do - ED's IF (FCB(1) = ' ') OR (FCB(17) <> ' ') THEN CALL FERR -
  // saw a second file named with NULs: ED stopped at its first line with
  // DISK OR DIRECTORY FULL, on 4.9.0 and every build before it.
  memset(&mem[DEFAULT_FCB], 0, 36);
  memset(&mem[DEFAULT_FCB + 1], ' ', 11);
  memset(&mem[DEFAULT_FCB2 + 1], ' ', 11);

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

// Whether bytes are text, as a name on the text list has to be to open as text
// under auto.  `n` bytes from the top of a file of `size`, which may be a
// prefix of it.  Text ends at the first ^Z or NUL, and it is binary if
//   - the ^Z or NUL has more than a record after it: text ends in its last
//     record.  A random file MBASIC wrote with PUT, the last records it put
//     all NULs, is not text followed by padding;
//   - a NUL has anything after it but NULs and ^Zs, which would be a last
//     record padded with NUL rather than ^Z, or has nothing before it;
//   - there is a control character before the end other than BS, TAB, LF,
//     VT, FF, CR and ESC;
//   - what is before the end is not UTF-8 (ASCII is) - unless its lines end
//     in bare LFs and none in CR LF, which is a host text file with a Latin-1
//     or 8-bit character in it and needs the converter to be read at all.
// Measured on 3,796 distinct files from the RomWBW, MP/M II and CP/M tool
// disks.  Every binary one fails within its first record: a REL file or a
// REL library opens with a link item, 84h or 85h, which is no UTF-8 lead byte,
// and has a NUL or a control byte within a few more; a tokenized MBASIC
// program opens with FFh and has a NUL; a WordStar document has 8Dh soft
// returns and CR LF hard ones; Aztec C's and ISIS's libraries have a NUL in
// their first four bytes.  DRI's macro libraries - DISKDEF.LIB, Z80.LIB,
// SEQIO.LIB and the rest on the MP/M II disks - and M80's XX80.LIB are text.
// So are all but 5 of the 1,802 .ASM, .MAC, .Z80, .PRN and .LST files.
// Four of the 5 have no bare LF, which binary reads exactly as text would,
// the guest stopping at the ^Z itself; the fifth is a listing with a DC1.
//
// TEXT_8BIT is what fails the last test only: CP/M text with a Latin-1 or
// code page 437 byte in it, which is CR LF text as CP/M writes it.  It opens
// binary, which reads it as it is, but a file made or renamed under a text
// name is host text if its conversion loses nothing - see
// convert_made_file_to_text.
CPMEmulator::TextKind CPMEmulator::text_kind(const uint8_t* p, size_t n, uint64_t size) {
  size_t end = n;
  for (size_t i = 0; i < n; i++) {
    if (p[i] == CPM_EOF || p[i] == 0) { end = i; break; }
  }
  if (end < n && size > end && size - end > 128) return NOT_TEXT;  // not in the last record
  if (end < n && p[end] == 0) {
    if (end == 0) return NOT_TEXT;  // NULs and nothing else: no sign of text
    if (n < size) return NOT_TEXT;  // padding that goes on past what was read
    for (size_t i = end; i < n; i++) {
      if (p[i] != 0 && p[i] != CPM_EOF) return NOT_TEXT;
    }
  }
  bool utf8 = true;
  size_t crlf = 0, bare_lf = 0;
  for (size_t i = 0; i < end;) {
    uint8_t c = p[i];
    if (c < 0x80) {
      if (c < 0x20 && !(c >= 0x08 && c <= 0x0D) && c != 0x1B) return NOT_TEXT;
      if (c == '\n') (i > 0 && p[i - 1] == '\r' ? crlf : bare_lf)++;
      i++;
      continue;
    }
    size_t len = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3
               : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
    bool ok = len != 0;
    for (size_t k = 1; ok && k < len; k++) {
      if (i + k >= end) {
        ok = end == n && n < size;  // cut by the window, not by the end
        break;
      }
      ok = (p[i + k] & 0xC0) == 0x80;
    }
    if (!ok) {
      utf8 = false;
      i++;
    } else {
      i += len;
    }
  }
  return utf8 || (bare_lf > 0 && crlf == 0) ? TEXT : TEXT_8BIT;
}

// The first 64 KB of a host file, judged by bytes_look_like_text.  A file
// that cannot be read is text, which is what its name says.
bool CPMEmulator::file_looks_like_text(const std::string& unix_path) {
  FILE* fp = fopen(unix_path.c_str(), "rb");
  if (!fp) return true;
  std::vector<uint8_t> head(65536);
  size_t n = fread(head.data(), 1, head.size(), fp);
  fclose(fp);
  int64_t size = platform::get_file_size(unix_path.c_str());
  return bytes_look_like_text(head.data(), n, size < 0 ? n : static_cast<uint64_t>(size));
}

// The mode auto gives a file that exists: binary unless its name is on the
// text list, and then text only if what it holds is text.  The name alone
// decided until now, and every name on the text list also names binary
// files: .LIB is a macro library to MAC and RMAC and a REL library to LINK
// and L80, and DRI's LINK read XDOS2.LIB, made by LIB, through the text
// converter and stopped with DISK READ ERROR; .BAS is an ASCII program or a
// tokenized one, as MBASIC's SAVE chooses; .DOC and .TXT are what WordStar
// writes in document mode, soft returns and all.  Binary loses nothing - the
// guest reads what is there - so it is what a name that could be either gets
// when its bytes are not text.
FileMode CPMEmulator::detect_file_mode(const std::string& filename, const std::string& unix_path) {
  if (extension_mode(filename) != MODE_TEXT) return MODE_BINARY;
  return file_looks_like_text(unix_path) ? MODE_TEXT : MODE_BINARY;
}

// What the extension alone says: MODE_TEXT or MODE_BINARY for the two lists
// below, MODE_AUTO for a name on neither - one auto has to guess for.
FileMode CPMEmulator::extension_mode(const std::string& filename) {
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

  return MODE_AUTO;
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

// Replace a '*' on the host side of a mapping with the text the CP/M pattern
// matched.  `*.BAS = /dir/*.bas` looking up PRINTSEP.BAS yields
// /dir/printsep.bas.  For `*` and `*.*` the whole name stands in, extension
// included, since that is what those patterns match.  A host path with no
// '*' is returned untouched, so every mapping written before this existed
// resolves exactly as it did.
std::string CPMEmulator::expand_unix_pattern(const std::string& cpm_pattern,
                                             const std::string& unix_pattern,
                                             const std::string& normalized) const {
  size_t star = unix_pattern.find('*');
  if (star == std::string::npos) return unix_pattern;

  std::string stand_in = normalized;
  size_t pat_dot = cpm_pattern.find('.');
  if (pat_dot == 1 && cpm_pattern[0] == '*' && cpm_pattern != "*.*") {
    // '*.EXT' matched the stem only, so the extension comes from the host
    // side of the mapping rather than from the name being looked up.
    size_t dot = normalized.find('.');
    if (dot != std::string::npos) stand_in = normalized.substr(0, dot);
  }
  for (char& c : stand_in) c = tolower(c);

  return unix_pattern.substr(0, star) + stand_in + unix_pattern.substr(star + 1);
}

std::string CPMEmulator::find_unix_file_ex(const std::string& cpm_name, FileMode* mode_out,
                                           bool* eol_out, qkz80_uint8 fcb_drive) {
  std::string normalized = normalize_cpm_filename(cpm_name);

  // A mapping with no host path is a mode rule rather than a location: it
  // says how a name should be treated wherever it is eventually found.
  // Collected first, applied to whatever path the steps below settle on.
  bool have_mode_rule = false;
  FileMode rule_mode = MODE_AUTO;
  bool rule_eol = default_eol_convert;
  for (const auto& mapping : file_mappings) {
    if (mapping.unix_pattern.empty() && match_pattern(mapping.cpm_pattern, normalized)) {
      have_mode_rule = true;
      rule_mode = mapping.mode;
      rule_eol = mapping.eol_convert;
    }
  }

  // Check new file mappings with patterns
  for (const auto& mapping : file_mappings) {
    if (mapping.unix_pattern.empty()) continue;  // mode rule, handled above
    if (match_pattern(mapping.cpm_pattern, normalized)) {
      std::string target = expand_unix_pattern(mapping.cpm_pattern, mapping.unix_pattern,
                                               normalized);
      if (platform::get_file_type(target.c_str()) != platform::FileType::NotFound) {
        *mode_out = mapping.mode;
        *eol_out = mapping.eol_convert;

        // Auto-detect if needed
        if (*mode_out == MODE_AUTO) {
          *mode_out = detect_file_mode(normalized, target);
        }

        return target;
      }
    }
  }

  // Check legacy file map: a file named on the command line, or one a guest
  // renamed.  A mode rule for the name applies here as it does below - it
  // did not, so a file renamed to X.TXT under `*.TXT = binary` opened as text
  // while every other X.TXT opened binary.
  auto it = file_map.find(normalized);
  if (it != file_map.end()) {
    *mode_out = have_mode_rule ? rule_mode : detect_file_mode(normalized, it->second);
    *eol_out = have_mode_rule ? rule_eol : default_eol_convert;
    if (*mode_out == MODE_AUTO) *mode_out = detect_file_mode(normalized, it->second);
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
        *mode_out = have_mode_rule ? rule_mode : detect_file_mode(normalized, candidates[i]);
        *eol_out = have_mode_rule ? rule_eol : default_eol_convert;
        if (*mode_out == MODE_AUTO) *mode_out = detect_file_mode(normalized, candidates[i]);
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
    *mode_out = have_mode_rule ? rule_mode : detect_file_mode(normalized, lowercase);
    *eol_out = have_mode_rule ? rule_eol : default_eol_convert;
    if (*mode_out == MODE_AUTO) *mode_out = detect_file_mode(normalized, lowercase);
    return lowercase;
  }

  // Try as-is
  if (platform::get_file_type(normalized.c_str()) != platform::FileType::NotFound) {
    *mode_out = have_mode_rule ? rule_mode : detect_file_mode(normalized, normalized);
    *eol_out = have_mode_rule ? rule_eol : default_eol_convert;
    if (*mode_out == MODE_AUTO) *mode_out = detect_file_mode(normalized, normalized);
    return normalized;
  }

  return "";  // Not found
}

void CPMEmulator::pad_to_128(uint8_t* buffer, size_t actual_size) {
  if (actual_size < 128) {
    // Pad with ^Z for CP/M compatibility
    memset(buffer + actual_size, CPM_EOF, 128 - actual_size);
  }
}

// Read a host text file into `img`: LF with no CR before it to CR LF, the
// text ending at a ^Z, padded with ^Z to a record.  Records its line starts
// and the style it will be written back in.  False if it cannot be read.
bool CPMEmulator::load_text_image(TextImage& img, const std::string& path) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) return false;
  std::vector<uint8_t> host;
  std::vector<uint8_t> chunk(65536);
  size_t n;
  while ((n = fread(chunk.data(), 1, chunk.size(), fp)) > 0) {
    host.insert(host.end(), chunk.begin(), chunk.begin() + n);
  }
  fclose(fp);

  img.cpm.clear();
  img.cpm.reserve(host.size() + host.size() / 16 + 128);
  img.lines.assign(1, std::make_pair(static_cast<size_t>(0), static_cast<uint64_t>(0)));
  img.crlf = false;
  img.eof_mark = false;
  bool seen_lf = false, prev_cr = false;
  for (size_t h = 0; h < host.size(); h++) {
    uint8_t c = host[h];
    if (c == CPM_EOF) {
      img.eof_mark = true;
      break;
    }
    if (c == '\n') {
      if (!seen_lf) img.crlf = prev_cr;  // the first line end sets the style
      seen_lf = true;
      if (!prev_cr) img.cpm.push_back('\r');
      img.cpm.push_back('\n');
      img.lines.push_back(std::make_pair(img.cpm.size(), static_cast<uint64_t>(h + 1)));
    } else {
      img.cpm.push_back(c);
    }
    prev_cr = (c == '\r');
  }
  img.eof_pad = img.eof_mark && host.size() % 128 == 0;
  img.host_size = host.size();
  img.zpos = img.cpm.size() % 128 ? img.cpm.size() : SIZE_MAX;
  while (img.cpm.size() % 128) img.cpm.push_back(CPM_EOF);
  img.dirty = false;
  img.dirty_from = SIZE_MAX;
  return true;
}

// The image of the text file at `path`, shared by every FCB open on it:
// loaded from the host file, or empty for one a make has just created.
std::shared_ptr<TextImage> CPMEmulator::text_image(const std::string& path, bool empty) {
  auto it = text_images.find(path);
  if (it != text_images.end()) {
    if (!empty) return it->second;
    forget_text_image(path);  // made over: that image is no longer this file
  }
  std::shared_ptr<TextImage> img = std::make_shared<TextImage>();
  img->path = path;
  if (empty) {
    img->lines.assign(1, std::make_pair(static_cast<size_t>(0), static_cast<uint64_t>(0)));
  } else if (!load_text_image(*img, path)) {
    return nullptr;
  }
  text_images[path] = img;
  return img;
}

// The host file at `path` is gone or has been made over.  An FCB still open
// on it keeps reading the image it had, as one on a deleted CP/M file keeps
// reading its blocks, and nothing it writes reaches the host.
void CPMEmulator::forget_text_image(const std::string& path) {
  auto it = text_images.find(path);
  if (it == text_images.end()) return;
  it->second->detached = true;
  text_images.erase(it);
}

// Record `record` of the image becomes `data`, as a disk takes a record: the
// image grows to hold it, a gap before it as NULs.  Marks where the first
// changed byte is, and keeps zpos.
void CPMEmulator::image_write(TextImage& img, uint32_t record, const uint8_t* data) {
  size_t at = static_cast<size_t>(record) * 128;
  if (at + 128 > img.cpm.size()) {
    img.dirty = true;
    img.dirty_from = std::min(img.dirty_from, img.cpm.size());
    img.cpm.resize(at + 128, 0);
  }
  size_t k = 0;
  while (k < 128 && img.cpm[at + k] == data[k]) k++;
  if (k == 128) return;
  img.dirty = true;
  img.dirty_from = std::min(img.dirty_from, at + k);
  memcpy(&img.cpm[at], data, 128);

  if (img.zpos != SIZE_MAX && img.zpos < at) return;  // a ^Z before it is still first
  for (size_t j = 0; j < 128; j++) {
    if (data[j] == CPM_EOF) {
      img.zpos = at + j;
      return;
    }
  }
  if (img.zpos == SIZE_MAX || img.zpos >= at + 128) return;  // the first is after it
  // The first ^Z was in this record and has been written over: the next one.
  img.zpos = SIZE_MAX;
  for (size_t j = at + 128; j < img.cpm.size(); j++) {
    if (img.cpm[j] == CPM_EOF) {
      img.zpos = j;
      break;
    }
  }
}

// Write what has changed in the image back to the host file as text: from
// the start of the line the first change is in to the end of the text, the
// lines before it left as they are.  See TextImage for the form.
bool CPMEmulator::write_back(TextImage& img) {
  if (!img.dirty || img.detached) return true;
  size_t end = img.text_end();
  if (img.dirty_from > end) {
    // Only past the text's ^Z, which no text reader sees and the host
    // file does not hold.
    img.dirty = false;
    img.dirty_from = SIZE_MAX;
    return true;
  }
  auto it = std::upper_bound(img.lines.begin(), img.lines.end(), img.dirty_from,
                             [](size_t v, const std::pair<size_t, uint64_t>& e) {
                               return v < e.first;
                             });
  --it;  // lines[0] is (0, 0)
  size_t p = it->first;
  uint64_t h = it->second;
  img.lines.erase(it + 1, img.lines.end());

  std::vector<uint8_t> out;
  out.reserve(end - p + 128);
  for (size_t i = p; i < end; i++) {
    uint8_t c = img.cpm[i];
    if (!img.crlf && c == '\r' && i + 1 < end && img.cpm[i + 1] == '\n') continue;
    out.push_back(c);
    if (c == '\n') img.lines.push_back(std::make_pair(i + 1, h + out.size()));
  }
  if (img.eof_mark) {
    out.push_back(CPM_EOF);
    while (img.eof_pad && (h + out.size()) % 128) out.push_back(CPM_EOF);
  }
  uint64_t size = h + out.size();

  bool ok = false;
  if (size >= img.host_size) {
    FILE* fp = fopen(img.path.c_str(), "r+b");
    if (fp) {
      ok = fseek(fp, static_cast<long>(h), SEEK_SET) == 0 &&
           (out.empty() || fwrite(out.data(), 1, out.size(), fp) == out.size());
      ok = (fclose(fp) == 0) && ok;
    }
  } else {
    // Shorter than it was, and C has no truncate: the whole file, the lines
    // kept read back from it first.
    std::vector<uint8_t> head(static_cast<size_t>(h));
    FILE* fp = fopen(img.path.c_str(), "rb");
    if (fp) {
      ok = head.empty() || fread(head.data(), 1, head.size(), fp) == head.size();
      fclose(fp);
    }
    if (ok && (fp = fopen(img.path.c_str(), "wb")) != nullptr) {
      ok = (head.empty() || fwrite(head.data(), 1, head.size(), fp) == head.size()) &&
           (out.empty() || fwrite(out.data(), 1, out.size(), fp) == out.size());
      ok = (fclose(fp) == 0) && ok;
    } else {
      ok = false;
    }
  }
  if (!ok) {
    // Where the lines start is no longer known past p: the next write back
    // starts there.
    img.dirty_from = p;
    return false;
  }
  img.host_size = size;
  img.dirty = false;
  img.dirty_from = SIZE_MAX;
  return true;
}

void CPMEmulator::flush_text_images() {
  for (auto& pair : text_images) write_back(*pair.second);
}

// The records a text file with conversion holds.  Its image's, if an FCB has
// it open; otherwise counted by converting it, without keeping anything.
uint32_t CPMEmulator::text_record_count(const std::string& path) {
  auto it = text_images.find(path);
  if (it != text_images.end()) return static_cast<uint32_t>(it->second->records());
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) return 0;
  uint64_t bytes = 0;
  bool cr = false;
  int ch;
  while ((ch = fgetc(fp)) != EOF && ch != CPM_EOF) {
    if (ch == '\n' && !cr) bytes++;
    bytes++;
    cr = (ch == '\r');
  }
  fclose(fp);
  uint64_t records = (bytes + 127) / 128;
  return records > 0xFFFFFF ? 0xFFFFFF : static_cast<uint32_t>(records);
}

void CPMEmulator::close_open_file(OpenFile& of) {
  if (of.img) {
    write_back(*of.img);
    std::string path = of.img->path;
    of.img.reset();
    auto it = text_images.find(path);
    if (it != text_images.end() && it->second.use_count() == 1) text_images.erase(it);
  }
  if (of.fp) {
    fclose(of.fp);
    of.fp = nullptr;
  }
}

void CPMEmulator::close_all_files() {
  for (auto& pair : open_files) {
    close_open_file(pair.second);
  }
  open_files.clear();
  text_images.clear();
  std::set<std::string> made = made_by_content;
  for (const auto& path : made) settle_made_file(path, debug);
}

// Read record `record` into buffer.
size_t CPMEmulator::read_record(OpenFile& of, uint32_t record, uint8_t* buffer,
                                bool sequential) {
  if (of.img) {
    const TextImage& img = *of.img;
    size_t at = static_cast<size_t>(record) * 128;
    if (at >= img.cpm.size()) return 0;
    memcpy(buffer, &img.cpm[at], 128);
    return 128;
  }
  // A seek on every call: the record is the FCB's, not the stream's.  It
  // also clears the stream's end-of-file indicator, which would otherwise go
  // on answering end of file after another FCB had written more.
  if (fseek(of.fp, static_cast<long>(record) * 128L, SEEK_SET) != 0) return 0;
  size_t n = fread(buffer, 1, 128, of.fp);
  if (sequential && of.mode == MODE_TEXT) {
    for (size_t i = 0; i < n; i++) {
      if (buffer[i] == CPM_EOF) return i;
    }
  }
  return n;
}

// Write record `record` from buffer.  A text file's change reaches the host
// at once when that is cheap, and at the latest when the file is closed.
bool CPMEmulator::write_record(OpenFile& of, uint32_t record, const uint8_t* buffer) {
  if (of.img) {
    TextImage& img = *of.img;
    image_write(img, record, buffer);
    if (!img.dirty) return true;
    size_t last = img.lines.back().first, end = img.text_end();
    if (img.dirty_from >= last && (end < last || end - last <= 65536)) return write_back(img);
    return true;
  }
  if (fseek(of.fp, static_cast<long>(record) * 128L, SEEK_SET) != 0) return false;
  size_t n = fwrite(buffer, 1, 128, of.fp);
  fflush(of.fp);
  return n == 128 && !ferror(of.fp);
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
      // Everything unrecognised is taken as a file mapping, which means a
      // mistyped directive becomes a mapping for a CP/M file nobody will
      // ever open and nothing says so.  Two cheap checks catch most of it
      // without changing what the line does.
      static const char* const kDirectives[] = {
        "program", "cd", "chdir", "default_mode", "debug", "eol_convert",
        "ctrl_c_exit", "printer", "aux_input", "aux_output"
      };
      std::string key_lower;
      for (char c : key) key_lower += tolower(c);
      bool warned = false;
      for (size_t d = 0; d < sizeof(kDirectives) / sizeof(kDirectives[0]); d++) {
        if (key_lower == kDirectives[d]) {
          fprintf(stderr,
                  "Config line %d: '%s' is being read as a file mapping; the "
                  "directive is spelled '%s'\n",
                  line_num, key.c_str(), kDirectives[d]);
          warned = true;
          break;
        }
      }
      // A CP/M name has a dot or a wildcard nearly always; a bare identifier
      // whose value names nothing on disk is far more likely a typo than a
      // mapping for an extension-less file that does not exist yet.
      if (!warned &&
          key.find('.') == std::string::npos && key.find('*') == std::string::npos &&
          value.find('/') == std::string::npos &&
          platform::get_file_type(value.c_str()) == platform::FileType::NotFound) {
        fprintf(stderr,
                "Config line %d: '%s' is not a directive, so it is a file "
                "mapping to '%s' - which does not exist\n",
                line_num, key.c_str(), value.c_str());
      }

      // Assume it's a file mapping: pattern = path [mode]
      FileMode mode = default_mode;
      bool eol_convert = default_eol_convert;

      // A value that is nothing but a mode sets the mode for every name the
      // pattern matches, wherever the file turns out to live.  This used to
      // register "text" as though it were a path, which never opened.
      if (value == "text" || value == "binary") {
        add_file_mapping_ex(key, "", value == "text" ? MODE_TEXT : MODE_BINARY,
                            value == "text" ? default_eol_convert : false);
        continue;
      }

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

// One field of a command-line name - the name, 8 bytes, or the type, 3 -
// from src[from, to), blank-padded and cut to the field as the CCP cuts it.
static void fill_fcb_field(qkz80_uint8* mem, qkz80_uint16 at, size_t width,
                           const std::string& src, size_t from, size_t to,
                           const std::string& whole) {
  size_t i = 0;
  for (size_t k = from; k < to && i < width; k++, i++) {
    char c = src[k];
    if (c == '*') {
      while (i < width) mem[at + i++] = '?';
      return;
    }
    if (c != '?' && !is_valid_cpm_char(c)) {
      fprintf(stderr, "Warning: invalid CP/M character '%c' in filename '%s'\n", c, whole.c_str());
      c = '_';  // Replace with underscore
    }
    mem[at + i] = static_cast<qkz80_uint8>(c);
  }
  while (i < width) mem[at + i++] = ' ';
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

  // Fill the name and the type field, blank-padded, the way 2.2's CCP
  // CONVERT does: a '*' fills the rest of its field with '?', and a '?' is
  // kept.  Both were taken for invalid characters and became '_', so a
  // program given *.BAK looked for _.BAK.  Anything else a CP/M name cannot
  // hold is replaced, with a warning, as before.
  size_t dot_pos = upper_name.find('.', name_start);
  size_t name_end = dot_pos != std::string::npos ? dot_pos : upper_name.length();
  fill_fcb_field(mem, fcb_addr + 1, 8, upper_name, name_start, name_end, filename);
  if (dot_pos != std::string::npos) {
    fill_fcb_field(mem, fcb_addr + 9, 3, upper_name, dot_pos + 1, upper_name.length(), filename);
  } else {
    fill_fcb_field(mem, fcb_addr + 9, 3, upper_name, 0, 0, filename);
  }
}

bool CPMEmulator::handle_pc(qkz80_uint16 pc) {
  // Check for JMP 0 (exit)
  if (pc == 0) {
    close_all_files();
    program_exit("Program exit via JMP 0");
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

// How many bytes from DE a BDOS function reads or writes here: to R2 for the
// calls that use R0-R2, to CR for 20 and 21, and so on down to the name alone
// for delete.  0 for a call that does not touch memory at DE - among them 16,
// which only looks the address up, and 18, which CP/M says ignores DE and
// which programs call with whatever DE was left holding.
static int fcb_bytes_used(qkz80_uint8 func) {
  switch (func) {
  case 19: return 12;                    // name
  case 17: return 15;                    // name, EX, S1, S2
  case 15: return 16;                    // and RC
  case 23: return 28;                    // the new name at 16-27
  case 22: return 32;                    // RC and the map cleared, to 31
  case 20: case 21: return 33;           // CR
  case 33: case 34: case 35: case 36: case 40: return 36;  // R0-R2
  default: return 0;
  }
}

void CPMEmulator::bdos_call(qkz80_uint8 func) {
  if (debug || debug_bdos_funcs.count(func)) {
    fprintf(stderr, "BDOS call %d\n", func);
  }

  // Every file call reads and writes its FCB as mem[DE + n], and the guest
  // memory is exactly 64K, so an FCB near the top ran off the end of it:
  // BDOS 22 at FFECh wrote 19 bytes of host heap.  CP/M would wrap the
  // address to 0000h, but no CP/M program can have an FCB there - the BDOS
  // and BIOS are at the top of memory, here as on a real machine - so the
  // call fails as a bad FCB rather than every FCB access learning to wrap.
  int fcb_len = fcb_bytes_used(func);
  if (fcb_len && cpu->get_reg16(qkz80::regp_DE) > 0x10000 - fcb_len) {
    if (debug || debug_bdos_funcs.count(func)) {
      fprintf(stderr, "BDOS %d: FCB at %04X runs past FFFFh, refused\n", func,
              cpu->get_reg16(qkz80::regp_DE));
    }
    cpu->set_reg8(0xFF, qkz80::reg_A);
    return;
  }

  // A call that looks at host files sees every text file's changes.  A
  // write that was not cheap to write back at once is still in its image.
  if (func == 17 || func == 23 || func == 35) flush_text_images();

  switch (func) {
  case 0:  // System Reset
    close_all_files();
    program_exit("System reset");
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
    // Binary files are written as they go; a text file's changes may still
    // be in its image.
    flush_text_images();
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
  // The first read past the end of input still answers CR, so a line the
  // guest was part way through submits and nothing that worked before
  // changes.  After that it answers ^Z, which is CP/M's end-of-input
  // character and what BIOS CONIN here has always returned - a program that
  // checks for it now stops on its own instead of reading CR forever.
  if (ch == -1 || ch == EOF) ch = (consecutive_console_eof == 1) ? '\r' : 0x1A;
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

bool CPMEmulator::open_fcb_file(qkz80_uint16 fcb_addr, int func) {
  bool trace = debug || debug_bdos_funcs.count(func);

  if (!validate_fcb_name(cpu->get_mem(), fcb_addr)) {
    if (trace) {
      fprintf(stderr, "BDOS Open: rejected invalid FCB filename\n");
    }
    cpu->set_reg8(0xFF, qkz80::reg_A);
    return false;
  }

  std::string filename = fcb_to_filename(fcb_addr);

  FileMode mode = MODE_BINARY;
  bool eol_convert = false;
  std::string unix_path = find_unix_file_ex(filename, &mode, &eol_convert,
                                            cpu->get_mem()[fcb_addr]);

  if (trace) {
    fprintf(stderr, "BDOS Open: '%s' -> '%s' (mode: %s)\n", filename.c_str(),
            unix_path.empty() ? "(not found)" : unix_path.c_str(),
            unix_path.empty() ? "none" : mode == MODE_TEXT ? "text" : "binary");
  }

  if (unix_path.empty()) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // File not found
    return false;
  }

  // Opening an FCB that is already open replaces it; close the old stream
  // rather than leaking it, and start the new one clean.  First, because
  // closing the old one can rewrite the file this is about to open: the
  // same name made by this FCB and not closed is decided at its last close.
  auto old = open_files.find(fcb_addr);
  if (old != open_files.end()) {
    std::string old_path = old->second.unix_path;
    close_open_file(old->second);
    open_files.erase(old);
    settle_made_file(old_path, trace);
    if (old_path == unix_path) {
      // it may be text now, and opens as what it is
      unix_path = find_unix_file_ex(filename, &mode, &eol_convert, cpu->get_mem()[fcb_addr]);
      if (unix_path.empty()) {
        cpu->set_reg8(0xFF, qkz80::reg_A);
        return false;
      }
    }
  }

  // A file made this run under a text-list name and not yet decided is still
  // written as it comes, whatever it holds so far; see settle_made_file.
  if (made_by_content.count(unix_path)) mode = MODE_BINARY;

  OpenFile of;
  if (mode == MODE_TEXT && eol_convert) {
    of.img = text_image(unix_path, false);
    if (!of.img) {
      cpu->set_reg8(0xFF, qkz80::reg_A);
      return false;
    }
  } else {
    of.fp = fopen(unix_path.c_str(), "r+b");
    if (!of.fp) of.fp = fopen(unix_path.c_str(), "rb");
    if (!of.fp) {
      cpu->set_reg8(0xFF, qkz80::reg_A);
      return false;
    }
  }
  of.unix_path = unix_path;
  of.cpm_name = filename;
  of.mode = mode;
  of.eol_convert = eol_convert;
  open_files[fcb_addr] = of;
  return true;
}

// The records a guest reading this file sequentially finds in it.  A binary
// file is its host size in 128-byte records.  A text file with conversion is
// its image's, and one without is its host bytes up to the first ^Z.
uint32_t CPMEmulator::cpm_record_count(const OpenFile& of) {
  if (of.img) return static_cast<uint32_t>(of.img->records());
  if (of.mode != MODE_TEXT) {
    int64_t size = platform::get_file_size(of.unix_path.c_str());
    if (size <= 0) return 0;
    int64_t records = (size + 127) / 128;
    return records > 0xFFFFFF ? 0xFFFFFF : static_cast<uint32_t>(records);
  }
  FILE* fp = fopen(of.unix_path.c_str(), "rb");
  if (!fp) return 0;
  uint64_t bytes = 0;
  int ch;
  while ((ch = fgetc(fp)) != EOF && ch != CPM_EOF) bytes++;
  fclose(fp);
  uint64_t records = (bytes + 127) / 128;
  return records > 0xFFFFFF ? 0xFFFFFF : static_cast<uint32_t>(records);
}

// 2.2's OPENFIL clears S2 and opens the directory entry for extent EX of
// module 0, keeping the caller's EX.  With cpmemu's disk (EXM = 0) an entry is
// one logical extent, so the open fails, FFh, when the file has no records in
// that extent - extent 0 always exists, even for an empty file - and RC is
// that extent's record count: 128 for any extent before the last.  This
// opened every extent that was asked for and answered RC = 128 whatever the
// file held, so a program that finds a file's end the CP/M 1.4 way, opening
// extents 0, 1, 2 ... until one fails and taking the last one's RC, never
// found it.  Before the branch that let the caller's EX stand, EX was forced
// to 0 and the loop could not even move.
void CPMEmulator::bdos_open_file() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
  if (!open_fcb_file(fcb_addr, 15)) return;

  qkz80_uint8* mem = cpu->get_mem();
  auto it = open_files.find(fcb_addr);
  uint32_t records = cpm_record_count(it->second);
  uint32_t extents = records == 0 ? 1 : (records + 127) / 128;
  uint32_t ex = mem[fcb_addr + FCB_EX] & 0x1F;
  if (ex >= extents) {
    if (debug || debug_bdos_funcs.count(15)) {
      fprintf(stderr, "BDOS Open: '%s' has %u records, no extent %u\n",
              it->second.cpm_name.c_str(), records, ex);
    }
    std::string path = it->second.unix_path;
    close_open_file(it->second);
    open_files.erase(it);
    settle_made_file(path, debug || debug_bdos_funcs.count(15));
    cpu->set_reg8(0xFF, qkz80::reg_A);
    return;
  }
  mem[fcb_addr + FCB_S2] = 0;
  mem[fcb_addr + FCB_RC] = static_cast<qkz80_uint8>(
      ex + 1 < extents ? 128 : records - ex * 128);

  cpu->set_reg8(0, qkz80::reg_A);  // Success
}

void CPMEmulator::bdos_close_file() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);

  if (debug || debug_bdos_funcs.count(16)) {
    fprintf(stderr, "Close file: FCB at %04X\n", fcb_addr);
  }

  auto it = open_files.find(fcb_addr);
  if (it != open_files.end()) {
    if (debug || debug_bdos_funcs.count(16)) {
      fprintf(stderr, "Close file: closing '%s'\n", it->second.cpm_name.c_str());
    }
    std::string path = it->second.unix_path;
    close_open_file(it->second);
    open_files.erase(it);
    settle_made_file(path, debug || debug_bdos_funcs.count(16));
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

// The DMA buffer is guest memory, and a guest address is 16 bits: a buffer
// at FFC0h runs on at 0000h, as the Z80's own addressing does.  These were
// memcpy and fread at &mem[current_dma], which ran up to 127 bytes past the
// 64K the emulator allocates when the guest put its buffer near the top.
void CPMEmulator::dma_put(const uint8_t* src, size_t n) {
  qkz80_uint8* mem = cpu->get_mem();
  for (size_t i = 0; i < n; i++) mem[(current_dma + i) & 0xFFFF] = src[i];
}

void CPMEmulator::dma_get(uint8_t* dst, size_t n) {
  const qkz80_uint8* mem = cpu->get_mem();
  for (size_t i = 0; i < n; i++) dst[i] = mem[(current_dma + i) & 0xFFFF];
}

// CP/M keeps everything about an open file in its FCB: a close only writes
// the directory, a disk reset does not invalidate an FCB, and a copy of an
// FCB reads on from where the original was.  cpmemu keeps a host stream per
// FCB address, so an FCB it has no stream for - closed, reset, or copied to
// another address - is opened again from its name, the FCB left as it is;
// its EX, S2 and CR still say which record.  Only BDOS 21 did this; 20, 33,
// 34 and 40 answered 0xFF, so a program that closed a file to checkpoint its
// directory entry and went on reading, or copied an FCB after opening it,
// failed here and worked on CP/M.
OpenFile* CPMEmulator::fcb_open_file(qkz80_uint16 fcb_addr, int func) {
  auto it = open_files.find(fcb_addr);
  if (it != open_files.end()) return &it->second;
  if (debug || debug_bdos_funcs.count(func)) {
    fprintf(stderr, "BDOS %d: FCB %04X not open, opening it from its name\n", func, fcb_addr);
  }
  if (!open_fcb_file(fcb_addr, func)) return nullptr;  // A = 0xFF
  return &open_files.find(fcb_addr)->second;
}

// BDOS 20 and 21 read and write the record EX, S2 and CR name - CR within
// logical extent EX of module S2 - which is the record BDOS 36 would report
// for the same FCB.  They used to read and write wherever the host stream had
// got to and only count CR up, so a guest that set CR back to 0 and wrote,
// as MP/M's GENSYS does with SYSTEM.DAT, appended instead of rewriting.
//
// CR = 128 is what reading an extent's last record leaves, and the call
// after it goes to record 0 of the next extent.  2.2 does that for a read;
// for a write it answers error 1, and this takes the record the numbering
// says instead.  A CR above 128 names no record: end of file for a read,
// error 1 for a write, both as 2.2 answers.
void CPMEmulator::bdos_read_sequential() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();
  qkz80_uint8* f = &mem[fcb_addr];
  bool trace = debug || debug_bdos_funcs.count(20);

  OpenFile* of = fcb_open_file(fcb_addr, 20);
  if (!of) return;  // A = 0xFF: no such file

  qkz80_uint8 cr = f[FCB_CR];
  uint32_t record = fcb_extent_base(f) + cr;
  uint8_t buffer[128];
  size_t nread = 0;
  if (cr <= 128 && record < FCB_MAX_RECORDS) {
    nread = read_record(*of, record, buffer, true);
  }

  // CP/M convention: return A=0 (success) when data is available,
  // return A=1 (EOF) only when no more data can be read.
  // For partial records at end of file, return success with Ctrl-Z padding.
  // At the end the FCB is left alone - it used to step CR anyway, so an
  // append written after it landed one record past the end.
  if (nread == 0) {
    cpu->set_reg8(1, qkz80::reg_A);  // EOF - no data available
    if (trace) {
      fprintf(stderr, "Read sequential: FCB %04X file '%s' record %u -> EOF (no data)\n",
              fcb_addr, of->cpm_name.c_str(), record);
    }
    return;
  }

  // Pad to 128 bytes if needed
  if (nread < 128) {
    pad_to_128(buffer, nread);
  }

  dma_put(buffer, 128);
  cpu->set_reg8(0, qkz80::reg_A);  // Success

  if (trace) {
    fprintf(stderr, "Read sequential: FCB %04X file '%s' record %u read %zu bytes, returning A=0\n",
            fcb_addr, of->cpm_name.c_str(), record, nread);
  }

  // The record after: CR + 1, so CR = 128 after an extent's last record.  A
  // read at CR = 128 was record 0 of the next extent, so the FCB moves there.
  if (cr == 128) {
    fcb_next_extent(f);
    cr = 0;
  }
  f[FCB_CR] = static_cast<qkz80_uint8>(cr + 1);
}

void CPMEmulator::bdos_write_sequential() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();
  qkz80_uint8* f = &mem[fcb_addr];
  bool trace = debug || debug_bdos_funcs.count(21);

  // Not through BDOS 15 if it is not open, which would clear S2: the FCB
  // already says where this record goes.
  OpenFile* of = fcb_open_file(fcb_addr, 21);
  if (!of) return;  // A = 0xFF

  qkz80_uint8 cr = f[FCB_CR];
  uint32_t record = fcb_extent_base(f) + cr;
  if (cr > 128 || record >= FCB_MAX_RECORDS) {
    cpu->set_reg8(1, qkz80::reg_A);  // no such record: 2.2's error 1
    return;
  }

  uint8_t buffer[128];
  dma_get(buffer, 128);
  bool ok = write_record(*of, record, buffer);
  if (trace) {
    fprintf(stderr, "Write sequential: FCB %04X file '%s' record %u %s\n", fcb_addr,
            of->cpm_name.c_str(), record, ok ? "written" : "FAILED");
  }
  if (!ok) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
    return;
  }
  cpu->set_reg8(0, qkz80::reg_A);  // Success

  // The record after.  Writing an extent's last record moves the FCB to the
  // next extent at once, EX + 1 and CR = 0, which is 2.2's WTSEQ; reading
  // one leaves CR = 128 instead.
  if (cr == 128) {
    fcb_next_extent(f);
    cr = 0;
  }
  f[FCB_CR] = static_cast<qkz80_uint8>(cr + 1);
  if (f[FCB_CR] == 128) fcb_next_extent(f);
}

// The mode a file BDOS 22 creates is written in: a mode rule for the name if
// there is one, else default_mode.  Make used default_mode as it stood, and
// auto is not binary, so it was converted as text: every file a guest
// created, a .COM or a .REL included, lost each ^Z record tail and had its
// CR LFs collapsed.  Under auto a name on the binary list is binary, and the
// rest are written as they come, since what they are is not known until
// they have been written - see MakeKind.  A name on the text list used to be
// made as text, and every name on it also names binary files: MBASIC's SAVE
// makes X.BAS tokenized unless told ,A, and a library program can make its
// X.LIB directly.
void CPMEmulator::make_file_mode(const std::string& filename, FileMode* mode, bool* eol,
                                 MakeKind* kind) {
  std::string normalized = normalize_cpm_filename(filename);
  *mode = default_mode;
  *eol = default_eol_convert;
  for (const auto& mapping : file_mappings) {
    if (mapping.unix_pattern.empty() && match_pattern(mapping.cpm_pattern, normalized)) {
      *mode = mapping.mode;
      *eol = mapping.eol_convert;
    }
  }
  MakeKind k = MAKE_AS_MODE;
  if (*mode == MODE_AUTO) {
    FileMode ext = extension_mode(normalized);
    if (ext == MODE_TEXT) k = MAKE_BY_CONTENT;
    else if (ext == MODE_AUTO) k = MAKE_GUESSED;
    *mode = MODE_BINARY;
  }
  if (kind) *kind = k;
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

  FileMode mode;
  bool eol_convert;
  MakeKind kind;
  make_file_mode(filename, &mode, &eol_convert, &kind);

  if (debug || debug_bdos_funcs.count(22)) {
    fprintf(stderr, "Make file: %s (mode: %s%s)\n", filename.c_str(),
            mode == MODE_TEXT ? "text" : "binary",
            kind == MAKE_GUESSED ? ", until renamed"
            : kind == MAKE_BY_CONTENT ? ", text at its close if it is text" : "");
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

  // Make with EX above 0 makes that extent of the file, and in CP/M the
  // extents before it are the file's own: 2.2's FCREATE writes a directory
  // entry for the extent the FCB names and touches no other.  A CP/M 1.4
  // program that writes past a file's end opens the next extent, and when
  // the open fails - which it does now for an extent the file has not got -
  // makes it.  Truncating here, as a make of extent 0 does, would throw the
  // whole file away at that point, so an existing file is opened as it is.
  FILE* fp = nullptr;
  bool extending = false;
  if ((cpu->get_mem()[fcb_addr + FCB_EX] & 0x1F) != 0) {
    FileMode found_mode;
    bool found_eol;
    std::string found = find_unix_file_ex(filename, &found_mode, &found_eol,
                                          cpu->get_mem()[fcb_addr]);
    if (!found.empty() && (fp = fopen(found.c_str(), "r+b")) != nullptr) {
      extending = true;
      unix_name = found;
      mode = found_mode;
      eol_convert = found_eol;
      if (debug || debug_bdos_funcs.count(22)) {
        fprintf(stderr, "Make file: %s exists, extent %u added to it\n", found.c_str(),
                cpu->get_mem()[fcb_addr + FCB_EX] & 0x1Fu);
      }
    }
  }
  if (!fp) fp = fopen(unix_name.c_str(), "w+b");
  if (!fp) {
    cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
    return;
  }
  if (extending) {
    // the file is what it was; a rename decides it as before, if at all
  } else {
    made_guessed.erase(unix_name);
    made_by_content.erase(unix_name);
    made_random.erase(unix_name);
    if (kind == MAKE_GUESSED) made_guessed.insert(unix_name);
    if (kind == MAKE_BY_CONTENT) made_by_content.insert(unix_name);
  }
  std::shared_ptr<TextImage> img;
  if (mode == MODE_TEXT && eol_convert) {
    fclose(fp);
    fp = nullptr;
    img = text_image(unix_name, !extending);
    if (!img) {
      cpu->set_reg8(0xFF, qkz80::reg_A);
      return;
    }
  } else if (!extending) {
    forget_text_image(unix_name);  // an FCB still open on the old file keeps it
  }

  auto old = open_files.find(fcb_addr);
  if (old != open_files.end()) {
    std::string old_path = old->second.unix_path;
    close_open_file(old->second);
    open_files.erase(old);
    if (old_path != unix_name) settle_made_file(old_path, debug || debug_bdos_funcs.count(22));
  }

  OpenFile of;
  of.fp = fp;
  of.img = img;
  of.unix_path = unix_name;
  of.cpm_name = filename;
  of.mode = mode;
  of.eol_convert = eol_convert;
  open_files[fcb_addr] = of;

  // What 2.2's FCREATE and GETEMPTY clear: S2, S1, and RC with the
  // allocation map after it.  EX is the caller's, as for open.
  qkz80_uint8* mem = cpu->get_mem();
  mem[fcb_addr + FCB_S1] = 0;
  mem[fcb_addr + FCB_S2] = 0;
  memset(&mem[fcb_addr + FCB_RC], 0, 17);

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
    made_guessed.erase(unix_path);
    made_by_content.erase(unix_path);
    made_random.erase(unix_path);
    forget_text_image(unix_path);
    cpu->set_reg8(0, qkz80::reg_A);  // Success
  }
}

// BDOS 33, 34 and 40 read and write R0-R2's record, and leave the FCB
// pointing at it - CR the record, EX and S2 its extent - so that a sequential
// call after them reads that record again or writes it again, which is what
// 2.2 does and what its manual promises.  They used to leave the FCB alone and
// the host stream one record on, so the sequential call went to the next one.
//
// A record of a text file with conversion is the same record a sequential
// call reads, from its image.  It was 128 raw host bytes at record * 128,
// which is not where that record's text is once an LF has become CR LF.
bool CPMEmulator::random_position(qkz80_uint16 fcb_addr, uint32_t* record) {
  qkz80_uint8* f = &cpu->get_mem()[fcb_addr];
  *record = static_cast<uint32_t>(f[FCB_R0] | (f[FCB_R1] << 8) | (f[FCB_R2] << 16));
  if (*record >= FCB_MAX_RECORDS) {
    cpu->set_reg8(6, qkz80::reg_A);  // seek past physical end of disk
    return false;
  }
  fcb_set_record(f, *record);
  return true;
}

void CPMEmulator::bdos_read_random() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);

  OpenFile* of = fcb_open_file(fcb_addr, 33);
  if (!of) return;  // A = 0xFF: no such file

  uint32_t record_num;
  if (!random_position(fcb_addr, &record_num)) return;

  uint8_t buffer[128];
  size_t nread = read_record(*of, record_num, buffer, false);

  if (debug || debug_bdos_funcs.count(33)) {
    fprintf(stderr, "Read random: FCB %04X file '%s' record %u read %zu bytes\n",
            fcb_addr, of->cpm_name.c_str(), record_num, nread);
  }

  if (nread == 0) {
    cpu->set_reg8(1, qkz80::reg_A);  // EOF
  } else {
    pad_to_128(buffer, nread);
    dma_put(buffer, 128);
    cpu->set_reg8(0, qkz80::reg_A);  // Success
  }
}

void CPMEmulator::bdos_write_random() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);

  OpenFile* of = fcb_open_file(fcb_addr, 34);
  if (!of) return;  // A = 0xFF: no such file

  uint32_t record_num;
  if (!random_position(fcb_addr, &record_num)) return;

  uint8_t buffer[128];
  dma_get(buffer, 128);
  bool ok = write_record(*of, record_num, buffer);
  if (made_by_content.count(of->unix_path)) made_random.insert(of->unix_path);

  if (debug || debug_bdos_funcs.count(34)) {
    fprintf(stderr, "Write random: FCB %04X file '%s' record %u %s\n",
            fcb_addr, of->cpm_name.c_str(), record_num, ok ? "written" : "FAILED");
  }
  cpu->set_reg8(ok ? 0 : 0xFF, qkz80::reg_A);
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

  // File size in 128-byte records (round up).  Same 64-bit-then-clamp shape as
  // write_dir_entry: r0/r1/r2 below is a 24-bit random-record field, so a file
  // of more than 0xFFFFFF records - 2 GiB - cannot be described here at all.
  // Saturate rather than wrap, so a huge file reports "as large as this field
  // can say" instead of a small wrapped number that looks legitimate.
  int64_t records64 = (file_size + 127) / 128;
  if (records64 > 0xFFFFFF) records64 = 0xFFFFFF;
  uint32_t records = static_cast<uint32_t>(records64);
  // A text file with conversion has the records its image has - the ones a
  // random read reads - not its host bytes / 128: an LF file is short of them
  // by one byte a line, so the record a program took for the last was not.
  if (mode == MODE_TEXT && eol_convert) records = text_record_count(unix_path);

  // Store in FCB bytes 33-35 (r0, r1, r2)
  mem[fcb_addr + 33] = records & 0xFF;
  mem[fcb_addr + 34] = (records >> 8) & 0xFF;
  mem[fcb_addr + 35] = (records >> 16) & 0xFF;

  cpu->set_reg8(0, qkz80::reg_A);  // Success
}

void CPMEmulator::bdos_set_random_record() {
  qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
  qkz80_uint8* mem = cpu->get_mem();

  // The record the next sequential call would use: CR within extent EX of
  // module S2, the numbering 20 and 21 use.  This counted EX * 128 + CR and
  // left S2 out, and took all eight bits of EX.  CR is taken whole, as 2.2's
  // COMPRAND takes it, so CR = 128 is the first record of the next extent.
  uint32_t record_num = fcb_extent_base(&mem[fcb_addr]) + mem[fcb_addr + FCB_CR];

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
    renamed_made_file(old_path, new_name, new_path);
    // An FCB still open on the file follows it, as a CP/M FCB does: its
    // image, and the path its host file is found by, are the new name's.
    if (new_path != old_path) forget_text_image(new_path);  // the file it replaced
    auto moved = text_images.find(old_path);
    if (moved != text_images.end() && new_path != old_path) {
      moved->second->path = new_path;
      text_images[new_path] = moved->second;
      text_images.erase(moved);
    }
    for (auto& pair : open_files) {
      if (pair.second.unix_path == old_path) pair.second.unix_path = new_path;
    }
    cpu->set_reg8(0, qkz80::reg_A);  // Success
  }
}

// A file BDOS 22 made under a name auto could only guess at - U.$$$, say -
// was written as it came, binary.  PIP, ED and WordStar all write NAME.$$$
// and rename it when they are done, so its real name arrives only here.
// When that name is one this file would be read as text under, the host copy
// becomes the host text it would have been had the guest made it under that
// name: CR LF to LF, ending at the ^Z.  Without this, PIP U.TXT=T.TXT of a
// two-line Unix file left u.txt as 128 bytes of CR LF text and ^Z padding
// where e4f7fd5, which made every file as text, left the 12 bytes it was
// given.  A name that still says nothing keeps the file on the list, so a
// second rename can decide.  A MAKE_BY_CONTENT file renamed while still open
// is decided by its new name the same way.
void CPMEmulator::renamed_made_file(const std::string& old_path, const std::string& new_name,
                                    const std::string& new_path) {
  // whatever the name held before is gone
  made_guessed.erase(new_path);
  made_by_content.erase(new_path);
  made_random.erase(new_path);
  made_random.erase(old_path);
  if (made_guessed.erase(old_path) + made_by_content.erase(old_path) == 0) return;

  bool trace = debug || debug_bdos_funcs.count(23);
  FileMode mode;
  bool eol_convert;
  MakeKind kind;
  make_file_mode(new_name, &mode, &eol_convert, &kind);
  if (kind == MAKE_GUESSED) {
    made_guessed.insert(new_path);
    return;
  }
  if (kind == MAKE_AS_MODE && (mode != MODE_TEXT || !eol_convert)) return;
  // Converting under an open stream would leave that stream writing to the
  // file the conversion replaced.  CP/M lets a program rename an open file;
  // none of the three above does.
  for (const auto& pair : open_files) {
    if (pair.second.is_open() && pair.second.unix_path == old_path) {
      if (trace) fprintf(stderr, "Rename: %s still open, left as written\n", new_path.c_str());
      return;
    }
  }
  convert_made_file_to_text(new_path, "Rename", trace);
}

void CPMEmulator::settle_made_file(const std::string& path, bool trace) {
  auto it = made_by_content.find(path);
  if (it == made_by_content.end()) return;
  for (const auto& pair : open_files) {
    if (pair.second.is_open() && pair.second.unix_path == path) return;  // not its last close
  }
  // Nothing written yet says nothing: it stays undecided, and opens as it
  // is, for what a program writes after it opens it again.  Microsoft's
  // LIB-80 makes its work file MYLIB.LIB and at once opens it again with the
  // same FCB; decided then, empty, it opened as text and the REL library
  // written into it went through the converter.
  if (platform::get_file_size(path.c_str()) <= 0) return;
  made_by_content.erase(it);
  // Written in sequence only, it is converted as the text writer converted
  // a text name before: CR LF to LF whether or not every line reads back as
  // it was written - RMAC's listings have a bare LF after their title line.
  // Written at random, it is converted only if every record reads back as it
  // was written, since a random file's records have to be where they were.
  bool random = made_random.erase(path) != 0;
  convert_made_file_to_text(path, "Close", trace, random ? READS_BACK_RECORDS : AS_WRITER);
}

// Rewrite a file of CP/M text as host text, if the file is text - the rule
// bytes_look_like_text applies to a name on the text list when it is opened -
// and if it passes `check`, which is checked, not assumed.  Text ends at the
// first ^Z, or at a NUL with only NULs and ^Zs after it.  A binary file
// renamed to a text name - DRI's LIB writes X.$$$ and renames it X.LIB - or
// saved under one - MBASIC's tokenized X.BAS - is left exactly as it was
// written.  Returns whether the file was rewritten.
//
// 8-bit text, TEXT_8BIT, has to read back as it is whatever `check` says:
// CP/M text with a Latin-1 or code page 437 character in it, which PIP, ED or
// a text writer leaves with CR LF lines, is host text once its CR LFs are LFs,
// and reads back the same; a WordStar document, whose 8Dh LF soft returns
// would come back as 8Dh CR LF, a hard return, stays as written.  Since
// 0ea06bc required UTF-8 of every close and rename, the first had stayed
// CP/M text on the host, CR LF and ^Z padding.
//
// A file written at random, READS_BACK_RECORDS, also must not end its text
// in NULs.  Host text reads back padded with ^Z, so a last record that ended
// in NULs - "JOHN" and a name field of NULs, from a random file of names -
// would come back with ^Zs where its NULs were; left as written, it opens as
// text, the NULs part of it, and reads back as it was.  What follows a ^Z is
// another matter: a text open drops it whether the file is converted or not,
// and MBASIC, which writes even PRINT # files at random, leaves the rest of
// its buffer after the ^Z in the last record.
bool CPMEmulator::convert_made_file_to_text(const std::string& path, const char* who,
                                            bool trace, MadeCheck check) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) return false;
  std::vector<uint8_t> raw;
  uint8_t chunk[4096];
  size_t n;
  while ((n = fread(chunk, 1, sizeof chunk, fp)) > 0) raw.insert(raw.end(), chunk, chunk + n);
  fclose(fp);

  size_t end = raw.size();
  for (size_t i = 0; i < raw.size(); i++) {
    if (raw[i] == CPM_EOF || raw[i] == 0) { end = i; break; }
  }
  const char* why = nullptr;
  TextKind kind = text_kind(raw.data(), raw.size(), raw.size());
  if (kind == NOT_TEXT) why = "not text";
  if (kind == TEXT_8BIT && check == AS_WRITER) check = READS_BACK_TEXT;
  if (!why && check == READS_BACK_RECORDS && end < raw.size() && raw[end] == 0) {
    why = "text ending in NULs, which host text would read back as ^Zs";
  }

  // CR LF to LF, as the text writer does it; a lone CR or LF stays.
  std::vector<uint8_t> host;
  for (size_t i = 0; !why && i < end; i++) {
    if (raw[i] == '\r' && i + 1 < end && raw[i + 1] == '\n') continue;
    host.push_back(raw[i]);
  }
  // And back, as the text reader does it: an LF not after a CR gains one.
  if (!why && check != AS_WRITER) {
    std::vector<uint8_t> back;
    bool cr = false;
    for (uint8_t ch : host) {
      if (ch == '\n' && !cr) back.push_back('\r');
      back.push_back(ch);
      cr = (ch == '\r');
    }
    if (back.size() != end || !std::equal(back.begin(), back.end(), raw.begin())) {
      why = "text the converter would not read back as it is";
    }
  }
  if (why) {
    if (trace) fprintf(stderr, "%s: %s left as written: %s\n", who, path.c_str(), why);
    return false;
  }

  fp = fopen(path.c_str(), "wb");
  if (!fp) return false;
  bool ok = host.empty() || fwrite(host.data(), 1, host.size(), fp) == host.size();
  ok = (fclose(fp) == 0) && ok;
  if (trace) {
    fprintf(stderr, "%s: %s converted to host text, %zu bytes to %zu%s\n", who, path.c_str(),
            raw.size(), host.size(), ok ? "" : " (WRITE FAILED)");
  }
  return ok;
}

void CPMEmulator::bdos_direct_console_io() {
  qkz80_uint8 e_reg = cpu->get_reg8(qkz80::reg_E);

  if (e_reg == 0xFF) {
    // Input mode - return character if available, 0 if not
    if (platform::stdin_has_data()) {
      int ch = platform::console_getchar();
      if (ch == -1 || ch == EOF) {
        // End of input, not "nothing yet".  BDOS 6 spells both 0, so the guest
        // cannot tell them apart and a polling loop never terminates; count it
        // so the shared limit eventually ends the run with a reason.
        note_console_eof();
        ch = 0;
      } else {
        consecutive_console_eof = 0;
      }
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
  close_all_files();

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

  // One directory entry per logical extent, as a CP/M directory has them,
  // and the FCB's EX and S2 choose which, as 2.2's GETFST and SAMEXT do with
  // EXM = 0: EX = '?' takes every extent of the module S2 names, or of every
  // module if S2 is '?' too; any other EX takes that extent of module 0 and
  // nothing if the file has no such extent.  A drive byte of '?' takes every
  // extent whatever EX and S2 hold, since GETFST then compares no byte of
  // the FCB at all.  This gave one entry per file, EX = 0 and RC at most 128,
  // whatever was asked, so STAT and every lister that adds up a file's
  // extents saw a 300-record file as 128 records.
  {
    bool every = mem[fcb_addr] == '?';
    qkz80_uint8 want_ex = every ? '?' : mem[fcb_addr + FCB_EX];
    qkz80_uint8 want_s2 = every ? '?' : mem[fcb_addr + FCB_S2];
    std::vector<SearchResult> extents;
    for (const auto& r : search_results) {
      // Keep the division in 64 bits and clamp BEFORE narrowing.  Computing
      // this as `int records` overflowed at 2^31 records - 274877906816
      // bytes, reachable on any LP64 host and cheap to reach with a sparse
      // file - after which records went negative, the `> 128` clamp never
      // fired, and the RC byte and the whole allocation map came back zero:
      // the guest saw a 256 GiB file as empty.  Measured by bisection on
      // APFS: 274877906816 gave RC 0x80, one byte more gave RC 0x00.  The
      // extents listed stop where an FCB can no longer name one.
      int64_t size = platform::get_file_size(r.path.c_str());
      int64_t records64 = size > 0 ? (size + 127) / 128 : 0;
      uint32_t records = records64 > 0xFFFFFF ? 0xFFFFFF : static_cast<uint32_t>(records64);
      uint32_t count = records == 0 ? 1 : (records + 127) / 128;
      if (count > FCB_MAX_RECORDS / 128) count = FCB_MAX_RECORDS / 128;
      for (uint32_t k = 0; k < count; k++) {
        bool match;
        if (want_ex == '?') {
          match = want_s2 == '?' || (want_s2 & 0x7Fu) == (k >> 5);
        } else {
          match = k == (want_ex & 0x1Fu);
        }
        if (!match) continue;
        SearchResult e = r;
        e.extent = k;
        e.records = records;
        extents.push_back(e);
      }
    }
    search_results.swap(extents);
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
  // The records in this entry's extent: 128 for any before the last.
  int64_t before = static_cast<int64_t>(r.extent) * 128;
  int64_t records = r.records > before ? r.records - before : 0;
  int rc = records > 128 ? 128 : static_cast<int>(records);  // RC in this extent

  uint8_t entry[32];
  memset(entry, 0, 32);
  entry[0] = search_user;  // User number
  memcpy(&entry[1], r.name, 8);
  memcpy(&entry[9], r.ext, 3);
  entry[12] = static_cast<uint8_t>(r.extent & 0x1F);  // EX (extent)
  entry[13] = 0;  // S1
  entry[14] = static_cast<uint8_t>(r.extent >> 5);    // S2 (module)
  entry[15] = rc; // RC (record count)
  // Allocation map bytes 16-31 can be any non-zero value for existing file
  for (int i = 16; i < 32; i++) {
    entry[i] = (i - 16 < (records + 7) / 8) ? 0x01 : 0x00;
  }
  dma_put(entry, 32);
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
  close_all_files();
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
    close_all_files();
    program_exit("BIOS WBOOT called - exiting");
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

  // BIOS SECTRAN - Sector Translate.  BC is the logical sector, DE the
  // translate table, and the physical sector comes back in HL.  This shared
  // the disk stub group below, which sets only A, so HL came back holding
  // whatever the guest had left in it and the table was never read at all:
  // measured, a guest that loaded HL with DEAD and called this with BC=0005
  // and DE=0 got DEAD back where a real BIOS answers 0005.  It also clobbered
  // A, which the skeletal CBIOS - XCHG / DAD B / MOV L,M / MVI H,0 / RET -
  // leaves alone, so A is untouched here.
  //
  // It is arithmetic over guest memory and reaches no media, so there is no
  // failure for CPM_BIOS_DISK=fail to report and nothing unimplemented for
  // =error to refuse: it now answers the same way in all three modes.  That
  // is a behavior change for =error, which used to take the emulator down
  // over a table lookup.  The group below still stops a guest at the first
  // call that would actually touch a disk.
  case BIOS_SECTRAN: {
    qkz80_uint8* mem = cpu->get_mem();
    qkz80_uint16 sector = cpu->get_reg16(qkz80::regp_BC);
    qkz80_uint16 table = cpu->get_reg16(qkz80::regp_DE);
    // DE = 0 means no translation, and it is the case this emulator's own
    // DPH drives: the XLT word at DPH_ADDR is zero.  The answer there is the
    // logical sector itself, not the byte at address BC.  The skeletal CBIOS
    // has no such test and would index off page zero; answering BC is the
    // no-translation convention, not that listing.  The cast keeps the
    // sum inside the 64K address space, as ADD HL,BC does; without it a
    // table at FFFF reads off the end of the memory array.
    qkz80_uint16 phys = table ? mem[(qkz80_uint16)(table + sector)] : sector;
    // set_reg16 takes the value first.  A table entry is a byte, so this is
    // also what clears H on that path, the MVI H,0 of the skeletal listing.
    cpu->set_reg16(phys, qkz80::regp_HL);
    if (debug || debug_bios_offsets.count(offset)) {
      fprintf(stderr, "BIOS SECTRAN: sector %04X table %04X -> %04X\n",
              (unsigned)sector, (unsigned)table, (unsigned)phys);
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
    if (bios_disk_mode == 2) {
      // Error mode - exit emulator
      fprintf(stderr, "FATAL: Unimplemented BIOS disk function at offset %d\n", offset);
      fprintf(stderr, "This emulator handles file I/O at the BDOS level.\n");
      fprintf(stderr, "Set CPM_BIOS_DISK=ok or CPM_BIOS_DISK=fail to change this behavior.\n");
      exit(1);
    } else if (bios_disk_mode == 1) {
      // Fail mode - return error to caller.  A = 1 is the CP/M BIOS permanent
      // error, which READ and WRITE are the two calls documented to return;
      // the rest of this group returns no status at all, so the value is
      // simply unread there.  This returned 0 until 4.7.1 - byte for byte
      // what "ok" returns - so the mode the startup line announced as
      // "BIOS disk functions will return failure" was invisible to the guest
      // and every CPM_BIOS_DISK=fail run behaved as CPM_BIOS_DISK=ok.
      cpu->set_reg8(0x01, qkz80::reg_A);  // Return failure
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
    fprintf(stderr, "Options may also be written after the program; they are\n");
    fprintf(stderr, "applied and kept out of the CP/M command tail.\n");
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

  // One definition of what an emulator option is, used both for the options
  // written before the program name and for any written after it.  Keeping
  // it in one place is the point: the two passes cannot drift apart.
  auto try_option = [&](const char* a) -> bool {
    if (strcmp(a, "--8080") == 0) {
      mode_8080 = true;
    } else if (strcmp(a, "--z80") == 0) {
      mode_8080 = false;
    } else if (strncmp(a, "--progress=", 11) == 0) {
      cli_progress_interval = atoll(a + 11) * 1000000LL;
    } else if (strcmp(a, "--progress") == 0) {
      cli_progress_interval = 100 * 1000000LL;  // Default to 100M if no value specified
    } else if (strncmp(a, "--save-memory=", 14) == 0) {
      save_memory_file = a + 14;
    } else if (strncmp(a, "--save-range=", 13) == 0) {
      // Parse range like "DC00-FFFF"
      unsigned int start, end;
      if (sscanf(a + 13, "%x-%x", &start, &end) == 2) {
        save_memory_start = start;
        save_memory_end = end;
      }
    } else if (strncmp(a, "--int-cycles=", 13) == 0) {
      int_cycles = strtoull(a + 13, nullptr, 10);
    } else if (strncmp(a, "--int-rst=", 10) == 0) {
      int_rst = atoi(a + 10) & 7;  // Clamp to 0-7
    } else if (strcmp(a, "--no-ctrl-c-exit") == 0) {
      ctrl_c_exit_enabled = false;
      ctrl_c_exit_from_cli = true;  // Outranks a 'ctrl_c_exit' config line
    } else if (strcmp(a, "--ctrl-c-exit") == 0) {
      ctrl_c_exit_enabled = true;
      ctrl_c_exit_from_cli = true;  // Outranks a 'ctrl_c_exit' config line
    } else {
      return false;  // Not ours - the program's, or the program name itself
    }
    return true;
  };

  while (arg_offset < argc && argv[arg_offset][0] == '-' && try_option(argv[arg_offset])) {
    arg_offset++;
  }

  if (argc < arg_offset + 1) {
    fprintf(stderr, "Error: No program specified\n");
    fprintf(stderr, "Usage: %s [options] <program.com|config.cfg> [args...]\n", argv[0]);
    return 1;
  }

  // An emulator option written after the program or config file used to be
  // handed to the CP/M program instead, silently: `cpmemu prog.cfg
  // --no-ctrl-c-exit` left the ^C exit on and said nothing.  Recognised
  // options are now honoured wherever they appear and kept out of the
  // command tail.  Only exact matches for options this emulator defines are
  // taken; anything else still belongs to the program, so a CP/M tail like
  // TEST.COM/N/E is untouched.  This runs before the config file is read, so
  // a flag still outranks a 'ctrl_c_exit' line as documented.
  std::vector<char*> guest_argv;
  for (int i = 0; i <= arg_offset && i < argc; i++) {
    guest_argv.push_back(argv[i]);
  }
  for (int i = arg_offset + 1; i < argc; i++) {
    if (argv[i][0] == '-' && try_option(argv[i])) {
      fprintf(stderr, "Note: '%s' taken as an emulator option, not passed to the program\n",
              argv[i]);
      continue;
    }
    guest_argv.push_back(argv[i]);
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
  static CPMEmulator* running_emulator;
  running_emulator = &cpm;
  close_guest_files = [] { running_emulator->close_files_at_exit(); };

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
  cpm.setup_command_line((int)guest_argv.size(), guest_argv.data(), arg_offset);

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

  // The watchdog is the fourth way out, and it is the one where a memory image
  // is worth most: a guest that ran away is exactly the post-mortem
  // --save-memory exists for, and this path used to return 0 without writing
  // one or saying it had not.  Not reachable from the test suite - the limit is
  // nine billion instructions, about eight minutes here - so it is checked by
  // reading rather than by running.
  if (close_guest_files) close_guest_files();
  do_save_memory();
  return 0;
}
