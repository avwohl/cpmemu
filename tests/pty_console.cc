/*
 * POSIX console tests for cpmemu.
 *
 * The POSIX half of os/linux/platform.cc cannot be tested through a pipe.
 * enable_raw_mode() returns immediately when is_terminal() is false, so a
 * redirected run never touches termios at all: every mask, every restore and
 * every signal path below is invisible to a pipe, and a build that compiles
 * proves only that it compiles.  The one exception is stdin_has_data(), which
 * answers for a file and a pipe as well as for a tty; the three polled
 * redirected cases below are the ones that reach it.
 *
 * So this drives a real terminal.  It allocates a pty, gives the slave to
 * cpmemu as stdin, writes the bytes a keyboard would send into the master, and
 * captures the guest's stdout and stderr through pipes so the comparison is
 * against the guest's output alone.  It is the POSIX counterpart of
 * tests/win_console.cc and runs the same guest programs, so a case named the
 * same on both platforms can be compared byte for byte.
 *
 * The case this exists for: BSD/XNU gates VLNEXT (^V) and VDISCARD (^O) on
 * IEXTEN outside the ICANON block, so without the IEXTEN clear in
 * enable_raw_mode() the line discipline eats those two bytes before the guest
 * can see them.  Linux does not - IEXTEN is inert there once ICANON is off -
 * which is why a Linux pty could never prove that clear was load-bearing.
 * Measured on macOS 27 (arm64): with the mask as written every byte 0x01..0x1F
 * and 0x7F reaches the guest; with IEXTEN left set, 0x0F and 0x16 vanish and
 * nothing else changes.  The first case below is a control that re-measures
 * this on whatever machine is running, so a pass here always means something.
 *
 * Two deliberate departures from a real login session, both so the harness can
 * see what it is testing:
 *
 *   - the pty is not made the child's controlling terminal.  Nothing in the
 *     case table needs one (ISIG is cleared, so no key raises a signal), and
 *     leaving it out keeps a hangup from killing the guest before it can
 *     report.  The two job control cases at the end are the exception and say
 *     so: a stop and an fg cannot be staged without one, and a child in an
 *     orphaned process group has SIGTSTP discarded rather than delivered, so
 *     they build a session and a stand-in shell of their own.
 *
 *   - end of input is delivered through a file, a pipe or /dev/null rather
 *     than by closing the master.  Closing it does work - measured on macOS,
 *     once no master fd is left open anywhere, select() reports the slave
 *     readable and read() returns 0 - but a file, a pipe and /dev/null are
 *     what a redirected run actually uses, and they are the cases nothing else
 *     on POSIX covers without an assembler.  The one thing to watch if that
 *     ever changes: a master fd surviving into the child keeps the terminal
 *     alive, and the reader then blocks forever with no EOF and no error,
 *     which looks exactly like a platform that cannot report a hangup.  That
 *     is why the child below closes it.
 *
 * Build and run:
 *     c++ -std=c++11 -Wall -Wextra -o pty_console tests/pty_console.cc
 *     ./pty_console path/to/cpmemu
 *     ./pty_console --manual path/to/cpmemu     (type keys yourself)
 * tests/run_tests.sh builds and runs the first form on any POSIX host.  The
 * second is for a person, and nothing automatic can stand in for it: writing
 * into a pty steps past the terminal program, so a key Terminal.app or iTerm
 * has bound for itself passes here and is still lost in real use.
 */

#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <string>
#include <vector>

// The CP/M guest programs are shared with tests/win_console.cc so the two
// harnesses run the same code and report comparable byte strings.
#include "con_guests.h"

// ============================================================================
// Small helpers
// ============================================================================

static long long now_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

// Read whatever is waiting without blocking.  Every fd handed here is
// non-blocking, so EAGAIN means "nothing more right now" and not an error.
static void drain(int fd, std::string& into) {
    if (fd < 0) return;
    for (;;) {
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof buf);
        if (n > 0) { into.append(buf, (size_t)n); continue; }
        if (n < 0 && errno == EINTR) continue;
        return;
    }
}

// Write every byte, even if the tty input queue makes us wait for room
static bool write_all(int fd, const char* data, size_t len) {
    while (len) {
        ssize_t n = write(fd, data, len);
        if (n > 0) { data += n; len -= (size_t)n; continue; }
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) { usleep(1000); continue; }
        return false;
    }
    return true;
}

// Render a string with control characters visible, so a CR/LF mismatch is
// readable in the failure output instead of invisible.
static std::string show(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\r') out += "\\r";
        else if (c == '\n') out += "\\n";
        else if (c < 0x20 || c >= 0x7F) {
            char buf[8];
            snprintf(buf, sizeof buf, "\\x%02X", c);
            out += buf;
        } else out += (char)c;
    }
    return out;
}

static bool write_file(const std::string& path, const void* data, size_t len) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = (len == 0) || fwrite(data, 1, len, f) == len;
    fclose(f);
    return ok;
}

// ============================================================================
// The raw mode under test
//
// The same clears os/linux/platform.cc enable_raw_mode() makes, kept here so
// the control case below can apply them to a terminal of its own and measure
// what each one is worth on the machine running the tests.  If that function
// changes, this has to change with it - which is the point: the control then
// reports what the new mask loses.
// ============================================================================

static void apply_cpmemu_raw_mode(int fd, bool clear_iexten) {
    struct termios raw;
    if (tcgetattr(fd, &raw) != 0) return;
    raw.c_lflag &= ~(ICANON | ECHO | ECHONL | ISIG);
    if (clear_iexten) raw.c_lflag &= ~IEXTEN;
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSAFLUSH, &raw);
}

// ============================================================================
// Pty plumbing
// ============================================================================

struct Pty {
    int master;
    int slave;
    std::string name;
};

static bool pty_open(Pty& p) {
    p.master = posix_openpt(O_RDWR | O_NOCTTY);
    if (p.master < 0) return false;
    if (grantpt(p.master) != 0 || unlockpt(p.master) != 0) { close(p.master); return false; }
    const char* n = ptsname(p.master);
    if (!n) { close(p.master); return false; }
    p.name = n;
    // The parent keeps a slave fd open purely to read the terminal's settings:
    // tcgetattr() on the master fails until some slave exists, and the harness
    // has to know both what the terminal looked like before the emulator
    // started and whether it was put back afterwards.  It never reads input
    // from it - that would take bytes meant for the guest.
    p.slave = open(p.name.c_str(), O_RDWR | O_NOCTTY);
    if (p.slave < 0) { close(p.master); return false; }
    set_nonblock(p.master);
    return true;
}

static void pty_close(Pty& p) {
    if (p.slave >= 0) close(p.slave);
    if (p.master >= 0) close(p.master);
    p.master = p.slave = -1;
}

// Everything the emulator is expected to put back on the way out.
//
// PENDIN is excluded because the emulator never sets it: the line discipline
// does, to note that it has raw bytes to reprocess now that canonical mode is
// back, which is exactly what happens when a terminal is restored without its
// input queue being thrown away.  Holding the emulator to clearing it would be
// demanding that it discard the user's typing.
static bool termios_equal(const struct termios& a, const struct termios& b) {
    tcflag_t ignore = 0;
#ifdef PENDIN
    ignore |= PENDIN;
#endif
    if (a.c_iflag != b.c_iflag || a.c_oflag != b.c_oflag ||
        a.c_cflag != b.c_cflag ||
        (a.c_lflag & ~ignore) != (b.c_lflag & ~ignore)) return false;
    for (int i = 0; i < NCCS; i++) if (a.c_cc[i] != b.c_cc[i]) return false;
    return true;
}

// ============================================================================
// Key scripts
//
// A comma separated list.  Bytes, not key codes: a POSIX terminal hands the
// program whatever the keyboard sent, and the emulator translates none of it,
// so what a test sends is what the guest should receive.
//
//   ^X        control character
//   xNN       one byte in hex
//   Up Down Left Right Home End PgUp PgDn Ins Del F1 F2
//             the escape sequences xterm and Terminal.app send for those keys
//   Enter Bksp Esc Space Tab
//   wait:MS   pause, for the cases that are about timing
//   c         any single character, itself
//
// wait: is handled by the caller rather than here, because a pause that stops
// reading the terminal would let its output buffer fill and block the guest -
// which looks from the outside exactly like the stalls these tests are for.
// ============================================================================

