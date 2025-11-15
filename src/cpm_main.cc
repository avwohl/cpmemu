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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <fcntl.h>
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

// BIOS/BDOS placement (for 64K system)
#define BIOS_BASE      0xFA00  // BIOS starts here
#define BDOS_BASE      0xEC00  // BDOS starts here
#define CCP_BASE       0xE400  // CCP starts here

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
#define BIOS_LISTST    45  // List status
#define BIOS_SECTRAN   48  // Sector translate

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
    int position;  // Current record position
    bool write_mode;
};

class CPMEmulator {
private:
    qkz80* cpu;
    qkz80_uint8 current_drive;
    qkz80_uint8 current_user;
    qkz80_uint16 current_dma;
    bool debug;

    // File mapping: CP/M filename -> Unix path
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

public:
    // Public debug settings for selective debugging
    std::set<int> debug_bdos_funcs;  // Which BDOS functions to debug
    std::set<int> debug_bios_offsets; // Which BIOS offsets to debug

    // Disk BIOS behavior: 0=ok, 1=fail, 2=error
    int bios_disk_mode;

    CPMEmulator(qkz80* acpu, bool adebug = false)
        : cpu(acpu), current_drive(0), current_user(0),
          current_dma(DEFAULT_DMA), debug(adebug),
          printer_file(nullptr), aux_in_file(nullptr),
          aux_out_file(nullptr), iobyte(0), bios_disk_mode(0) {
    }

    ~CPMEmulator() {
        // Close device files
        if (printer_file) fclose(printer_file);
        if (aux_in_file) fclose(aux_in_file);
        if (aux_out_file) fclose(aux_out_file);
    }

    void setup_memory();
    void setup_command_line(int argc, char** argv);
    void add_file_mapping(const std::string& cpm_name, const std::string& unix_path);
    bool handle_pc(qkz80_uint16 pc);

    // Device redirection
    void set_printer_file(const std::string& path);
    void set_aux_input_file(const std::string& path);
    void set_aux_output_file(const std::string& path);

private:
    // BDOS functions
    void bdos_call(qkz80_uint8 func);
    void bdos_write_console(qkz80_uint8 ch);
    void bdos_write_string();
    void bdos_read_console();
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

    // Helper functions
    std::string fcb_to_filename(qkz80_uint16 fcb_addr);
    void filename_to_fcb(const std::string& filename, qkz80_uint16 fcb_addr);
    std::string find_unix_file(const std::string& cpm_name);
    void read_fcb(qkz80_uint16 addr, FCB* fcb);
    void write_fcb(qkz80_uint16 addr, const FCB* fcb);
    std::string normalize_cpm_filename(const std::string& name);
    bool match_wildcard(const std::string& pattern, const std::string& text);
};

void CPMEmulator::setup_memory() {
    char* mem = cpu->get_mem();

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

    // Set stack pointer
    cpu->regs.SP.set_pair16(0xFFF0);
}

void CPMEmulator::setup_command_line(int argc, char** argv) {
    char* mem = cpu->get_mem();

    if (argc < 2) {
        mem[DEFAULT_DMA] = 0;  // No command line
        return;
    }

    // Build command line from arguments
    // CP/M requires a leading space before the first argument
    // Also, filenames must be in 8.3 format (truncated if needed)
    std::string cmdline;
    for (int i = 2; i < argc; i++) {  // Skip program name
        cmdline += " ";  // Space before each argument (CP/M convention)

        // Get basename and convert to 8.3 format
        const char* arg_base = strrchr(argv[i], '/');
        arg_base = arg_base ? arg_base + 1 : argv[i];
        std::string arg_upper;
        for (const char* p = arg_base; *p; p++) {
            arg_upper += toupper(*p);
        }

        // Truncate to 8.3 format for command line
        size_t dot_pos = arg_upper.find('.');
        if (dot_pos != std::string::npos && dot_pos > 8) {
            // Long filename - truncate to 8.3
            std::string name_83 = arg_upper.substr(0, 8) + arg_upper.substr(dot_pos);
            cmdline += name_83;
        } else {
            cmdline += arg_upper;
        }

        args.push_back(argv[i]);
    }

    // Store command line at DEFAULT_DMA
    mem[DEFAULT_DMA] = std::min((int)cmdline.length(), 127);
    for (size_t i = 0; i < cmdline.length() && i < 127; i++) {
        mem[DEFAULT_DMA + 1 + i] = toupper(cmdline[i]);
    }


    // Parse first filename into DEFAULT_FCB
    if (argc >= 3) {
        filename_to_fcb(argv[2], DEFAULT_FCB);
    }

    // Parse second filename into DEFAULT_FCB2
    if (argc >= 4) {
        filename_to_fcb(argv[3], DEFAULT_FCB2);
    }
}

