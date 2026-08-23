/*
 * Linux/POSIX Platform Implementation
 */

#include "../platform.h"

#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cerrno>
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
    // These are the same clears cfmakeraw() performs, spelled out by hand.
    // Not for visibility reasons - g++ predefines _GNU_SOURCE whatever -std
    // says, so cfmakeraw is declared here - but because we want one clear fewer
    // than it makes, and that is easier to see written out than subtracted
    // afterwards.  See the c_cflag note below for which clear and why.
    // Disable canonical mode (line buffering), echo, and signal generation
    // ISIG disabled so ^C passes through to CP/M program instead of killing emulator
    // IEXTEN disabled so the line discipline stops eating VLNEXT (^V) and
    // VDISCARD (^O).  On Linux IEXTEN is inert once ICANON is off, but BSD/XNU
    // gates VLNEXT and VDISCARD on IEXTEN alone, so without this the macOS build
    // loses ^V and ^O before the guest ever sees them.
    raw.c_lflag &= ~(ICANON | ECHO | ECHONL | IEXTEN | ISIG);
    // Disable input processing (break handling, parity marking, CR-to-NL,
    // XON/XOFF).  ISTRIP disabled so the 8th bit of each byte survives.
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    // Disable output processing so escape sequences pass through unmodified
    raw.c_oflag &= ~(OPOST);
    // The one clear cfmakeraw() also makes and we deliberately skip is
    // c_cflag CSIZE/PARENB/CS8.  c_cflag is inert on a pty or a pipe, and on a
    // real serial console it reprograms the line itself - forcing 8N1 on a
    // terminal the user brought up 7E1 garbles the whole session.  Clearing
    // ISTRIP above is what actually keeps the 8th bit, which is all we need.
    // Set minimum characters to 1 and timeout to 0
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

bool is_terminal() {
    return isatty(STDIN_FILENO) != 0;
}

bool stdin_has_data() {
    // For non-interactive use (pipes, /dev/null), don't report data available
    // This prevents CP/M programs from checking for user abort when running batch
    if (!is_terminal()) {
        return false;
    }
    fd_set readfds;
    struct timeval tv;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0;
}

// Read through read(2) rather than getchar(), so that stdin_has_data() above
// stays truthful.  select() reports what the kernel holds; a stdio buffer
// between the two keeps bytes 2..N of a burst where select() cannot see them,
// and every status call answers "no character" while the input sits waiting.
// Bursts are ordinary: a paste, a fast typist, and every escape sequence - a
// POSIX arrow key is ESC [ A, so a program polling BDOS 6 would take the ESC
// and then stall with "[A" stuck in the buffer until the next keystroke.
// One byte per read leaves the kernel as the only place input is queued.
int console_getchar() {
    for (;;) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 1) return c;
        if (n == 0) return -1;          // EOF
        if (errno == EINTR) continue;   // a signal, not an error
        return -1;
    }
}

bool console_last_char_synthesized() {
    // POSIX terminals hand us the raw bytes, so nothing is ever synthesized here
    return false;
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