struct NamedKey {
    const char* name;
    const char* bytes;
};

static const NamedKey named_keys[] = {
    { "Up",    "\x1b[A" },  { "Down",  "\x1b[B" },
    { "Right", "\x1b[C" },  { "Left",  "\x1b[D" },
    { "Home",  "\x1b[H" },  { "End",   "\x1b[F" },
    { "PgUp",  "\x1b[5~" }, { "PgDn",  "\x1b[6~" },
    { "Ins",   "\x1b[2~" }, { "Del",   "\x1b[3~" },
    { "F1",    "\x1bOP" },  { "F2",    "\x1bOQ" },
    { "Enter", "\r" },      { "Bksp",  "\x7f" },
    { "Esc",   "\x1b" },    { "Space", " " },
    { "Tab",   "\t" }
};

// Send one entry of a key script.  Returns false only on a spec this does not
// understand, which is a mistake in the test rather than a failure of it.
static bool send_spec(int fd, const std::string& spec) {
    if (spec.empty()) return true;
    for (size_t i = 0; i < sizeof named_keys / sizeof named_keys[0]; i++) {
        if (spec == named_keys[i].name) {
            return write_all(fd, named_keys[i].bytes, strlen(named_keys[i].bytes));
        }
    }
    if (spec.size() == 2 && spec[0] == '^') {
        char c = spec[1];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        char b = (char)(c - '@');          // ^@ is 0, ^A is 1, ... ^_ is 31
        return write_all(fd, &b, 1);
    }
    if (spec.size() == 3 && (spec[0] == 'x' || spec[0] == 'X')) {
        char* end = NULL;
        long v = strtol(spec.c_str() + 1, &end, 16);
        if (end && *end == 0 && v >= 0 && v <= 0xFF) {
            char b = (char)v;
            return write_all(fd, &b, 1);
        }
    }
    if (spec.size() == 1) return write_all(fd, spec.c_str(), 1);
    fprintf(stderr, "pty_console: unknown key spec %s\n", spec.c_str());
    return false;
}

// ============================================================================
// Running one case
// ============================================================================

enum StdinKind {
    StdinPty,       // a terminal, which is the only way to reach the raw mode
    StdinFile,      // a regular file that ends
    StdinPipe,      // a pipe that ends
    StdinDevNull    // a character device that is empty from the start
};

struct Result {
    std::string out;            // the guest's stdout
    std::string err;            // the emulator's diagnostics
    std::string tty;            // whatever reached the terminal itself
    std::string left;           // still queued on the terminal after the exit
    bool timed_out;
    bool termios_restored;
    bool raw_seen;              // the emulator was observed in raw mode
};

struct Spawn {
    const char* emu;
    const char* com;
    const char* emu_args;
    const char* keys_before;
    const char* keys;
    StdinKind stdin_kind;
    const char* stdin_text;
    bool stdout_to_pty;
    int timeout_ms;
    int kill_with;
    bool collect_left;
};

static bool run_guest(const Spawn& s, const std::string& dir, Result& r) {
    r.timed_out = false;
    r.termios_restored = true;
    r.raw_seen = false;

    Pty pty;
    pty.master = pty.slave = -1;
    struct termios before;
    memset(&before, 0, sizeof before);

    int in_fd = -1;                 // what the child gets as stdin
    std::string in_path;
    int pipe_in[2] = { -1, -1 };

    if (s.stdin_kind == StdinPty) {
        if (!pty_open(pty)) {
            fprintf(stderr, "pty_console: cannot allocate a pty (%s)\n", strerror(errno));
            return false;
        }
        if (tcgetattr(pty.slave, &before) != 0) { pty_close(pty); return false; }
        // Queued before the fork, so there is no race to lose: these are bytes
        // already waiting when the emulator starts, which is what a user who
        // typed ahead of the prompt leaves behind
        if (s.keys_before && *s.keys_before) {
            for (const char* p = s.keys_before; p && *p; ) {
                const char* comma = strchr(p, ',');
                std::string spec(p, comma ? (size_t)(comma - p) : strlen(p));
                if (!send_spec(pty.master, spec)) break;
                if (!comma) break;
                p = comma + 1;
            }
        }
        in_fd = pty.slave;
    } else if (s.stdin_kind == StdinFile) {
        in_path = dir + "/case.in";
        if (!write_file(in_path, s.stdin_text ? s.stdin_text : "",
                        s.stdin_text ? strlen(s.stdin_text) : 0)) return false;
        in_fd = open(in_path.c_str(), O_RDONLY);
        // Falling through with no fd would quietly run the case against the
        // harness's own stdin instead, and it would pass having tested nothing
        if (in_fd < 0) return false;
    } else if (s.stdin_kind == StdinPipe) {
        if (pipe(pipe_in) != 0) return false;
        if (s.stdin_text && *s.stdin_text) {
            if (!write_all(pipe_in[1], s.stdin_text, strlen(s.stdin_text))) return false;
        }
        close(pipe_in[1]);
        pipe_in[1] = -1;
        in_fd = pipe_in[0];
    } else {
        in_fd = open("/dev/null", O_RDONLY);
        if (in_fd < 0) return false;
    }

    int out_pipe[2] = { -1, -1 };
    int err_pipe[2] = { -1, -1 };
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) return false;

    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // The master must not survive into the child: a fd the child holds
        // would keep the terminal alive after the parent let go of it
        if (pty.master >= 0) close(pty.master);
        close(out_pipe[0]);
        close(err_pipe[0]);
        setsid();               // no controlling terminal, see the file header
        dup2(in_fd, 0);
        // Only the OPOST case sends the guest's output to the terminal; every
        // other case wants it on a pipe, away from anything the tty might do
        dup2(s.stdout_to_pty && pty.slave >= 0 ? pty.slave : out_pipe[1], 1);
        dup2(err_pipe[1], 2);
        // in_fd is the slave itself for a terminal case, so close it once
        if (in_fd > 2) close(in_fd);
        if (out_pipe[1] > 2) close(out_pipe[1]);
        if (err_pipe[1] > 2) close(err_pipe[1]);
        if (pty.slave > 2 && pty.slave != in_fd) close(pty.slave);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(s.emu));
        std::string args = s.emu_args ? s.emu_args : "";
        std::vector<std::string> parts;
        for (size_t i = 0; i < args.size(); ) {
            size_t j = args.find(' ', i);
            if (j == std::string::npos) j = args.size();
            if (j > i) parts.push_back(args.substr(i, j - i));
            i = j + 1;
        }
        for (size_t i = 0; i < parts.size(); i++) argv.push_back(const_cast<char*>(parts[i].c_str()));
        argv.push_back(const_cast<char*>(s.com));
        argv.push_back(NULL);
        execv(s.emu, &argv[0]);
        _exit(127);
    }

    close(out_pipe[1]); out_pipe[1] = -1;
    close(err_pipe[1]); err_pipe[1] = -1;
    set_nonblock(out_pipe[0]);
    set_nonblock(err_pipe[0]);

    long long start = now_ms();
    // Whether the child has already been collected.  The wait below can reap
    // it, and a second waitpid() would then return -1 forever and the case
    // would report a timeout for a guest that had in fact finished, which
    // is what a very short one does.
    bool reaped = false;

    // Wait until the emulator has taken the terminal out of canonical mode
    // before typing at it.  Without this the first bytes can land while the
    // line discipline is still cooked, and the failure looks like a
    // translation bug rather than a race.  A redirected-input case never
    // enters raw mode, so waiting for it there would burn the whole timeout.
    if (s.stdin_kind == StdinPty) {
        while (now_ms() - start < s.timeout_ms) {
            struct termios t;
            if (tcgetattr(pty.slave, &t) == 0 && (t.c_lflag & ICANON) == 0) {
                r.raw_seen = true;
                break;
            }
            if (waitpid(pid, NULL, WNOHANG) == pid) { reaped = true; break; }
            usleep(2000);
            drain(out_pipe[0], r.out);
            drain(err_pipe[0], r.err);
            drain(pty.master, r.tty);
        }
    }

    // A signal case is about what the emulator leaves behind, so it is sent
    // only once raw mode is on: killing it before that would prove nothing
    if (s.kill_with) kill(pid, s.kill_with);

    for (const char* p = s.keys; p && *p; ) {
        const char* comma = strchr(p, ',');
        std::string spec(p, comma ? (size_t)(comma - p) : strlen(p));
        if (spec.size() > 5 && spec.compare(0, 5, "wait:") == 0) {
            // Keep reading through the pause, for the reason above
            long long until = now_ms() + atoi(spec.c_str() + 5);
            while (now_ms() < until) {
                usleep(5000);
                drain(out_pipe[0], r.out);
                drain(err_pipe[0], r.err);
                drain(pty.master, r.tty);
            }
        } else if (!send_spec(pty.master, spec)) {
            break;
        }
        drain(out_pipe[0], r.out);
        drain(err_pipe[0], r.err);
        drain(pty.master, r.tty);
        if (!comma) break;
        p = comma + 1;
    }

    // The wait for the emulator to finish gets a budget of its own rather than
    // whatever is left of the one the startup wait and the key script above
    // have already spent.  Sharing them was what made these cases fail under
    // load: a slow start took the time the exit was going to need and the case
    // reported a hang that never happened.
    //
    // Confirmed by making the start slow on purpose rather than waiting for a
    // loaded machine to do it.  With a wrapper that sleeps before it execs the
    // emulator, and the emulator itself untouched and healthy - it starts, goes
    // raw, takes the signal, puts the terminal back and dies - the six kill
    // cases went 6 pass at 0s and at 5s, 3 pass 3 fail at 9s, and 2 pass 4 fail
    // at 9.9s and 9.95s, against a 10s budget.  Every one of those failures
    // read "the emulator had to be killed: it never finished".
    //
    // How much the exit itself needs is not uniform, which is the other half of
    // why one budget was tight: four of the seven signals dump core, and the
    // reap then waits on whatever the host does with a core.  Measured here,
    // unloaded, with core_pattern piping to apport: SIGQUIT 445ms, SIGSEGV
    // 1385ms, SIGBUS 1327ms, SIGABRT 1392ms, against SIGTERM 11ms, SIGHUP 3ms
    // and SIGINT 3ms.  That cost is outside the emulator entirely and it is
    // paid on top of the start.
    long long wait_start = now_ms();
    for (;;) {
        drain(out_pipe[0], r.out);
        drain(err_pipe[0], r.err);
        drain(pty.master, r.tty);
        if (reaped) break;
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) { reaped = true; break; }
        if (now_ms() - wait_start > s.timeout_ms) {
            r.timed_out = true;
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            break;
        }
        usleep(3000);
    }
    drain(out_pipe[0], r.out);
    drain(err_pipe[0], r.err);
    drain(pty.master, r.tty);

    // What the shell would find waiting for it.  The terminal is canonical
    // again by now, so a reader gets nothing until a line terminator arrives;
    // supplying one is what makes the difference between kept and discarded
    // visible rather than merely pending.
    if (s.collect_left && pty.slave >= 0) {
        // Only now: O_NONBLOCK lives on the open file description, which the
        // child shared as its stdin, so setting it any earlier would have made
        // the emulator's own read() return EAGAIN and look like end of input.
        set_nonblock(pty.slave);
        write_all(pty.master, "\r", 1);
        usleep(150000);
        drain(pty.slave, r.left);
    }

    if (s.stdin_kind == StdinPty) {
        struct termios after;
        // A run that was killed never got to restore anything, so only a run
        // that finished on its own can be held to this
        r.termios_restored = (r.timed_out && !s.kill_with) ||
                             (tcgetattr(pty.slave, &after) == 0 && termios_equal(before, after));
    }

    if (out_pipe[0] >= 0) close(out_pipe[0]);
    if (err_pipe[0] >= 0) close(err_pipe[0]);
    if (pipe_in[0] >= 0) close(pipe_in[0]);
    if (s.stdin_kind == StdinFile || s.stdin_kind == StdinDevNull) close(in_fd);
    if (!in_path.empty()) unlink(in_path.c_str());
    pty_close(pty);
    return true;
}