void CPMEmulator::add_file_mapping(const std::string& cpm_name, const std::string& unix_path) {
    std::string normalized = normalize_cpm_filename(cpm_name);
    file_map[normalized] = unix_path;

    if (debug) {
        fprintf(stderr, "File mapping: '%s' -> '%s'\n", normalized.c_str(), unix_path.c_str());
    }
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

std::string CPMEmulator::fcb_to_filename(qkz80_uint16 fcb_addr) {
    char* mem = cpu->get_mem();
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
    char* mem = cpu->get_mem();

    // Clear FCB
    memset(&mem[fcb_addr], 0, 36);

    // Parse filename
    std::string upper_name;
    for (char c : filename) {
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

    // Fill name field (8 chars, space-padded)
    size_t name_len = (dot_pos != std::string::npos) ? (dot_pos - name_start) : (upper_name.length() - name_start);
    name_len = std::min(name_len, (size_t)8);

    for (size_t i = 0; i < 8; i++) {
        if (i < name_len) {
            mem[fcb_addr + 1 + i] = upper_name[name_start + i];
        } else {
            mem[fcb_addr + 1 + i] = ' ';
        }
    }

    // Fill extension field (3 chars, space-padded)
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
        // No extension
        for (int i = 0; i < 3; i++) {
            mem[fcb_addr + 9 + i] = ' ';
        }
    }
}

std::string CPMEmulator::find_unix_file(const std::string& cpm_name) {
    // First check file mapping
    std::string normalized = normalize_cpm_filename(cpm_name);

    auto it = file_map.find(normalized);
    if (it != file_map.end()) {
        return it->second;
    }

    // Try lowercase version in current directory
    std::string lowercase;
    for (char c : normalized) {
        lowercase += tolower(c);
    }

    // Check if file exists
    if (access(lowercase.c_str(), F_OK) == 0) {
        return lowercase;
    }

    // Try as-is
    if (access(normalized.c_str(), F_OK) == 0) {
        return normalized;
    }

    // Try with ./ prefix
    std::string with_prefix = "./" + lowercase;
    if (access(with_prefix.c_str(), F_OK) == 0) {
        return with_prefix;
    }

    return "";  // Not found
}

bool CPMEmulator::handle_pc(qkz80_uint16 pc) {
    // Check for JMP 0 (exit)
    if (pc == 0) {
        fprintf(stderr, "Program exit via JMP 0\n");
        exit(0);
    }

    // Check for BDOS call
    if (pc == BDOS_ENTRY) {
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
            // TODO: Implement
            cpu->set_reg8(0, qkz80::reg_A);
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

        case 40: // Write Random with Zero Fill
            bdos_write_random_zero_fill();
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
    if (ch == EOF) ch = 0x1A;  // EOF becomes ^Z
    cpu->set_reg8(ch & 0x7F, qkz80::reg_A);
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
    // Simple implementation: always return 0 (no character ready)
    // A real implementation would use select() or similar
    cpu->set_reg8(0x00, qkz80::reg_A);
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
    current_drive = cpu->get_reg8(qkz80::reg_E) & 0x0F;
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
    std::string filename = fcb_to_filename(fcb_addr);
    std::string unix_path = find_unix_file(filename);

    if (debug) {
        fprintf(stderr, "BDOS Open: '%s' -> '%s'\n", filename.c_str(),
                unix_path.empty() ? "(not found)" : unix_path.c_str());
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
    of.position = 0;
    of.write_mode = false;
    open_files[fcb_addr] = of;

    // Clear extent and record count
    char* mem = cpu->get_mem();
    mem[fcb_addr + 12] = 0;  // EX
    mem[fcb_addr + 15] = 0x80;  // RC (128 records max per extent)

    cpu->set_reg8(0, qkz80::reg_A);  // Success
}

void CPMEmulator::bdos_close_file() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);

    auto it = open_files.find(fcb_addr);
    if (it != open_files.end()) {
        fclose(it->second.fp);
        open_files.erase(it);
        cpu->set_reg8(0, qkz80::reg_A);  // Success
    } else {
        cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
    }
}

void CPMEmulator::bdos_read_sequential() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
    char* mem = cpu->get_mem();

    auto it = open_files.find(fcb_addr);
    if (it == open_files.end()) {
        cpu->set_reg8(0xFF, qkz80::reg_A);  // Error: file not open
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

    // Update current record in FCB
    mem[fcb_addr + 32]++;
}

void CPMEmulator::bdos_write_sequential() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
    char* mem = cpu->get_mem();

    auto it = open_files.find(fcb_addr);
    if (it == open_files.end()) {
        // File not open - try to open it for writing
        bdos_open_file();
        it = open_files.find(fcb_addr);
        if (it == open_files.end()) {
            cpu->set_reg8(0xFF, qkz80::reg_A);
            return;
        }
    }

    // Write 128 bytes from DMA
    size_t nwritten = fwrite(&mem[current_dma], 1, 128, it->second.fp);

    if (nwritten == 128) {
        cpu->set_reg8(0, qkz80::reg_A);  // Success
    } else {
        cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
    }

    // Update current record in FCB
    mem[fcb_addr + 32]++;
}

