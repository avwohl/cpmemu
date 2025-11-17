/*
 * CP/M 2.2 Emulator for qkz80 - Enhanced Version
 *
 * Full file I/O support including:
 * - Text/binary mode tracking per file
 * - ^Z EOF handling for text files
 * - EOL conversion (Unix \n <-> CP/M \r\n)
 * - 128-byte record padding
 * - Configuration file support
 * - Wildcard file mapping
 * - File type detection heuristics
 */

#include "qkz80.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <fcntl.h>
#include <fstream>
#include <sstream>

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
#define BIOS_BASE      0xFA00
#define BDOS_BASE      0xEC00
#define CCP_BASE       0xE400

// BIOS function offsets
#define BIOS_BOOT      0
#define BIOS_WBOOT     3
#define BIOS_CONST     6
#define BIOS_CONIN     9
#define BIOS_CONOUT    12
#define BIOS_LIST      15
#define BIOS_PUNCH     18
#define BIOS_READER    21
#define BIOS_HOME      24
#define BIOS_SELDSK    27
#define BIOS_SETTRK    30
#define BIOS_SETSEC    33
#define BIOS_SETDMA    36
#define BIOS_READ      39
#define BIOS_WRITE     42
#define BIOS_LISTST    45
#define BIOS_SECTRAN   48

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

// Open file tracking
struct OpenFile {
    FILE* fp;
    std::string unix_path;
    std::string cpm_name;
    FileMode mode;
    bool eol_convert;
    int position;
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
    qkz80_uint16 current_dma;
    bool debug;
    FileMode default_mode;
    bool default_eol_convert;

    std::vector<FileMapping> file_mappings;
    std::map<qkz80_uint16, OpenFile> open_files;
    std::vector<std::string> args;

public:
    CPMEmulator(qkz80* acpu, bool adebug = false)
        : cpu(acpu), current_drive(0), current_user(0),
          current_dma(DEFAULT_DMA), debug(adebug),
          default_mode(MODE_AUTO), default_eol_convert(true) {
    }

    void setup_memory();
    void setup_command_line(int argc, char** argv, int program_arg_index = 1);
    void add_file_mapping(const std::string& cpm_pattern, const std::string& unix_pattern,
                         FileMode mode = MODE_AUTO, bool eol_convert = true);
    bool load_config_file(const std::string& cfg_path);
    bool handle_pc(qkz80_uint16 pc);

private:
    // File I/O helpers
    FileMode detect_file_mode(const std::string& filename, const std::string& unix_path);
    bool is_text_file_heuristic(const std::string& unix_path);
    std::string find_unix_file(const std::string& cpm_name, FileMode* mode_out, bool* eol_out);
    std::string expand_wildcards(const std::string& pattern, const std::string& filename);

    // EOL and EOF handling
    size_t read_with_conversion(OpenFile& of, uint8_t* buffer, size_t size);
    size_t write_with_conversion(OpenFile& of, const uint8_t* buffer, size_t size);
    void pad_to_128(uint8_t* buffer, size_t actual_size);

    // BDOS functions
    void bdos_call(qkz80_uint8 func);
    void bdos_write_console(qkz80_uint8 ch);
    void bdos_write_string();
    void bdos_read_console();
    void bdos_console_status();
    void bdos_get_version();
    void bdos_get_set_dma();
    void bdos_open_file();
    void bdos_close_file();
    void bdos_read_sequential();
    void bdos_write_sequential();
    void bdos_make_file();
    void bdos_delete_file();
    void bdos_search_first();
    void bdos_search_next();
    void bdos_get_current_drive();
    void bdos_set_drive();
    void bdos_get_set_user();

    // BIOS functions
    void bios_call(int offset);
    void bios_const();
    void bios_conin();
    void bios_conout();

    // Helper functions
    std::string fcb_to_filename(qkz80_uint16 fcb_addr);
    void filename_to_fcb(const std::string& filename, qkz80_uint16 fcb_addr);
    std::string normalize_cpm_filename(const std::string& name);
    bool match_pattern(const std::string& pattern, const std::string& text);
};

