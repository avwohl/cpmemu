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

// The raw settings themselves, computed once by enable_raw_mode().  A resume
// has to put these back from inside a signal handler, where recomputing them
// is not on: tcgetattr would read whatever the shell just left on the terminal
// rather than the terminal we started with.
static struct termios raw_termios;
static bool raw_wanted = false;      // raw mode is what this run should be in

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
//
// Split in two because a suspend needs the first half without the second: the
// shell has to get a usable terminal while we are stopped, but this run is
// still meant to be raw when it comes back.  disable_raw_mode() is the way out
// for good and says so by clearing raw_wanted, so a SIGCONT arriving after the
// atexit handler cannot re-raw a terminal we have already handed over.
//
// The restore is not gated on raw_active, and that is the fix to a real bug
// rather than tidiness.  apply_raw_mode() can only set that flag after its
// tcsetattr returns, while the settings are live in the kernel from the moment
// the call completes there - so a signal delivered in the gap between the two
// finds raw_active false, skips the restore, and leaves the terminal raw with
// nothing left to put it back.  The gap is a few instructions wide, which makes
// it rare rather than impossible: measured, one failure in 120 runs of the
// seven kill cases in tests/pty_console.cc with 64 spinning processes on a
// 2-core box, reported as "the terminal was not put back on exit" on the SIGQUIT
// case.  Widening the gap to 50ms on purpose turns that into all seven failing
// every time, and this line is what turns it back into all seven passing with
// the 50ms still there.
//
// termios_saved is the right guard instead: it is set immediately after the
// tcgetattr that filled original_termios and never cleared, so it means "there
// is a terminal to put back", which is exactly the question.  Restoring one
// that was never made raw writes back the settings already on it.
static void unapply_raw_mode() {
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
        raw_active = false;
    }
}

// The other half: put back the settings enable_raw_mode() computed, rather
// than recomputing them.  A resume runs this from a signal handler, where a
// fresh tcgetattr would read whatever the shell just wrote to the terminal
// instead of the terminal we were started on.  tcsetattr is on the list of
// functions a handler may call, which is what makes doing it from here legal.
static void apply_raw_mode() {
    if (!raw_wanted || raw_active) {
        return;
    }
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios) == 0) {
        raw_active = true;
    }
}

void disable_raw_mode() {
    unapply_raw_mode();
    raw_wanted = false;
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

// Suspend and resume.
//
// enable_raw_mode() ran once, at startup, and nothing re-applied it.  Both bash
// and zsh write their own termios back to the terminal when they take a job
// into the foreground, so a stop and an fg left the emulator running against a
// canonical terminal for the rest of its life: the guest saw nothing until CR
// and then got the whole line at once, and ISIG being on again meant the next
// ^Z stopped it rather than reaching the guest as 1A.
//
// The terminal has to go back before the process stops, not after it resumes,
// or the shell gets its prompt on a terminal with no echo and no line editing.
// Restoring, then re-raising with the default disposition, is what makes the
// process actually stop rather than carry on from a handler that returned - and
// the unblock is not optional: signal() gives BSD semantics, which block the
// signal for the duration of its own handler, so a bare raise() here would only
// mark it pending and the emulator would keep running.
static void suspend_for_shell(int sig) {
    int saved_errno = errno;
    sigset_t just_this, previous;

    unapply_raw_mode();
    signal(sig, SIG_DFL);
    sigemptyset(&just_this);
    sigaddset(&just_this, sig);
    sigprocmask(SIG_UNBLOCK, &just_this, &previous);
    raise(sig);                                 // stops here until SIGCONT
    sigprocmask(SIG_SETMASK, &previous, NULL);
    signal(sig, suspend_for_shell);

    errno = saved_errno;
}

// The half that does the work, and deliberately not folded into the handler
// above: SIGSTOP cannot be caught at all, and SIGTTIN and SIGTTOU stop the
// process without going anywhere near SIGTSTP, so a resume from any of those
// reaches only here.  SIGTTOU is not hypothetical: `cpmemu prog.com &` puts the
// tcsetattr in enable_raw_mode() in a background process group, where the
// terminal driver stops the process rather than letting the call through, and
// the fg that follows resumes it here.  Linux restarts the interrupted ioctl at
// that point and completes it with no handler involved, so on Linux this is
// belt and braces; a platform that returns EINTR instead needs it.
//
// One thing this cannot fix, and it is worth being plain about: a shell that
// writes the terminal *after* sending SIGCONT is racing this handler, and
// nothing here can win that race.  bash and zsh both hand the terminal over
// first, which is the order tests/pty_console.cc reproduces.
static void resume_from_shell(int sig) {
    int saved_errno = errno;
    (void)sig;
    apply_raw_mode();
    errno = saved_errno;
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
        // Same rule for the job control pair: a shell with job control switched
        // off ignores SIGTSTP on its children's behalf, and re-arming it here
        // would stop a process that was meant to be unstoppable.
        if (signal(SIGTSTP, suspend_for_shell) == SIG_IGN) {
            signal(SIGTSTP, SIG_IGN);
        }
        if (signal(SIGCONT, resume_from_shell) == SIG_IGN) {
            signal(SIGCONT, SIG_IGN);
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
    // XON/XOFF).  ISTRIP disabled so the 8th bit of each byte survives the line
    // discipline: measured, 128 of 128 high bytes reach read() unchanged.  That
    // is as far as this layer goes, and it is not the same as the guest seeing
    // the bit - see the note below.
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    // Disable output processing so escape sequences pass through unmodified
    raw.c_oflag &= ~(OPOST);
    // The one clear cfmakeraw() also makes and we deliberately skip is
    // c_cflag CSIZE/PARENB/CS8.  c_cflag is inert on a pty or a pipe, and on a
    // real serial console it reprograms the line itself - forcing 8N1 on a
    // terminal the user brought up 7E1 garbles the whole session.  Clearing
    // ISTRIP above is what actually keeps the 8th bit through the driver, and
    // it is all this layer needs.
    //
    // It is not all the emulator needs, and this comment used to imply it was.
    // All four console read sites in cpmemu.cc then mask with & 0x7F -
    // cpmemu.cc:1591 (BDOS 1), :1771 (BDOS 10 buffer store), :2361 (BDOS 6) and
    // :2799 (BIOS CONIN) - so a high byte that arrives here intact reaches the
    // CP/M program with its top bit gone.  Measured: the UTF-8 bytes for
    // e-acute, alpha, pound and em-dash give the guest C3 29 4E 31 42 23 62 00
    // 14 after masking, and on the polled path a byte that masks to 0x00 is
    // dropped outright, because BDOS 6 reads 0 as "no character".
    //
    // Whether CP/M should see eight bits is a real question and it is not
    // settled here; it is open in todo.txt.  What this clear buys is that
    // settling it either way needs no change to this layer.
    // Set minimum characters to 1 and timeout to 0
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    // TCSAFLUSH here used to throw away whatever the user had already typed:
    // measured on macOS, bytes typed at the shell before the emulator reached
    // this line were echoed, then discarded, and the guest never saw them.
    //
    // raw_wanted is set before the tcsetattr rather than after it, because the
    // tcsetattr is exactly where a background start is stopped by SIGTTOU: the
    // resume handler runs while this call is still outstanding, and it has to
    // find a run that already knows it is supposed to be raw.
    raw_termios = raw;
    raw_wanted = true;
    apply_raw_mode();
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