void CPMEmulator::bdos_make_file() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
    std::string filename = fcb_to_filename(fcb_addr);

    if (debug) {
        fprintf(stderr, "Make file: %s\n", filename.c_str());
    }

    // Convert to lowercase
    std::string unix_name;
    for (char c : filename) {
        unix_name += tolower(c);
    }

    FILE* fp = fopen(unix_name.c_str(), "w+b");
    if (!fp) {
        cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
        return;
    }

    OpenFile of;
    of.fp = fp;
    of.unix_path = unix_name;
    of.position = 0;
    of.write_mode = true;
    open_files[fcb_addr] = of;

    char* mem = cpu->get_mem();
    mem[fcb_addr + 12] = 0;  // EX
    mem[fcb_addr + 15] = 0;  // RC

    cpu->set_reg8(0, qkz80::reg_A);  // Success
}

void CPMEmulator::bdos_delete_file() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
    std::string filename = fcb_to_filename(fcb_addr);
    std::string unix_path = find_unix_file(filename);

    if (debug) {
        fprintf(stderr, "Delete file: %s\n", filename.c_str());
    }

    if (unix_path.empty() || unlink(unix_path.c_str()) != 0) {
        cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
    } else {
        cpu->set_reg8(0, qkz80::reg_A);  // Success
    }
}

void CPMEmulator::bdos_read_random() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
    char* mem = cpu->get_mem();

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
    char* mem = cpu->get_mem();

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
    char* mem = cpu->get_mem();
    std::string filename = fcb_to_filename(fcb_addr);
    std::string unix_path = find_unix_file(filename);

    if (unix_path.empty()) {
        cpu->set_reg8(0xFF, qkz80::reg_A);  // Error: file not found
        return;
    }

    struct stat st;
    if (stat(unix_path.c_str(), &st) != 0) {
        cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
        return;
    }

    // File size in 128-byte records (round up)
    uint32_t records = (st.st_size + 127) / 128;

    // Store in FCB bytes 33-35 (r0, r1, r2)
    mem[fcb_addr + 33] = records & 0xFF;
    mem[fcb_addr + 34] = (records >> 8) & 0xFF;
    mem[fcb_addr + 35] = (records >> 16) & 0xFF;

    cpu->set_reg8(0, qkz80::reg_A);  // Success
}

void CPMEmulator::bdos_set_random_record() {
    qkz80_uint16 fcb_addr = cpu->get_reg16(qkz80::regp_DE);
    char* mem = cpu->get_mem();

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

    std::string old_name = fcb_to_filename(fcb_addr);
    std::string old_path = find_unix_file(old_name);

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

    if (rename(old_path.c_str(), new_path.c_str()) != 0) {
        cpu->set_reg8(0xFF, qkz80::reg_A);  // Error
    } else {
        // Update file mapping
        file_map[normalize_cpm_filename(new_name)] = new_path;
        cpu->set_reg8(0, qkz80::reg_A);  // Success
    }
}