// File type detection heuristic
bool CPMEmulator::is_text_file_heuristic(const std::string& unix_path) {
    FILE* fp = fopen(unix_path.c_str(), "rb");
    if (!fp) return false;

    uint8_t buffer[512];
    size_t nread = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);

    if (nread == 0) return true;  // Empty file, treat as text

    // Count control characters (excluding \r, \n, \t, \f, ^Z)
    int control_chars = 0;
    int printable_chars = 0;

    for (size_t i = 0; i < nread; i++) {
        uint8_t ch = buffer[i];
        if (ch == '\r' || ch == '\n' || ch == '\t' || ch == '\f' || ch == 0x1A) {
            printable_chars++;
        } else if (ch < 32 || ch == 127) {
            control_chars++;
        } else if (ch >= 32 && ch < 127) {
            printable_chars++;
        }
    }

    // If more than 5% control characters, probably binary
    if (nread > 20 && control_chars * 20 > nread) {
        return false;
    }

    return true;
}

FileMode CPMEmulator::detect_file_mode(const std::string& filename, const std::string& unix_path) {
    // Check extension
    std::string upper = filename;
    for (char& c : upper) c = toupper(c);

    // Known text extensions
    const char* text_exts[] = {".BAS", ".MAC", ".ASM", ".TXT", ".DOC", ".LST", ".PRN", nullptr};
    for (int i = 0; text_exts[i]; i++) {
        if (upper.find(text_exts[i]) != std::string::npos) {
            return MODE_TEXT;
        }
    }

    // Known binary extensions
    const char* binary_exts[] = {".COM", ".EXE", ".OVL", ".OVR", ".SYS", ".BIN", ".DAT", nullptr};
    for (int i = 0; binary_exts[i]; i++) {
        if (upper.find(binary_exts[i]) != std::string::npos) {
            return MODE_BINARY;
        }
    }

    // Try heuristic
    if (is_text_file_heuristic(unix_path)) {
        return MODE_TEXT;
    }

    return MODE_BINARY;  // Default to binary if unsure
}

std::string CPMEmulator::expand_wildcards(const std::string& pattern, const std::string& filename) {
    // Simple wildcard expansion for finding files
    // Pattern can contain * and ?

    size_t last_slash = pattern.rfind('/');
    std::string dir_part = (last_slash != std::string::npos) ? pattern.substr(0, last_slash + 1) : "";
    std::string file_part = (last_slash != std::string::npos) ? pattern.substr(last_slash + 1) : pattern;

    // If no wildcards, return as-is
    if (file_part.find('*') == std::string::npos && file_part.find('?') == std::string::npos) {
        return pattern;
    }

    // Try matching against lowercase version of filename
    std::string lower_name = filename;
    for (char& c : lower_name) c = tolower(c);

    std::string try_path = dir_part + lower_name;
    if (access(try_path.c_str(), F_OK) == 0) {
        return try_path;
    }

    // Try uppercase
    std::string upper_name = filename;
    for (char& c : upper_name) c = toupper(c);

    try_path = dir_part + upper_name;
    if (access(try_path.c_str(), F_OK) == 0) {
        return try_path;
    }

    return "";
}

std::string CPMEmulator::find_unix_file(const std::string& cpm_name, FileMode* mode_out, bool* eol_out) {
    std::string normalized = normalize_cpm_filename(cpm_name);

    if (debug) {
        fprintf(stderr, "Looking for CP/M file: %s (normalized: %s)\n", cpm_name.c_str(), normalized.c_str());
    }

    // Check file mappings
    for (const auto& mapping : file_mappings) {
        if (match_pattern(mapping.cpm_pattern, normalized)) {
            std::string unix_path = expand_wildcards(mapping.unix_pattern, normalized);
            if (!unix_path.empty() && access(unix_path.c_str(), F_OK) == 0) {
                *mode_out = mapping.mode;
                *eol_out = mapping.eol_convert;

                // Auto-detect if needed
                if (*mode_out == MODE_AUTO) {
                    *mode_out = detect_file_mode(normalized, unix_path);
                }

                if (debug) {
                    fprintf(stderr, "  Found via mapping: %s (mode: %s)\n",
                            unix_path.c_str(), *mode_out == MODE_TEXT ? "text" : "binary");
                }
                return unix_path;
            }
        }
    }

    // Try lowercase in current directory
    std::string lowercase;
    for (char c : normalized) {
        lowercase += tolower(c);
    }

    if (access(lowercase.c_str(), F_OK) == 0) {
        *mode_out = detect_file_mode(normalized, lowercase);
        *eol_out = default_eol_convert;
        if (debug) {
            fprintf(stderr, "  Found local: %s (mode: %s)\n",
                    lowercase.c_str(), *mode_out == MODE_TEXT ? "text" : "binary");
        }
        return lowercase;
    }

    // Try as-is
    if (access(normalized.c_str(), F_OK) == 0) {
        *mode_out = detect_file_mode(normalized, normalized);
        *eol_out = default_eol_convert;
        return normalized;
    }

    if (debug) {
        fprintf(stderr, "  Not found\n");
    }
    return "";
}