// ============================================================================
// The control
//
// Every case below is worth exactly what the raw mode is worth, so the raw
// mode gets measured first, on this machine, against a reader of our own.  It
// asserts the thing the emulator depends on - that the mask in
// os/linux/platform.cc lets every control byte through - and then reports what
// the same mask loses with the IEXTEN clear taken out, which is the one clear
// that has no effect on Linux and a large one on BSD/XNU.  A run that says
// "nothing" there is a run where the ^V and ^O cases below could not have
// failed, and the line says so rather than leaving a green tick to be read as
// proof.
// ============================================================================

// Bytes a CP/M guest can be handed.  0x00 is left out: BDOS 6 reports "no
// character waiting" as 0, so a guest cannot tell a typed NUL from silence,
// and the hex-echo programs would loop past it.
static std::string control_bytes() {
    std::string s;
    for (int c = 0x01; c <= 0x1F; c++) s += (char)c;
    s += (char)0x7F;
    return s;
}

// Push the bytes through a reader running under the given mask, and return
// what came out the far side.
static bool measure_raw_mode(bool clear_iexten, std::string& got) {
    Pty pty;
    pty.master = pty.slave = -1;
    if (!pty_open(pty)) return false;

    int p[2];
    if (pipe(p) != 0) { pty_close(pty); return false; }

    pid_t pid = fork();
    if (pid < 0) { pty_close(pty); return false; }
    if (pid == 0) {
        close(p[0]);
        close(pty.master);
        setsid();
        dup2(pty.slave, 0);
        if (pty.slave > 2) close(pty.slave);
        apply_cpmemu_raw_mode(0, clear_iexten);
        for (;;) {
            unsigned char c;
            ssize_t n = read(0, &c, 1);
            if (n != 1) { if (n < 0 && errno == EINTR) continue; break; }
            char buf[8];
            int len = snprintf(buf, sizeof buf, "%02X ", c);
            if (write(p[1], buf, (size_t)len) != len) break;
        }
        _exit(0);
    }
    close(p[1]);
    set_nonblock(p[0]);

    long long start = now_ms();
    while (now_ms() - start < 3000) {
        struct termios t;
        if (tcgetattr(pty.slave, &t) == 0 && (t.c_lflag & ICANON) == 0) break;
        usleep(2000);
    }
    std::string bytes = control_bytes();
    write_all(pty.master, bytes.data(), bytes.size());

    got.clear();
    // The reader never stops on its own, so give it a fixed grace period and
    // then take what it produced.  Every byte is one read and one write.
    for (int i = 0; i < 60; i++) {
        usleep(10000);
        drain(p[0], got);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    close(p[0]);
    pty_close(pty);
    return true;
}

// Which of the bytes we sent are missing from a hex string of what arrived
static std::string missing_from(const std::string& got) {
    std::string lost;
    std::string bytes = control_bytes();
    for (size_t i = 0; i < bytes.size(); i++) {
        unsigned char c = (unsigned char)bytes[i];
        char pat[8];
        snprintf(pat, sizeof pat, "%02X ", c);
        if (got.find(pat) == std::string::npos) {
            char note[16];
            if (c == 0x7F) snprintf(note, sizeof note, " 7F(DEL)");
            else snprintf(note, sizeof note, " %02X(^%c)", c, c + 64);
            lost += note;
        }
    }
    return lost;
}

static bool run_control(int& passed, int& failed) {
    std::string with_clear, without_clear;
    if (!measure_raw_mode(true, with_clear) || !measure_raw_mode(false, without_clear)) {
        printf("FAIL  control: the raw mode lets every control byte through\n");
        printf("        could not allocate a terminal to measure it on\n");
        failed++;
        return false;
    }

    std::string lost_with = missing_from(with_clear);
    std::string lost_without = missing_from(without_clear);

    if (lost_with.empty()) {
        printf("PASS  control: the raw mode lets every control byte through\n");
        passed++;
    } else {
        printf("FAIL  control: the raw mode lets every control byte through\n");
        printf("        the line discipline still ate:%s\n", lost_with.c_str());
        printf("        got: %s\n", with_clear.c_str());
        failed++;
    }

    if (lost_without.empty()) {
        printf("      note: leaving IEXTEN set loses nothing here, so the clear in\n");
        printf("            enable_raw_mode() is insurance on this platform and the\n");
        printf("            ^V and ^O cases below could not have failed.\n");
    } else {
        printf("      note: without the IEXTEN clear this platform loses:%s\n", lost_without.c_str());
        printf("            so the ^V and ^O cases below are testing something real.\n");
    }
    return true;
}

// ============================================================================
// Cases
// ============================================================================

struct Case {
    const char* name;
    const unsigned char* prog;
    size_t prog_len;
    const char* keys_before;    // written into the terminal before the emulator
                                // starts, the way a shell leaves type-ahead
                                // behind; NULL for nothing
    const char* keys;
    const char* emu_args;
    const char* want_out;       // exact match against the guest's stdout, or
                                // NULL when the case is only about stderr
    const char* want_err;       // substring of stderr, or "" for no requirement
    int timeout_ms;
    StdinKind stdin_kind;
    const char* stdin_text;
    bool stdout_to_pty;
    int kill_with;              // send this signal once raw mode is on, and
                                // hold the emulator to putting the terminal
                                // back anyway; 0 to let the case run normally
    const char* want_left;      // what the next reader of the terminal should
                                // find still waiting, or NULL not to look
};

#define PROG(p) p, sizeof(p)

static const Case cases[] = {
    // The three the IEXTEN clear was for, under the same name the Windows
    // harness gives them so the two results can be read side by side.
    { "control keys reach the guest untouched", PROG(con6hex_com),
      NULL, "^R,^V,^O,^S,^Q,^Z,.", "", "12 16 0F 13 11 1A 2E ", "", 15000, StdinPty, NULL, false, 0, NULL },

    // Every byte a CP/M program can be sent, in one string.  The WordStar
    // diamond is a subset of it: ^E ^S ^D ^X for the cursor, ^A ^F ^R ^C for
    // words and pages, and ^V ^O ^T ^Y ^U ^P for the rest.  0x0A is the one
    // that comes back as something else, and by design: every read site turns
    // a LF into a CR, because CP/M ends a line with CR alone.
    { "every control byte reaches the guest", PROG(con6hex_com),
      NULL, "x01,x02,x03,x04,x05,x06,x07,x08,x09,x0A,x0B,x0C,x0D,x0E,x0F,x10,x11,x12,"
      "x13,x14,x15,x16,x17,x18,x19,x1A,x1B,x1C,x1D,x1E,x1F,x7F,.", "",
      "01 02 03 04 05 06 07 08 09 0D 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 "
      "19 1A 1B 1C 1D 1E 1F 7F 2E ", "", 15000, StdinPty, NULL, false, 0, NULL },

    // A POSIX terminal sends an escape sequence for an arrow key and the
    // emulator translates none of it, so the guest gets the three bytes as
    // typed.  Windows translates the same key to a WordStar diamond code, so
    // this is where the two platforms genuinely differ; the case is here to
    // pin that, not to bless it.
    { "an arrow key arrives as the bytes the terminal sent", PROG(con6hex_com),
      NULL, "Up,Down,.", "", "1B 5B 41 1B 5B 42 2E ", "", 15000, StdinPty, NULL, false, 0, NULL },

    { "the blocking read gets the same bytes as the polled one", PROG(con1hex_com),
      NULL, "^V,^O,A,.", "", "16 0F 41 2E ", "", 15000, StdinPty, NULL, false, 0, NULL },

    { "BIOS CONST and CONIN see the same keys as BDOS", PROG(bioshex_com),
      NULL, "^V,^O,A,.", "", "16 0F 41 2E ", "", 15000, StdinPty, NULL, false, 0, NULL },

    // ---- seven-bit console input -----------------------------------------
    // Every console read site masks the byte it hands the guest with & 0x7F,
    // so a CP/M program here sees seven bits.  That is a decision rather than
    // a law: every layer below keeps the eighth bit, and POSIX raw mode clears
    // ISTRIP specifically so it does.  Until these cases were written the only
    // tests that pinned any of it were the four code-page cases in
    // tests/win_console.cc, which have never been executed on any machine, so
    // the masks were asserted by nothing that runs.  Whoever gives the guest
    // eight bits has to rewrite these deliberately - a failure here is that
    // decision being made by accident, not a regression to mask away.
    //
    // The keys are the UTF-8 for e-acute, alpha, pound and em-dash: nine bytes
    // in, nine bytes out with bit 7 cleared on each.
    { "console input is seven bits", PROG(con1hex_com),
      NULL, "xC3,xA9,xCE,xB1,xC2,xA3,xE2,x80,x94,.", "",
      "43 29 4E 31 42 23 62 00 14 2E ", "", 15000, StdinPty, NULL, false, 0, NULL },

    { "seven bits over redirected input too", PROG(con1hex_com),
      NULL, "", "", "43 29 4E 31 42 23 62 00 14 2E ", "", 15000, StdinPipe,
      "\xC3\xA9\xCE\xB1\xC2\xA3\xE2\x80\x94.", false, 0, NULL },

    { "BIOS CONIN strips the eighth bit too", PROG(bioshex_com),
      NULL, "xC3,xA9,.", "", "43 29 2E ", "", 15000, StdinPty, NULL, false, 0, NULL },

    // BDOS 6 spells "no character" 0, so the one input byte that masks to zero
    // is not merely altered on the polled path - it is consumed and never
    // delivered.  0x80 in, nothing out: the guest sees the A that followed it
    // and nothing before.  It is the only byte the mask destroys outright, and
    // 0x00 is indistinguishable from an idle console on real CP/M too; what
    // this emulator adds is the second one.
    { "BDOS 6 loses a byte that masks to zero", PROG(con6hex_com),
      NULL, "x80,A,.", "", "41 2E ", "", 15000, StdinPty, NULL, false, 0, NULL },

    // The line editor is the fourth read site and the one that masks what it
    // stores rather than what it returns.  0xC3 is stored as 'C' and echoed as
    // 'C', so the guest reads back 43.
    { "the line editor stores seven bits too", PROG(con10buf_com),
      NULL, "xC3,Enter", "", "C\r\n43 ", "", 15000, StdinPty, NULL, false, 0, NULL },

    // The mask is applied to what gets stored, after every raw-byte test, so
    // the escape hatch counts real ^C rather than bytes that merely mask to
    // one.  Five 0x83 reach the guest as five 03 and the emulator stays up;
    // five real 0x03 end it, which is the case below.  check_ctrl_c_exit has
    // four call sites and the same mistake could be made at any one of them,
    // so all four are here: BDOS 6, BDOS 1, the line editor and BIOS CONIN.
    // Each fails by truncation - the emulator exits before the guest can print
    // the byte that would have finished the line.
    { "a byte that masks to ^C is not a ^C", PROG(con6hex_com),
      NULL, "x83,x83,x83,x83,x83,.", "", "03 03 03 03 03 2E ", "", 15000,
      StdinPty, NULL, false, 0, NULL },

    { "nor at the blocking read", PROG(con1hex_com),
      NULL, "x83,x83,x83,x83,x83,.", "", "03 03 03 03 03 2E ", "", 15000,
      StdinPty, NULL, false, 0, NULL },

    { "nor at BIOS CONIN", PROG(bioshex_com),
      NULL, "x83,x83,x83,x83,x83,.", "", "03 03 03 03 03 2E ", "", 15000,
      StdinPty, NULL, false, 0, NULL },

    { "nor in the line editor", PROG(con10buf_com),
      NULL, "x83,x83,x83,x83,x83,Enter", "",
      "^C^C^C^C^C\r\n03 03 03 03 03 ", "", 15000, StdinPty, NULL, false, 0, NULL },

    // The escape hatch a raw-mode emulator needs.  The fifth ^C exits before
    // the guest can print it, so only four appear.
    { "five typed ^C exit the emulator", PROG(con6hex_com),
      NULL, "^C,^C,^C,^C,^C", "", "03 03 03 03 ", "5 consecutive ^C received",
      15000, StdinPty, NULL, false, 0, NULL },

    { "^C spread past the window does not exit", PROG(con6hex_com),
      NULL, "^C,wait:800,^C,wait:800,^C,wait:800,^C,wait:800,^C,wait:200,.", "",
      "03 03 03 03 03 2E ", "", 25000, StdinPty, NULL, false, 0, NULL },

    { "--no-ctrl-c-exit hands every ^C to the guest", PROG(con6hex_com),
      NULL, "^C,^C,^C,^C,^C,^C,.", "--no-ctrl-c-exit", "03 03 03 03 03 03 2E ", "",
      15000, StdinPty, NULL, false, 0, NULL },

    // A status call that promised a character it could not produce would leave
    // this guest stuck in CONIN and print nothing at all.  The leading S
    // releases the blocking read it starts with, so whatever matters is
    // already in the terminal when the poll begins.
    { "a quiet terminal reports a quiet terminal", PROG(conststall_com),
      NULL, "S", "", "T", "", 20000, StdinPty, NULL, false, 0, NULL },

    { "a typed key is seen, and then the terminal goes quiet", PROG(conststall_com),
      NULL, "S,A", "", "41 T", "", 20000, StdinPty, NULL, false, 0, NULL },

    // A byte the terminal would have eaten without the IEXTEN clear reaches
    // the line editor too, and is stored and echoed like any other control
    // character rather than being obeyed.
    { "the line editor stores ^V, does not obey it", PROG(con10buf_com),
      NULL, "A,B,^V,Enter", "", "AB^V\r\n41 42 16 ", "", 15000, StdinPty, NULL, false, 0, NULL },

    // ^H erases the last character and the echo puts the cursor back over it.
    // Nothing else in the suite touches the line editor's erase path.
    { "the line editor erases what ^H takes back", PROG(con10buf_com),
      NULL, "A,B,^H,C,Enter", "", "AB\b \bC\r\n41 43 ", "", 15000, StdinPty, NULL, false, 0, NULL },

    // OPOST is the one clear that only shows on the output side, and it only
    // shows when the output is the terminal: with it left set, ONLCR would
    // turn the guest's LF into CR LF and CP/M's CR LF into CR CR LF.
    { "output is raw, so CR LF stays two bytes", PROG(crlf_com),
      NULL, "", "", "\r\n", "", 10000, StdinPty, NULL, true, 0, NULL },

    // ---- input that ends -------------------------------------------------
    // Not terminal cases, but nothing else on POSIX covers them without an
    // assembler: tests/con_eof.asm and tests/con_spin.asm need pasmo, which is
    // not on every machine, and the whole group skips when it is missing.

    { "an empty file is end of input: CR once, then ^Z", PROG(coneof_com),
      NULL, "", "", "0D 1A ", "", 15000, StdinFile, "", false, 0, NULL },

    { "/dev/null is end of input too, not the keyboard", PROG(coneof_com),
      NULL, "", "", "0D 1A ", "", 15000, StdinDevNull, NULL, false, 0, NULL },

    { "the blocking read takes its bytes from a file", PROG(con1hex_com),
      NULL, "", "", "41 42 2E ", "", 15000, StdinFile, "AB.", false, 0, NULL },

    { "the blocking read takes its bytes from a pipe", PROG(con1hex_com),
      NULL, "", "", "41 42 2E ", "", 15000, StdinPipe, "AB.", false, 0, NULL },

    // A guest that ignores ^Z has to be stopped rather than left reading a
    // stream that will never produce another byte.  Its stdout is 1024 reads
    // of the same two bytes, which says nothing a count would not, so this one
    // is about the diagnostic and the exit.
    { "a reader that ignores end of input is stopped", PROG(con1hex_com),
      NULL, "", "", NULL, "reads past end of input", 20000, StdinFile, "", false, 0, NULL },

    // The polled path against redirected input, which is where the two
    // platforms used to disagree.  stdin_has_data() answered false for
    // anything that was not a tty, so a guest polling BDOS 6 or BIOS CONST
    // never took a byte of its own redirected input - and, worse, never
    // reached the read, so it never reached end of input either and spun until
    // it was killed.  Both of these hang against the old platform layer.
    //
    // POSIX matches Windows now: same guest, same input, same expected bytes
    // as "polled input arrives from a file, not only from a pipe" in
    // tests/win_console.cc, so the two suites can be compared byte for byte.
    // These are the polled twins of the two blocking cases above.
    { "a polled read takes its bytes from a file", PROG(con6hex_com),
      NULL, "", "", "41 42 2E ", "", 20000, StdinFile, "AB.", false, 0, NULL },

    { "a polled read takes its bytes from a pipe", PROG(con6hex_com),
      NULL, "", "", "41 42 2E ", "", 20000, StdinPipe, "AB.", false, 0, NULL },

    // The other half of reaching the read: end of input now reaches the polled
    // path too, so the shared give-up can end a run the guest would otherwise
    // never end.  BDOS 6 spells "no character" and "end of input" both as 0,
    // which is why this guest cannot stop itself, and why the count in
    // cpmemu.cc has to.  Nothing on POSIX could reach that code before.
    { "a polled reader that runs out of input is stopped", PROG(con6hex_com),
      NULL, "", "", NULL, "reads past end of input", 20000, StdinFile, "", false, 0, NULL },

    // ---- being killed ----------------------------------------------------
    // atexit() covers exit() and a return from main and nothing else, so a
    // signal leaves the terminal exactly as the emulator set it: no echo, no
    // line editing, ^C inert, and a shell the user has to reset by hand.  ISIG
    // is cleared, so none of these can come from the keyboard - they come from
    // a kill, from a terminal window closing, or from an ssh session dropping,
    // which is the POSIX face of the ctrl+break item in todo.txt.
    { "the terminal is put back when the emulator is killed", PROG(con1hex_com),
      NULL, "", "", "", "", 10000, StdinPty, NULL, false, SIGTERM, NULL },

    { "the terminal is put back on a hangup", PROG(con1hex_com),
      NULL, "", "", "", "", 10000, StdinPty, NULL, false, SIGHUP, NULL },

    { "the terminal is put back on a quit", PROG(con1hex_com),
      NULL, "", "", "", "", 10000, StdinPty, NULL, false, SIGQUIT, NULL },

    // Not reachable from the keyboard here, since ISIG is cleared and ^C is
    // the guest's to deal with, but a kill -INT from another window is
    { "the terminal is put back on an interrupt", PROG(con1hex_com),
      NULL, "", "", "", "", 10000, StdinPty, NULL, false, SIGINT, NULL },

    // A crash strands the terminal exactly as a kill does, and the user is no
    // less stuck for the emulator having been the one at fault
    { "the terminal is put back on a segfault", PROG(con1hex_com),
      NULL, "", "", "", "", 10000, StdinPty, NULL, false, SIGSEGV, NULL },

    { "the terminal is put back on a bus error", PROG(con1hex_com),
      NULL, "", "", "", "", 10000, StdinPty, NULL, false, SIGBUS, NULL },

    { "the terminal is put back on an abort", PROG(con1hex_com),
      NULL, "", "", "", "", 10000, StdinPty, NULL, false, SIGABRT, NULL },

    // ---- what was already typed ------------------------------------------
    // Bytes waiting on the terminal before the emulator reached its tcsetattr.
    // TCSAFLUSH threw these away: the user watched the line discipline echo
    // them and the guest then never saw them.  The same one-token choice is
    // what stops tcsetattr blocking forever on a terminal stopped by ^S, which
    // is far worse and much harder to test for, so this case stands in for
    // both - see the note in enable_raw_mode().
    { "type-ahead survives the switch to raw mode", PROG(con6hex_com),
      "A,B", ".", "", "41 42 2E ", "", 15000, StdinPty, NULL, false, 0, NULL },

    // The other half of the same choice: what the guest did not read has to be
    // there for the shell afterwards, not thrown away.  Everything is queued
    // before the emulator starts so there is no race in it - the line editor
    // stops at the CR, which leaves QR untouched behind it, and the emulator
    // then has to hand the terminal back without taking them with it.
    { "what the guest never read is left for the shell", PROG(con10buf_com),
      "A,B,Enter,Q,R", "", "", "AB\r\n41 42 ", "", 15000, StdinPty, NULL, false, 0, "QR\n" }
};

// ============================================================================
// Job control
//
// Two things the case table above cannot reach, because both are about what a
// shell does to the terminal while the emulator is stopped, and the harness
// above is deliberately not a shell: it calls setsid() and never claims a
// controlling terminal, so its children sit in an orphaned process group where
// SIGTSTP, SIGTTIN and SIGTTOU are discarded rather than delivered.
//
//   kill -TSTP, fg   bash and zsh put their own termios back when they take a
//                    job into the foreground.  enable_raw_mode() ran once, at
//                    startup, and nothing re-applied it, so the emulator came
//                    back to a canonical terminal and stayed there for the rest
//                    of the run: the guest saw nothing until CR and then the
//                    whole line at once.
//
//   cpmemu prog &    the tcsetattr in enable_raw_mode() then runs in a
//   ... then fg      background process group, where the terminal driver stops
//                    the process with SIGTTOU rather than letting it through.
//
// Both therefore run under a stand-in shell: a session leader that owns the
// pty, runs the emulator in a process group of its own, and reports through a
// pipe every time the job stops or is continued.  The harness cannot wait on
// the job itself - it is the shell's child, not the harness's - so those notes
// are the only way it can tell a stop from a hang.
//
// The shell half is simulated in the order bash uses: hand the terminal over
// first, then send SIGCONT.  That order is what gives the emulator a chance at
// all - a shell writing the terminal after the SIGCONT would be racing the
// handler under test, and nothing here can decide that race for it - so a pass
// here means the handler works against the shells people have, not against
// every shell that could exist.
// ============================================================================

static bool tty_is_raw(int fd) {
    struct termios t;
    return tcgetattr(fd, &t) == 0 && (t.c_lflag & ICANON) == 0;
}

// Where the job is to sit when it starts
enum JobStart {
    JobForeground,      // the shell gives it the terminal before it runs
    JobBackground       // `cpmemu prog.com &`: its tcsetattr meets SIGTTOU
};

struct Job {
    pid_t shell;        // the stand-in shell, and the job's parent
    pid_t pid;          // the emulator
    int out_fd;
    int err_fd;
    int note_fd;
    Pty pty;
    struct termios before;
    std::string notes;  // everything the shell has said so far
    std::string out;
    std::string err;
    std::string tty;
};

static void job_drain(Job& j) {
    drain(j.out_fd, j.out);
    drain(j.err_fd, j.err);
    drain(j.note_fd, j.notes);
    drain(j.pty.master, j.tty);
}

// How many times the shell has reported the given event.  Counting rather than
// searching, because a case cares about the second STOP as well as the first.
static int job_note_count(const Job& j, const char* what) {
    int n = 0;
    for (size_t at = 0; (at = j.notes.find(what, at)) != std::string::npos; at++) n++;
    return n;
}

static bool job_wait_note(Job& j, const char* what, int least, int timeout_ms) {
    long long start = now_ms();
    for (;;) {
        job_drain(j);
        if (job_note_count(j, what) >= least) return true;
        if (now_ms() - start >= timeout_ms) return false;
        usleep(3000);
    }
}

static bool job_wait_raw(Job& j, bool want, int timeout_ms) {
    long long start = now_ms();
    for (;;) {
        job_drain(j);
        if (tty_is_raw(j.pty.slave) == want) return true;
        if (now_ms() - start >= timeout_ms) return false;
        usleep(2000);
    }
}

static bool job_wait_out(Job& j, const char* want, int timeout_ms) {
    long long start = now_ms();
    for (;;) {
        job_drain(j);
        if (j.out.find(want) != std::string::npos) return true;
        if (now_ms() - start >= timeout_ms) return false;
        usleep(3000);
    }
}

static void note(int fd, const char* text) {
    // In the shell child, with nothing sensible left to do if it fails
    if (write(fd, text, strlen(text)) < 0) { /* the parent will time out */ }
}

// Start the emulator under a stand-in shell.  Returns false only if the
// scaffolding could not be built, which is a fault in the harness or a machine
// without ptys, not a failure of the emulator.
static bool job_start(Job& j, const std::string& emu, const std::string& com,
                      JobStart where, std::string& why) {
    j.shell = j.pid = -1;
    j.out_fd = j.err_fd = j.note_fd = -1;
    j.pty.master = j.pty.slave = -1;

    if (!pty_open(j.pty)) { why = "cannot allocate a pty"; return false; }
    if (tcgetattr(j.pty.slave, &j.before) != 0) {
        why = "cannot read the terminal";
        pty_close(j.pty);
        return false;
    }

    int out_pipe[2], err_pipe[2], note_pipe[2];
    if (pipe(out_pipe) != 0) { why = "cannot make a pipe"; pty_close(j.pty); return false; }
    if (pipe(err_pipe) != 0 || pipe(note_pipe) != 0) {
        why = "cannot make a pipe";
        close(out_pipe[0]); close(out_pipe[1]);
        pty_close(j.pty);
        return false;
    }

    pid_t shell = fork();
    if (shell < 0) {
        why = "cannot fork";
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        close(note_pipe[0]); close(note_pipe[1]);
        pty_close(j.pty);
        return false;
    }

    if (shell == 0) {
        close(j.pty.master);
        close(out_pipe[0]);
        close(err_pipe[0]);
        close(note_pipe[0]);
        setsid();
#ifdef TIOCSCTTY
        if (ioctl(j.pty.slave, TIOCSCTTY, 0) != 0) { note(note_pipe[1], "NOCTTY\n"); _exit(2); }
#else
        note(note_pipe[1], "NOCTTY\n");
        _exit(2);
#endif
        pid_t job = fork();
        if (job < 0) { note(note_pipe[1], "NOFORK\n"); _exit(2); }
        if (job == 0) {
            // A process group of its own.  Set on both sides of the fork so
            // that neither the exec nor the tcsetpgrp below can win the race.
            setpgid(0, 0);
            close(note_pipe[1]);
            dup2(j.pty.slave, 0);
            dup2(out_pipe[1], 1);
            dup2(err_pipe[1], 2);
            if (j.pty.slave > 2) close(j.pty.slave);
            if (out_pipe[1] > 2) close(out_pipe[1]);
            if (err_pipe[1] > 2) close(err_pipe[1]);
            execl(emu.c_str(), emu.c_str(), com.c_str(), (char*)NULL);
            _exit(127);
        }
        setpgid(job, job);
        close(out_pipe[1]);
        close(err_pipe[1]);

        bool has_terminal = false;
        if (where == JobForeground) {
            tcsetpgrp(j.pty.slave, job);
            has_terminal = true;
        }
        char line[64];
        snprintf(line, sizeof line, "PID %ld\n", (long)job);
        note(note_pipe[1], line);

        for (;;) {
            int st = 0;
            pid_t w = waitpid(job, &st, WUNTRACED | WCONTINUED);
            if (w != job) {
                if (errno == EINTR) continue;
                break;
            }
            if (WIFSTOPPED(st)) {
                note(note_pipe[1], "STOP\n");
                if (!has_terminal) {
                    // fg, the way a shell does it: the terminal first, then
                    // the signal that starts the job running again
                    tcsetpgrp(j.pty.slave, job);
                    has_terminal = true;
                    kill(-job, SIGCONT);
                }
            } else if (WIFCONTINUED(st)) {
                note(note_pipe[1], "CONT\n");
            } else {
                // Whether the exit put the terminal back has to be read here,
                // in the session leader, and before it exits.  Darwin revokes
                // the slave for every other descriptor the moment the session
                // leader goes: measured on macOS 27, a tcgetattr in the
                // harness after job_finish() returns -1 with ENOTTY (25) and
                // isatty() on the same fd is 0, so a check made out there
                // could only ever say "not put back" whatever the emulator
                // did.  This is also the tighter place to look - nothing has
                // touched the terminal between the emulator's exit and this
                // line, where out there the stand-in shell's own exit sits in
                // between.  j.before was read before the fork, so this copy of
                // the process already has what to compare against.
                struct termios after;
                if (tcgetattr(j.pty.slave, &after) != 0) {
                    note(note_pipe[1], "NOTTY\n");
                } else {
                    note(note_pipe[1],
                         termios_equal(j.before, after) ? "PUTBACK\n" : "STRANDED\n");
                }
                note(note_pipe[1], "GONE\n");
                _exit(WIFEXITED(st) ? WEXITSTATUS(st) : 1);
            }
        }
        _exit(3);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    close(note_pipe[1]);
    j.shell = shell;
    j.out_fd = out_pipe[0];
    j.err_fd = err_pipe[0];
    j.note_fd = note_pipe[0];
    set_nonblock(j.out_fd);
    set_nonblock(j.err_fd);
    set_nonblock(j.note_fd);

    if (!job_wait_note(j, "PID ", 1, 5000)) {
        why = "the stand-in shell could not set up job control: " + show(j.notes);
        return false;
    }
    size_t at = j.notes.find("PID ");
    j.pid = (pid_t)atol(j.notes.c_str() + at + 4);
    return true;
}

// Take everything down, whatever state it is in.  Returns true if the emulator
// exited on its own within the grace period.
static bool job_finish(Job& j, int grace_ms) {
    bool gone = job_wait_note(j, "GONE\n", 1, grace_ms);
    if (!gone && j.pid > 0) {
        kill(j.pid, SIGCONT);
        kill(j.pid, SIGKILL);
    }
    if (j.shell > 0) {
        long long start = now_ms();
        while (now_ms() - start < 3000) {
            if (waitpid(j.shell, NULL, WNOHANG) == j.shell) { j.shell = -1; break; }
            job_drain(j);
            usleep(3000);
        }
        if (j.shell > 0) {
            kill(j.shell, SIGKILL);
            waitpid(j.shell, NULL, 0);
            j.shell = -1;
        }
    }
    job_drain(j);
    if (j.out_fd >= 0) close(j.out_fd);
    if (j.err_fd >= 0) close(j.err_fd);
    if (j.note_fd >= 0) close(j.note_fd);
    j.out_fd = j.err_fd = j.note_fd = -1;
    return gone;
}

static void report(const char* name, bool ok, const std::string& detail,
                   int& passed, int& failed) {
    if (ok) {
        printf("PASS  %s\n", name);
        passed++;
    } else {
        printf("FAIL  %s\n", name);
        fputs(detail.c_str(), stdout);
        failed++;
    }
}

// ---------------------------------------------------------------------------
// kill -TSTP, then fg
//
// Against an emulator with no handlers this fails in two separate places, and
// the messages keep them apart: the terminal is still raw while the process is
// stopped, so the shell that gets it back has no echo and no line editing; and
// after the resume it is canonical, so the guest is handed a whole line at a
// time or, as here, nothing at all.
// ---------------------------------------------------------------------------

static void run_suspend_case(const std::string& emu, const std::string& dir,
                             int& passed, int& failed) {
    const char* name = "a suspend puts the terminal back, and a resume takes it again";
    std::string detail;
    std::string com = dir + "/tstp.com";
    if (!write_file(com, con6hex_com, sizeof con6hex_com)) {
        report(name, false, "        cannot write the guest program\n", passed, failed);
        return;
    }

    Job j;
    std::string why;
    if (!job_start(j, emu, com, JobForeground, why)) {
        report(name, false, "        " + why + "\n", passed, failed);
        unlink(com.c_str());
        return;
    }

    bool ok = true;
    if (!job_wait_raw(j, true, 8000)) {
        detail += "        the emulator never put the terminal into raw mode\n";
        ok = false;
    }

    if (ok) {
        send_spec(j.pty.master, "A");
        job_wait_out(j, "41 ", 5000);
        if (j.out != "41 ") {
            detail += "        before the suspend the guest should have had 41 , it had " +
                      show(j.out) + "\n";
            ok = false;
        }
    }

    // ISIG is cleared, so a typed ^Z is the guest's byte 1A and never becomes a
    // signal.  kill(2) is the route that is left, and is the one the item in
    // todo.txt was measured with.
    if (ok) {
        kill(j.pid, SIGTSTP);
        if (!job_wait_note(j, "STOP\n", 1, 5000)) {
            detail += "        SIGTSTP did not stop the emulator\n";
            ok = false;
        }
    }

    if (ok && tty_is_raw(j.pty.slave)) {
        // The half no shell can paper over: whatever it does next, the terminal
        // it was handed back was raw.
        detail += "        the terminal was still raw while the emulator was stopped:\n"
                  "        a shell taking it back has no echo and no line editing\n";
        ok = false;
    }

    if (ok) {
        // fg, as bash and zsh do it: the shell's own termios back first, then
        // the job continued
        tcsetattr(j.pty.slave, TCSANOW, &j.before);
        kill(j.pid, SIGCONT);
        if (!job_wait_raw(j, true, 8000)) {
            detail += "        raw mode was not re-applied after the resume:\n"
                      "        the guest is back to whole lines at a time\n";
            ok = false;
        }
    }

    if (ok) {
        send_spec(j.pty.master, "B");
        send_spec(j.pty.master, ".");
        job_wait_out(j, "2E ", 8000);
        if (j.out != "41 42 2E ") {
            detail += "        expected: 41 42 2E \n";
            detail += "        got:      " + show(j.out) + "\n";
            ok = false;
        }
    }

    bool exited = job_finish(j, 5000);
    if (ok && !exited) {
        detail += "        the emulator had to be killed: it never finished\n";
        ok = false;
    }
    // The stand-in shell read the terminal for us the moment the emulator
    // exited, and said what it found - see the GONE branch in job_start().
    if (ok) {
        if (job_note_count(j, "STRANDED\n") > 0) {
            detail += "        the terminal was not put back on exit\n";
            ok = false;
        } else if (job_note_count(j, "PUTBACK\n") == 0) {
            detail += "        the stand-in shell never reported the terminal it was\n"
                      "        left with, so the exit was not checked: " + show(j.notes) + "\n";
            ok = false;
        }
    }
    pty_close(j.pty);
    unlink(com.c_str());
    report(name, ok, detail, passed, failed);
}

// ---------------------------------------------------------------------------
// cpmemu prog.com &, then fg
//
// No signal is sent to the emulator here.  It starts in a process group that is
// not the terminal's foreground group, so the terminal driver stops it inside
// enable_raw_mode()'s tcsetattr with SIGTTOU, and the fg that follows is the
// shell's tcsetpgrp plus a SIGCONT.
//
// What this proves depends on the platform, and the case says which it got.  On
// Linux the interrupted ioctl is restarted rather than failed - the emulator's
// own tcsetattr completes after the fg with no handler involved - so this is a
// guard against that changing, not evidence that the SIGCONT handler works.  It
// is on a platform that returns EINTR instead that the handler is the only
// thing standing between the fg and a canonical terminal.
// ---------------------------------------------------------------------------

static void run_background_case(const std::string& emu, const std::string& dir,
                                int& passed, int& failed) {
    const char* name = "a background start takes the terminal when it is brought forward";
    std::string detail;
    std::string com = dir + "/bgfg.com";
    if (!write_file(com, con6hex_com, sizeof con6hex_com)) {
        report(name, false, "        cannot write the guest program\n", passed, failed);
        return;
    }

    Job j;
    std::string why;
    if (!job_start(j, emu, com, JobBackground, why)) {
        report(name, false, "        " + why + "\n", passed, failed);
        unlink(com.c_str());
        return;
    }

    // A platform that lets a background tcsetattr through would run the rest of
    // this case green having tested nothing, so say so instead.
    bool stopped = job_wait_note(j, "STOP\n", 1, 4000);

    bool ok = true;
    if (stopped) {
        if (!job_wait_raw(j, true, 8000)) {
            detail += "        the terminal was still canonical after the fg:\n"
                      "        the tcsetattr SIGTTOU interrupted was never completed\n";
            ok = false;
        }
        if (ok) {
            send_spec(j.pty.master, "A");
            send_spec(j.pty.master, ".");
            job_wait_out(j, "2E ", 8000);
            if (j.out != "41 2E ") {
                detail += "        expected: 41 2E \n";
                detail += "        got:      " + show(j.out) + "\n";
                ok = false;
            }
        }
        if (ok && !job_finish(j, 5000)) {
            detail += "        the emulator had to be killed: it never finished\n";
            ok = false;
        } else if (!ok) {
            job_finish(j, 1000);
        }
    } else {
        job_finish(j, 1000);
    }

    pty_close(j.pty);
    unlink(com.c_str());

    if (!stopped) {
        printf("SKIP  %s\n", name);
        printf("        a background tcsetattr was not stopped by SIGTTOU here, so\n");
        printf("        there was nothing in this case for the emulator to survive\n");
        return;
    }
    report(name, ok, detail, passed, failed);
}

// ============================================================================
// Manual mode
//
// Everything above writes bytes into a pty, which steps past the terminal
// program itself.  If Terminal.app or iTerm binds a key for its own use the
// tests still pass and the user still loses the key.  This runs the same hex
// echo attached to the real terminal so a person can press keys and read what
// the guest received.
// ============================================================================

static int manual_mode(const std::string& emu, const std::string& com) {
    // Manual mode is a person pressing keys, so a redirected stdin has nothing
    // to offer it: the guest would read the file or the pipe and exit before
    // anyone typed anything.  Say so rather than run.
    if (!isatty(0)) {
        printf("SKIP  posix console --manual (stdin is not a terminal)\n");
        return 0;
    }
    printf("Press keys.  Each one prints the bytes the CP/M program received.\n");
    printf("  ^V should print 16 and ^O should print 0F: those are the two the\n");
    printf("  line discipline takes on macOS without the IEXTEN clear.\n");
    printf("  An arrow key prints its escape sequence, 1B 5B and a letter.\n");
    printf("Press '.' to finish.\n\n");
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) return 2;
    if (pid == 0) {
        char* argv[3];
        argv[0] = const_cast<char*>(emu.c_str());
        argv[1] = const_cast<char*>(com.c_str());
        argv[2] = NULL;
        execv(emu.c_str(), argv);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    printf("\n");
    return 0;
}

// ============================================================================

int main(int argc, char** argv) {
    bool manual = false;
    std::string emu;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--manual") == 0) manual = true;
        else emu = argv[i];
    }
    if (emu.empty()) emu = "../src/cpmemu";

    if (access(emu.c_str(), X_OK) != 0) {
        printf("SKIP  posix console (no emulator at %s)\n", emu.c_str());
        return 0;
    }

    // A machine with no pty to hand cannot run any of this, and saying so is
    // better than failing every case for the same reason
    {
        Pty probe;
        probe.master = probe.slave = -1;
        if (!pty_open(probe)) {
            printf("SKIP  posix console (no pty available: %s)\n", strerror(errno));
            return 0;
        }
        pty_close(probe);
    }

    const char* tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir) tmpdir = "/tmp";
    char tmpl[1024];
    snprintf(tmpl, sizeof tmpl, "%s%scpmemu_pty.XXXXXX", tmpdir,
             tmpdir[strlen(tmpdir) - 1] == '/' ? "" : "/");
    char* dirp = mkdtemp(tmpl);
    if (!dirp) {
        printf("FAIL  posix console (no temp directory: %s)\n", strerror(errno));
        return 1;
    }
    std::string dir = dirp;

    if (manual) {
        std::string com = dir + "/keys.com";
        if (!write_file(com, con6hex_com, sizeof con6hex_com)) {
            printf("FAIL  posix console (cannot write %s)\n", com.c_str());
            return 1;
        }
        int rc = manual_mode(emu, com);
        unlink(com.c_str());
        rmdir(dir.c_str());
        return rc;
    }

    // A guest that dies with its stdout pipe already closed would take the
    // harness with it, and a dead harness reports nothing at all
    signal(SIGPIPE, SIG_IGN);

    int passed = 0;
    int failed = 0;

    run_control(passed, failed);

    const size_t count = sizeof cases / sizeof cases[0];
    for (size_t i = 0; i < count; i++) {
        const Case& c = cases[i];
        char com[512];
        snprintf(com, sizeof com, "%s/case%02u.com", dir.c_str(), (unsigned)i);
        if (!write_file(com, c.prog, c.prog_len)) {
            printf("FAIL  %s\n        cannot write %s\n", c.name, com);
            failed++;
            continue;
        }

        Spawn s;
        s.emu = emu.c_str();
        s.com = com;
        s.emu_args = c.emu_args;
        s.keys_before = c.keys_before;
        s.keys = c.keys;
        s.stdin_kind = c.stdin_kind;
        s.stdin_text = c.stdin_text;
        s.stdout_to_pty = c.stdout_to_pty;
        s.timeout_ms = c.timeout_ms;
        s.kill_with = c.kill_with;
        s.collect_left = (c.want_left != NULL);

        Result r;
        bool ran = run_guest(s, dir, r);
        unlink(com);
        if (!ran) {
            printf("FAIL  %s\n        the emulator did not start\n", c.name);
            failed++;
            continue;
        }

        // The OPOST case is the only one whose output goes to the terminal
        const std::string& got = c.stdout_to_pty ? r.tty : r.out;

        // A signal case is only worth something if the emulator was in raw mode
        // when the signal arrived.  Without this it is possible to pass by
        // never getting there: the terminal the case compares is then the one
        // it started with, so "put back" is trivially true and the case proves
        // nothing.  That was masked while the two waits shared a budget,
        // because a startup slow enough to miss raw mode also ran the case out
        // of time; separating them takes the mask away.
        bool ok = !r.timed_out && r.termios_restored;
        if (ok && c.kill_with) ok = r.raw_seen;
        if (ok && c.want_out) ok = (got == c.want_out);
        if (ok && *c.want_err) ok = r.err.find(c.want_err) != std::string::npos;
        if (ok && c.want_left) ok = (r.left == c.want_left);

        if (ok) {
            printf("PASS  %s\n", c.name);
            passed++;
        } else {
            printf("FAIL  %s\n", c.name);
            if (r.timed_out) printf("        the emulator had to be killed: it never finished\n");
            if (!r.termios_restored) printf("        the terminal was not put back on exit\n");
            if (c.kill_with && !r.raw_seen)
                printf("        the emulator never reached raw mode, so the signal proved nothing\n");
            if (c.want_out) {
                printf("        expected: %s\n", show(c.want_out).c_str());
                printf("        got:      %s\n", show(got).c_str());
            }
            if (*c.want_err && r.err.find(c.want_err) == std::string::npos)
                printf("        stderr did not mention: %s\n", c.want_err);
            if (c.want_left && r.left != c.want_left) {
                printf("        the terminal should still hold: %s\n", show(c.want_left).c_str());
                printf("        it held:                        %s\n", show(r.left).c_str());
            }
            failed++;
        }
    }

    // Last, because both build a session of their own and one of them stops a
    // process: a case that goes wrong here should not be able to disturb the
    // table above by running before it.
    run_suspend_case(emu, dir, passed, failed);
    run_background_case(emu, dir, passed, failed);

    rmdir(dir.c_str());

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