void CPMEmulator::bdos_direct_console_io() {
    qkz80_uint8 e_reg = cpu->get_reg8(qkz80::reg_E);

    if (e_reg == 0xFF) {
        // Input mode - return character if available, 0 if not
        // For simplicity, we'll just do blocking input like normal
        int ch = getchar();
        if (ch == EOF) ch = 0;
        cpu->set_reg8(ch & 0x7F, qkz80::reg_A);
    } else if (e_reg == 0xFE) {
        // Status check - return 0xFF if char ready, 0 if not
        // For simplicity, always return 0 (no character ready)
        cpu->set_reg8(0, qkz80::reg_A);
    } else {
        // Output mode - send character
        putchar(e_reg & 0x7F);
        fflush(stdout);
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

    // Reset to drive A, user 0
    current_drive = 0;
    current_user = 0;

    // No return value
}

void CPMEmulator::bdos_search_first() {
    // Simple implementation: return not found
    // A complete implementation would scan the directory
    cpu->set_reg8(0xFF, qkz80::reg_A);
}

void CPMEmulator::bdos_search_next() {
    // Simple implementation: return not found
    cpu->set_reg8(0xFF, qkz80::reg_A);
}

void CPMEmulator::bdos_get_login_vector() {
    // Return bitmap of logged in drives
    // For simplicity, say drive A is logged in
    cpu->set_reg8(0x01, qkz80::reg_L);  // Drive A
    cpu->set_reg8(0x00, qkz80::reg_H);
}

void CPMEmulator::bdos_get_allocation_vector() {
    // Return address of allocation vector
    // Return a dummy address (not actually used by most programs)
    cpu->set_reg8(0x00, qkz80::reg_L);
    cpu->set_reg8(0xF0, qkz80::reg_H);
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
    // Return a dummy address
    cpu->set_reg8(0x00, qkz80::reg_L);
    cpu->set_reg8(0xF1, qkz80::reg_H);
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

        // Disk I/O functions - behavior controlled by bios_disk_mode
        case BIOS_HOME:
        case BIOS_SELDSK:
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
                cpu->set_reg8(0xFF, qkz80::reg_A);  // Return success
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
    // Console status - return 0 (no character waiting)
    cpu->set_reg8(0x00, qkz80::reg_A);
}

void CPMEmulator::bios_conin() {
    // Console input
    int ch = getchar();
    if (ch == EOF) ch = 0x1A;
    cpu->set_reg8(ch & 0x7F, qkz80::reg_A);
}

void CPMEmulator::bios_conout() {
    // Console output - character is in C register
    qkz80_uint8 ch = cpu->get_reg8(qkz80::reg_C);
    putchar(ch & 0x7F);
    fflush(stdout);
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

// Main program
int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program.com> [args...]\n", argv[0]);
        return 1;
    }

    const char* program = argv[1];

    // Create CPU
    qkz80 cpu;

    // Create emulator
    CPMEmulator cpm(&cpu, false);

    // Setup CP/M memory
    cpm.setup_memory();

    // Parse command line arguments
    cpm.setup_command_line(argc, argv);

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
    for (int i = 2; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) == 0 && S_ISREG(st.st_mode)) {
            // Extract basename for CP/M name
            const char* base = strrchr(argv[i], '/');
            base = base ? base + 1 : argv[i];

            // Create uppercase CP/M name (full)
            std::string cpm_name;
            for (const char* p = base; *p; p++) {
                cpm_name += toupper(*p);
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
    FILE* fp = fopen(program, "rb");
    if (!fp) {
        fprintf(stderr, "Cannot open %s: %s\n", program, strerror(errno));
        return 1;
    }

    char* mem = cpu.get_mem();
    size_t loaded = fread(&mem[TPA_START], 1, 0xE000, fp);
    fclose(fp);

    fprintf(stderr, "Loaded %zu bytes from %s\n", loaded, program);

    // Set PC to start of TPA
    cpu.regs.PC.set_pair16(TPA_START);

    // Run
    long long max_instructions = 5000000000LL;  // Safety limit (5B for Zexall/Zexdoc)
    long long instruction_count = 0;
    long long last_report = 0;

    while (true) {
        qkz80_uint16 pc = cpu.regs.PC.get_pair16();

        // Check for CP/M system calls
        if (cpm.handle_pc(pc)) {
            continue;
        }

        // Execute one instruction
        cpu.execute();

        instruction_count++;

        // Progress report every 100M instructions
        if (instruction_count - last_report >= 100000000) {
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