bool CPMEmulator::match_pattern(const std::string& pattern, const std::string& text) {
    // Simple wildcard matching
    return fnmatch(pattern.c_str(), text.c_str(), FNM_CASEFOLD) == 0;
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

        return nread;
    }

    // Text mode with EOL conversion: Unix \n -> CP/M \r\n
    size_t out_pos = 0;

    while (out_pos < size) {
        int ch = fgetc(of.fp);

        if (ch == EOF) {
            break;
        }

        if (ch == '\n') {
            // Convert \n to \r\n
            if (out_pos + 1 < size) {
                buffer[out_pos++] = '\r';
                buffer[out_pos++] = '\n';
            } else {
                // Not enough space, put back
                ungetc(ch, of.fp);
                break;
            }
        } else if (ch == CPM_EOF) {
            // EOF marker
            of.eof_seen = true;
            break;
        } else {
            buffer[out_pos++] = (uint8_t)ch;
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
        // Pad with ^Z for text mode, or zeros for binary
        memset(buffer + actual_size, CPM_EOF, 128 - actual_size);
    }
}

void CPMEmulator::add_file_mapping(const std::string& cpm_pattern, const std::string& unix_pattern,
                                   FileMode mode, bool eol_convert) {
    FileMapping mapping;
    mapping.cpm_pattern = cpm_pattern;
    mapping.unix_pattern = unix_pattern;
    mapping.mode = mode;
    mapping.eol_convert = eol_convert;
    file_mappings.push_back(mapping);

    if (debug) {
        fprintf(stderr, "File mapping: '%s' -> '%s' (mode: %s, eol: %s)\n",
                cpm_pattern.c_str(), unix_pattern.c_str(),
                mode == MODE_TEXT ? "text" : mode == MODE_BINARY ? "binary" : "auto",
                eol_convert ? "yes" : "no");
    }
}

bool CPMEmulator::load_config_file(const std::string& cfg_path) {
    std::ifstream cfg(cfg_path);
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
            fprintf(stderr, "Config line %d: invalid format\n", line_num);
            continue;
        }

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim key and value
        key = key.substr(0, key.find_last_not_of(" \t") + 1);
        key = key.substr(key.find_first_not_of(" \t"));
        value = value.substr(value.find_first_not_of(" \t"));
        value = value.substr(0, value.find_last_not_of(" \t") + 1);

        // Parse configuration
        if (key == "program") {
            // Handled by caller
        } else if (key == "default_mode") {
            if (value == "text") default_mode = MODE_TEXT;
            else if (value == "binary") default_mode = MODE_BINARY;
            else default_mode = MODE_AUTO;
        } else if (key == "debug") {
            debug = (value == "true" || value == "1");
        } else if (key == "eol_convert") {
            default_eol_convert = (value == "true" || value == "1");
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

            add_file_mapping(key, value, mode, eol_convert);
        }
    }

    return true;
}

std::string CPMEmulator::normalize_cpm_filename(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (c != ' ') {
            result += toupper(c);
        }
    }
    return result;
}

std::string CPMEmulator::fcb_to_filename(qkz80_uint16 fcb_addr) {
    char* mem = cpu->get_mem();
    std::string filename;

    for (int i = 0; i < 8; i++) {
        char c = mem[fcb_addr + 1 + i] & 0x7F;
        if (c != ' ') filename += c;
    }

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
            if (c != ' ') filename += c;
        }
    }

    return filename;
}

