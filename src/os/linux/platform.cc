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
#include <csignal>
#include <cstdlib>
#include <cstring>

namespace platform {

// ============================================================================
// Terminal State
// ============================================================================

// The terminal as we found it, and whether we are currently the ones changing
// it.  Two flags rather than one: original_termios has to outlive a
// disable_raw_mode(), or a second enable_raw_mode() would snapshot a terminal
// we had already made raw and "restore" the shell to that afterwards.
static struct termios original_termios;
static bool termios_saved = false;   // original_termios holds the real thing
static bool raw_active = false;      // and we have replaced it

// TCSANOW rather than TCSAFLUSH here and below, for two measured reasons.
//
// TCSAFLUSH discards the input queue.  On the way out that means anything the
// guest never read is thrown away instead of being handed back to the shell:
// measured on macOS, typing `.QR' at a guest that stops on `.' left the shell
// with nothing under TCSAFLUSH and with `QR' under TCSANOW.  On the way in it
// means type-ahead is lost - bytes typed at the prompt before the emulator got
// here were echoed by the line discipline and then destroyed.
//
// TCSAFLUSH also waits for queued output to drain first, and on a terminal
// stopped by ^S, or one whose reader has gone quiet, it never does: measured,
// the emulator sat in tcsetattr indefinitely before the guest had even
// started, which from outside is indistinguishable from a hang.  TCSADRAIN
// waits the same way.  TCSANOW cannot block.
//
// One visible consequence of not flushing: handing a terminal back with input
// still queued sets PENDIN in its lflag, because the line discipline has raw
// bytes to reprocess now that canonical mode is back.  That bit is the
// driver's bookkeeping, not ours, and it clears itself on the next read.
void disable_raw_mode() {
    if (raw_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
        raw_active = false;
    }
}

// Signals that end the process without running atexit(), which leaves the
// terminal exactly as we set it: no echo, no line editing, and a shell the user
// has to reset by hand.  Measured on macOS, every one of these left a pty raw
// before this existed.  It is the POSIX face of the same problem ctrl+break has
// on Windows.
//
// ISIG is cleared below, so none of the first four can come from the keyboard -
// they arrive from a kill, from a terminal window closing, or from an ssh
// session dropping.  The last three are crashes, and are here because the user
// is no less stuck for the emulator having been the one at fault.
static const int restore_signals[] = {
    SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGSEGV, SIGBUS, SIGABRT
};

// tcsetattr() is one of the functions POSIX allows a signal handler to call, so
// putting the terminal back from here is safe.  Nothing else is done: no
// buffered output is flushed and no memory is saved, because neither is safe
// here.  The handler restores the default disposition and re-raises, so the
// process still dies of the signal it was sent rather than reporting a tidy
// exit, and a caller waiting on it sees the truth.
static void restore_terminal_and_die(int sig) {
    disable_raw_mode();
    signal(sig, SIG_DFL);
    raise(sig);
}

void enable_raw_mode() {
    if (!is_terminal()) {
        return;
    }

    if (!termios_saved) {
        // Only claim to have a terminal to restore if we actually read one.
        // Going ahead regardless would leave original_termios zeroed, and
        // pushing that back at exit asks for speed B0, which hangs up a real
        // serial line.
        if (tcgetattr(STDIN_FILENO, &original_termios) != 0) {
            return;
        }
        termios_saved = true;
        atexit(disable_raw_mode);
        for (size_t i = 0; i < sizeof(restore_signals) / sizeof(restore_signals[0]); i++) {
            // Anything already ignored stays ignored.  A shell that starts us
            // in the background, or a nohup, ignores these on purpose, and
            // putting a terminal back is not worth overriding that.
            if (signal(restore_signals[i], restore_terminal_and_die) == SIG_IGN) {
                signal(restore_signals[i], SIG_IGN);
            }
        }
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
    // TCSAFLUSH here used to throw away whatever the user had already typed:
    // measured on macOS, bytes typed at the shell before the emulator reached
    // this line were echoed, then discarded, and the guest never saw them.
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
        raw_active = true;
    }
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
