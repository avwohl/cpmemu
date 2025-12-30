/*
 * Linux/POSIX Platform Implementation
 */

#include "../platform.h"

#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdlib>
#include <cstring>

namespace platform {

// ============================================================================
// Terminal State
// ============================================================================

static struct termios original_termios;
static bool termios_saved = false;

void disable_raw_mode() {
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
        termios_saved = false;
    }
}

void enable_raw_mode() {
    if (!is_terminal()) {
        return;
    }

    if (!termios_saved) {
        tcgetattr(STDIN_FILENO, &original_termios);
        termios_saved = true;
        atexit(disable_raw_mode);
    }

    struct termios raw = original_termios;
    // Disable canonical mode (line buffering), echo, and signal generation
    // ISIG disabled so ^C passes through to CP/M program instead of killing emulator
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    // Set minimum characters to 1 and timeout to 0
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

bool is_terminal() {
    return isatty(STDIN_FILENO) != 0;
}

bool stdin_has_data() {
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0;
}

int console_getchar() {
    int ch = getchar();
    if (ch == EOF) return -1;
    return ch;
}

// ============================================================================
// File System
// ============================================================================

FileType get_file_type(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return FileType::NotFound;
    }
    if (S_ISREG(st.st_mode)) {
        return FileType::Regular;
    }
    if (S_ISDIR(st.st_mode)) {
        return FileType::Directory;
    }
    return FileType::Other;
}

int64_t get_file_size(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return static_cast<int64_t>(st.st_size);
}

bool delete_file(const char* path) {
    return unlink(path) == 0;
}

std::vector<DirEntry> list_directory(const char* path) {
    std::vector<DirEntry> entries;

    DIR* dir = opendir(path);
    if (!dir) {
        return entries;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and ..
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }

        DirEntry de;
        de.name = entry->d_name;

        // Determine if it's a directory
        // Some systems have d_type, but for portability we use stat
        std::string full_path = std::string(path) + "/" + entry->d_name;
        de.is_directory = (get_file_type(full_path.c_str()) == FileType::Directory);

        entries.push_back(de);
    }

    closedir(dir);
    return entries;
}

// ============================================================================
// Path Handling
// ============================================================================

char path_separator() {
    return '/';
}

std::string basename(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

int change_directory(const char* path) {
    return chdir(path);
}

// ============================================================================
// Initialization
// ============================================================================

void init() {
    // Nothing special needed for Linux
}

void cleanup() {
    disable_raw_mode();
}

} // namespace platform