void CPMEmulator::filename_to_fcb(const std::string& filename, qkz80_uint16 fcb_addr) {
    char* mem = cpu->get_mem();
    memset(&mem[fcb_addr], 0, 36);

    std::string upper_name;
    for (char c : filename) {
        upper_name += toupper(c);
    }

    size_t name_start = 0;
    if (upper_name.length() >= 2 && upper_name[1] == ':') {
        char drive = upper_name[0];
        if (drive >= 'A' && drive <= 'P') {
            mem[fcb_addr] = drive - 'A' + 1;
            name_start = 2;
        }
    }

    size_t dot_pos = upper_name.find('.', name_start);
    size_t name_len = (dot_pos != std::string::npos) ? (dot_pos - name_start) : (upper_name.length() - name_start);
    name_len = std::min(name_len, (size_t)8);

    for (size_t i = 0; i < 8; i++) {
        if (i < name_len) {
            mem[fcb_addr + 1 + i] = upper_name[name_start + i];
        } else {
            mem[fcb_addr + 1 + i] = ' ';
        }
    }

    if (dot_pos != std::string::npos) {
        size_t ext_start = dot_pos + 1;
        size_t ext_len = std::min(upper_name.length() - ext_start, (size_t)3);

        for (size_t i = 0; i < 3; i++) {
            if (i < ext_len) {
                mem[fcb_addr + 9 + i] = upper_name[ext_start + i];
            } else {
                mem[fcb_addr + 9 + i] = ' ';
            }
        }
    } else {
        for (int i = 0; i < 3; i++) {
            mem[fcb_addr + 9 + i] = ' ';
        }
    }
}

void CPMEmulator::setup_memory() {
    char* mem = cpu->get_mem();

    mem[0x0000] = 0xC3;
    mem[0x0001] = (BIOS_BASE + BIOS_WBOOT) & 0xFF;
    mem[0x0002] = ((BIOS_BASE + BIOS_WBOOT) >> 8) & 0xFF;

    mem[IOBYTE_ADDR] = 0x00;
    mem[DRVUSER_ADDR] = 0x00;

    mem[BDOS_ENTRY] = 0xC3;
    mem[BDOS_ENTRY + 1] = BDOS_BASE & 0xFF;
    mem[BDOS_ENTRY + 2] = (BDOS_BASE >> 8) & 0xFF;

    qkz80_uint16 bios_magic = 0xFF00;
    for (int i = 0; i < 17; i++) {
        qkz80_uint16 addr = BIOS_BASE + (i * 3);
        mem[addr] = 0xC3;
        mem[addr + 1] = (bios_magic + i) & 0xFF;
        mem[addr + 2] = ((bios_magic + i) >> 8) & 0xFF;
    }

    current_dma = DEFAULT_DMA;
    memset(&mem[DEFAULT_FCB], 0, 36);
    memset(&mem[DEFAULT_FCB2], 0, 20);
    // Set SP to top of available RAM
    // CP/M programs expect stack at high memory
    cpu->regs.SP.set_pair16(0xFFF0);
}

void CPMEmulator::setup_command_line(int argc, char** argv, int program_arg_index) {
    char* mem = cpu->get_mem();

    if (argc < program_arg_index + 1) {
        mem[DEFAULT_DMA] = 0;
        return;
    }

    std::string cmdline;
    for (int i = program_arg_index + 1; i < argc; i++) {
        if (i > program_arg_index + 1) cmdline += " ";
        cmdline += argv[i];
        args.push_back(argv[i]);
    }

    mem[DEFAULT_DMA] = std::min((int)cmdline.length(), 127);
    for (size_t i = 0; i < cmdline.length() && i < 127; i++) {
        mem[DEFAULT_DMA + 1 + i] = toupper(cmdline[i]);
    }

    if (argc >= program_arg_index + 2) {
        filename_to_fcb(argv[program_arg_index + 1], DEFAULT_FCB);
    }

    if (argc >= program_arg_index + 3) {
        filename_to_fcb(argv[program_arg_index + 2], DEFAULT_FCB2);
    }
}

bool CPMEmulator::handle_pc(qkz80_uint16 pc) {
    if (pc == 0) {
        qkz80_uint16 sp = cpu->get_reg16(qkz80::regp_SP);
        fprintf(stderr, "Program exit via JMP 0 (SP=0x%04X)\n", sp);
        exit(0);
    }

    if (pc == BDOS_ENTRY) {
        qkz80_uint8 func = cpu->get_reg8(qkz80::reg_C);
        bdos_call(func);
        qkz80_uint16 ret_addr = cpu->pop_word();
        cpu->regs.PC.set_pair16(ret_addr);
        return true;
    }

    if (pc >= 0xFF00 && pc < 0xFF20) {
        int bios_func = (pc - 0xFF00) * 3;
        bios_call(bios_func);
        qkz80_uint16 ret_addr = cpu->pop_word();
        cpu->regs.PC.set_pair16(ret_addr);
        return true;
    }

    return false;
}

