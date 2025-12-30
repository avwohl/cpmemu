/*
 * Windows Platform Implementation
 */

#include "../platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <io.h>
#include <sys/stat.h>
#include <cstdlib>

namespace platform {

// ============================================================================
// Terminal State
// ============================================================================

static HANDLE hStdin = INVALID_HANDLE_VALUE;
static DWORD original_console_mode = 0;
static bool console_mode_saved = false;

void disable_raw_mode() {
    if (console_mode_saved && hStdin != INVALID_HANDLE_VALUE) {
        SetConsoleMode(hStdin, original_console_mode);
        console_mode_saved = false;
    }
}

void enable_raw_mode() {
    if (!is_terminal()) {
        return;
    }

    if (hStdin == INVALID_HANDLE_VALUE) {
        hStdin = GetStdHandle(STD_INPUT_HANDLE);
    }

    if (!console_mode_saved) {
        GetConsoleMode(hStdin, &original_console_mode);
        console_mode_saved = true;
        atexit(disable_raw_mode);
    }

    // Disable line input, echo, and processed input (Ctrl+C handling)
    DWORD raw_mode = original_console_mode;
    raw_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    SetConsoleMode(hStdin, raw_mode);
}

bool is_terminal() {
    return _isatty(_fileno(stdin)) != 0;
}

bool stdin_has_data() {
    if (!is_terminal()) {
        // For non-terminal (pipe/file), check if data is available
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        DWORD available = 0;
        if (PeekNamedPipe(h, NULL, 0, NULL, &available, NULL)) {
            return available > 0;
        }
        return false;
    }

    // For console, use _kbhit()
    return _kbhit() != 0;
}

// ============================================================================
// File System
// ============================================================================

FileType get_file_type(const char* path) {
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return FileType::NotFound;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        return FileType::Directory;
    }
    return FileType::Regular;
}

int64_t get_file_size(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        return -1;
    }
    LARGE_INTEGER size;
    size.HighPart = fad.nFileSizeHigh;
    size.LowPart = fad.nFileSizeLow;
    return static_cast<int64_t>(size.QuadPart);
}

std::vector<DirEntry> list_directory(const char* path) {
    std::vector<DirEntry> entries;

    // Build search pattern
    std::string search_path = std::string(path);
    if (!search_path.empty() && search_path.back() != '\\' && search_path.back() != '/') {
        search_path += '\\';
    }
    search_path += '*';

    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return entries;
    }

    do {
        // Skip . and ..
        if (ffd.cFileName[0] == '.' &&
            (ffd.cFileName[1] == '\0' ||
             (ffd.cFileName[1] == '.' && ffd.cFileName[2] == '\0'))) {
            continue;
        }

        DirEntry de;
        de.name = ffd.cFileName;
        de.is_directory = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entries.push_back(de);
    } while (FindNextFileA(hFind, &ffd) != 0);

    FindClose(hFind);
    return entries;
}

// ============================================================================
// Path Handling
// ============================================================================

char path_separator() {
    return '\\';
}

std::string basename(const std::string& path) {
    // Handle both forward and back slashes
    size_t pos1 = path.find_last_of('/');
    size_t pos2 = path.find_last_of('\\');

    size_t pos;
    if (pos1 == std::string::npos && pos2 == std::string::npos) {
        return path;
    } else if (pos1 == std::string::npos) {
        pos = pos2;
    } else if (pos2 == std::string::npos) {
        pos = pos1;
    } else {
        pos = (pos1 > pos2) ? pos1 : pos2;
    }

    return path.substr(pos + 1);
}

int change_directory(const char* path) {
    return SetCurrentDirectoryA(path) ? 0 : -1;
}

// ============================================================================
// Initialization
// ============================================================================

void init() {
    // Enable virtual terminal processing for ANSI escape codes (Windows 10+)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }
}

void cleanup() {
    disable_raw_mode();
}

} // namespace platform