void CPMEmulator::bdos_call(qkz80_uint8 func) {
    if (debug) {
        fprintf(stderr, "BDOS call %d\n", func);
    }

    switch (func) {
        case 0:
            fprintf(stderr, "System reset (BDOS func 0)\n");
            exit(0);
            break;
        case 1:
            bdos_read_console();
            break;
        case 2:
            bdos_write_console(cpu->get_reg8(qkz80::reg_E));
            break;
        case 9:
            bdos_write_string();
            break;
        case 10:
            cpu->set_reg8(0, qkz80::reg_A);
            break;
        case 11:
            bdos_console_status();
            break;
        case 12:
            bdos_get_version();
            break;
        case 14:
            bdos_set_drive();
            break;
        case 15:
            bdos_open_file();
            break;
        case 16:
            bdos_close_file();
            break;
        case 17:
            bdos_search_first();
            break;
        case 18:
            bdos_search_next();
            break;
        case 19:
            bdos_delete_file();
            break;
        case 20:
            bdos_read_sequential();
            break;
        case 21:
            bdos_write_sequential();
            break;
        case 22:
            bdos_make_file();
            break;
        case 25:
            bdos_get_current_drive();
            break;
        case 26:
            bdos_get_set_dma();
            break;
        case 32:
            bdos_get_set_user();
            break;
        default:
            fprintf(stderr, "Unimplemented BDOS function %d\n", func);
            cpu->set_reg8(0xFF, qkz80::reg_A);
            break;
    }
}

void CPMEmulator::bdos_write_console(qkz80_uint8 ch) {
    putchar(ch & 0x7F);
    fflush(stdout);
}

void CPMEmulator::bdos_write_string() {
    qkz80_uint16 addr = cpu->get_reg16(qkz80::regp_DE);
    char* mem = cpu->get_mem();

    while (mem[addr] != '$') {
        putchar(mem[addr] & 0x7F);
        addr++;
    }
    fflush(stdout);
}

void CPMEmulator::bdos_read_console() {
    int ch = getchar();
    if (ch == EOF) ch = 0x1A;
    cpu->set_reg8(ch & 0x7F, qkz80::reg_A);
}

void CPMEmulator::bdos_console_status() {
    cpu->set_reg8(0x00, qkz80::reg_A);
}

void CPMEmulator::bdos_get_version() {
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
    current_drive = cpu->get_reg8(qkz80::reg_E) & 0x0F;
    if (debug) {
        fprintf(stderr, "Set drive to %c:\n", 'A' + current_drive);
    }
}

void CPMEmulator::bdos_get_set_user() {
    qkz80_uint8 code = cpu->get_reg8(qkz80::reg_E);

    if (code == 0xFF) {
        cpu->set_reg8(current_user, qkz80::reg_A);
    } else {
        current_user = code & 0x0F;
    }
}

void CPMEmulator::bdos_open_file() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
    std::string filename = fcb_to_filename(fcb_addr);

    FileMode mode;
    bool eol_convert;
    std::string unix_path = find_unix_file(filename, &mode, &eol_convert);

    if (debug) {
        fprintf(stderr, "Open file: %s -> %s (mode: %s)\n",
                filename.c_str(), unix_path.c_str(),
                mode == MODE_TEXT ? "text" : "binary");
    }

    if (unix_path.empty()) {
        cpu->set_reg8(0xFF, qkz80::reg_A);
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

    char* mem = cpu->get_mem();
    mem[fcb_addr + 12] = 0;
    mem[fcb_addr + 15] = 0x80;

    cpu->set_reg8(0, qkz80::reg_A);
}

void CPMEmulator::bdos_close_file() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);

    auto it = open_files.find(fcb_addr);
    if (it != open_files.end()) {
        // Flush any pending writes
        if (it->second.write_mode && it->second.write_buffer.size() > 0) {
            write_with_conversion(it->second, it->second.write_buffer.data(),
                                it->second.write_buffer.size());
        }

        fclose(it->second.fp);
        open_files.erase(it);
        cpu->set_reg8(0, qkz80::reg_A);
    } else {
        cpu->set_reg8(0xFF, qkz80::reg_A);
    }
}

void CPMEmulator::bdos_read_sequential() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
    char* mem = cpu->get_mem();

    auto it = open_files.find(fcb_addr);
    if (it == open_files.end()) {
        cpu->set_reg8(0xFF, qkz80::reg_A);
        return;
    }

    uint8_t buffer[128];
    size_t nread = read_with_conversion(it->second, buffer, 128);

    if (nread == 0 || it->second.eof_seen) {
        cpu->set_reg8(1, qkz80::reg_A);  // EOF
    } else {
        // Pad to 128 bytes
        if (nread < 128) {
            pad_to_128(buffer, nread);
        }

        // Copy to DMA
        memcpy(&mem[current_dma], buffer, 128);
        cpu->set_reg8(0, qkz80::reg_A);  // Success
    }

    mem[fcb_addr + 32]++;  // Increment current record
}

void CPMEmulator::bdos_write_sequential() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
    char* mem = cpu->get_mem();

    auto it = open_files.find(fcb_addr);
    if (it == open_files.end()) {
        bdos_open_file();
        it = open_files.find(fcb_addr);
        if (it == open_files.end()) {
            cpu->set_reg8(0xFF, qkz80::reg_A);
            return;
        }
    }

    it->second.write_mode = true;

    // Write 128 bytes from DMA
    size_t nwritten = write_with_conversion(it->second, (uint8_t*)&mem[current_dma], 128);

    if (nwritten > 0) {
        cpu->set_reg8(0, qkz80::reg_A);  // Success
    } else {
        cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
    }

    mem[fcb_addr + 32]++;  // Increment current record
}

void CPMEmulator::bdos_make_file() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
    std::string filename = fcb_to_filename(fcb_addr);

    if (debug) {
        fprintf(stderr, "Make file: %s\n", filename.c_str());
    }

    std::string unix_name;
    for (char c : filename) {
        unix_name += tolower(c);
    }

    FILE* fp = fopen(unix_name.c_str(), "w+b");
    if (!fp) {
        cpu->set_reg8(0xFF, qkz80::reg_A);
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

    char* mem = cpu->get_mem();
    mem[fcb_addr + 12] = 0;
    mem[fcb_addr + 15] = 0;

    cpu->set_reg8(0, qkz80::reg_A);
}

void CPMEmulator::bdos_delete_file() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
    std::string filename = fcb_to_filename(fcb_addr);

    FileMode mode;
    bool eol_convert;
    std::string unix_path = find_unix_file(filename, &mode, &eol_convert);

    if (debug) {
        fprintf(stderr, "Delete file: %s\n", filename.c_str());
    }

    if (unix_path.empty() || unlink(unix_path.c_str()) != 0) {
        cpu->set_reg8(0xFF, qkz80::reg_A);
    } else {
        cpu->set_reg8(0, qkz80::reg_A);
    }
}

void CPMEmulator::bdos_search_first() {
    cpu->set_reg8(0xFF, qkz80::reg_A);
}

void CPMEmulator::bdos_search_next() {
    cpu->set_reg8(0xFF, qkz80::reg_A);
}

void CPMEmulator::bios_call(int offset) {
    if (debug) {
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
        case BIOS_WBOOT:
            fprintf(stderr, "BIOS WBOOT called - exiting\n");
            exit(0);
            break;
        default:
            if (debug) {
                fprintf(stderr, "Unimplemented BIOS function at offset %d\n", offset);
            }
            break;
    }
}

void CPMEmulator::bios_const() {
    cpu->set_reg8(0x00, qkz80::reg_A);
}

void CPMEmulator::bios_conin() {
    int ch = getchar();
    if (ch == EOF) ch = 0x1A;
    cpu->set_reg8(ch & 0x7F, qkz80::reg_A);
}

void CPMEmulator::bios_conout() {
    qkz80_uint8 ch = cpu->get_reg8(qkz80::reg_C);
    putchar(ch & 0x7F);
    fflush(stdout);
}

// Main program
int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [--8080|--z80] <program.com|config.cfg> [args...]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  --8080              Run in 8080 mode (default: Z80)\n");
        fprintf(stderr, "  --z80               Run in Z80 mode\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s mbasic.com                    # Run MBASIC in Z80 mode\n", argv[0]);
        fprintf(stderr, "  %s --8080 8080exer.com          # Run 8080 test in 8080 mode\n", argv[0]);
        fprintf(stderr, "  %s mbasic.com test.bas          # With argument\n", argv[0]);
        fprintf(stderr, "  %s mbasic_tests.cfg             # With config file\n", argv[0]);
        return 1;
    }

    // Parse command line for CPU mode
    int arg_offset = 1;
    bool mode_8080 = false;

    if (strcmp(argv[1], "--8080") == 0) {
        mode_8080 = true;
        arg_offset = 2;
    } else if (strcmp(argv[1], "--z80") == 0) {
        mode_8080 = false;
        arg_offset = 2;
    }

    if (argc < arg_offset + 1) {
        fprintf(stderr, "Error: No program specified\n");
        fprintf(stderr, "Usage: %s [--8080|--z80] <program.com|config.cfg> [args...]\n", argv[0]);
        return 1;
    }

    const char* arg1 = argv[arg_offset];
    bool is_config = (strstr(arg1, ".cfg") != nullptr);
    const char* program = nullptr;

    qkz80 cpu;
    cpu.set_cpu_mode(mode_8080 ? qkz80::MODE_8080 : qkz80::MODE_Z80);
    fprintf(stderr, "CPU mode: %s\n", mode_8080 ? "8080" : "Z80");
    CPMEmulator cpm(&cpu, false);

    if (is_config) {
        // Load configuration file
        if (!cpm.load_config_file(arg1)) {
            return 1;
        }

        // Find program directive in config
        std::ifstream cfg(arg1);
        std::string line;
        while (std::getline(cfg, line)) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string value = line.substr(eq + 1);

                // Trim
                key = key.substr(key.find_first_not_of(" \t"));
                key = key.substr(0, key.find_last_not_of(" \t") + 1);
                value = value.substr(value.find_first_not_of(" \t"));
                value = value.substr(0, value.find_last_not_of(" \t") + 1);

                if (key == "program") {
                    program = strdup(value.c_str());
                    break;
                }
            }
        }

        if (!program) {
            fprintf(stderr, "No 'program' directive in config file\n");
            return 1;
        }
    } else {
        program = arg1;
    }

    cpm.setup_memory();
    cpm.setup_command_line(argc, argv, arg_offset);

    // Setup file mappings from command line
    for (int i = arg_offset + 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) == 0 && S_ISREG(st.st_mode)) {
            const char* base = strrchr(argv[i], '/');
            base = base ? base + 1 : argv[i];

            std::string cpm_name;
            for (const char* p = base; *p; p++) {
                cpm_name += toupper(*p);
            }

            cpm.add_file_mapping(cpm_name, argv[i]);
        }
    }

    // Load .COM file
    FILE* fp = fopen(program, "rb");
    if (!fp) {
        fprintf(stderr, "Cannot open %s: %s\n", program, strerror(errno));
        return 1;
    }

    char* mem = cpu.get_mem();
    size_t loaded = fread(&mem[TPA_START], 1, 0xE000, fp);
    fclose(fp);

    fprintf(stderr, "Loaded %zu bytes from %s\n", loaded, program);

    cpu.regs.PC.set_pair16(TPA_START);

    // Check for debug mode via environment variable
    static bool debug_trace = (getenv("QKZ80_DEBUG") != nullptr);
    static int instr_count = 0;

    // Run without instruction limit
    while (true) {
        qkz80_uint16 pc = cpu.regs.PC.get_pair16();

        if (cpm.handle_pc(pc)) {
            continue;
        }

        // Debug trace before instruction
        if (debug_trace && pc >= 0x100 && pc < 0x200) {
            char label[32];
            snprintf(label, sizeof(label), "BEFORE[%d]:", instr_count);
            cpu.debug_dump_regs(label);
        }

        cpu.execute();
        instr_count++;

        // Debug trace after instruction
        if (debug_trace && cpu.regs.PC.get_pair16() >= 0x100 && cpu.regs.PC.get_pair16() < 0x200) {
            char label[32];
            snprintf(label, sizeof(label), "AFTER [%d]:", instr_count - 1);
            cpu.debug_dump_regs(label);
        }
    }

    return 0;
}
