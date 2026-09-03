/*
 * Windows console tests for cpmemu.
 *
 * The Windows half of os/windows/platform.cc cannot be tested the way the
 * POSIX half is.  A pipe does not reach it at all: the extended key path sits
 * behind is_terminal(), and the reads there go to the console input buffer
 * rather than to stdin, so redirected input never touches the diamond table.
 * A cross-compile only proves the code builds.  wine is not a substitute
 * either, because its console layer does not reproduce console input
 * faithfully enough for a pass to mean anything.
 *
 * So this drives a real console.  It spawns cpmemu with stdin bound to the
 * console this process is attached to, writes the INPUT_RECORDs a keyboard
 * would produce into that console with WriteConsoleInput, and captures the
 * guest's stdout and stderr through pipes.  The scan codes below were measured
 * on a real console, not taken from documentation: Ctrl+Left really did reach
 * the old _getch() path as E0 73, and F1 as 00 3B.  The platform layer keys on
 * the virtual key code now, so what the scan codes are for here is keeping the
 * injected records the shape a keyboard produces.
 *
 * Two things a case can set beyond the keys.  code_page sets the console input
 * code page, which is what decides the bytes a character becomes: the cases
 * that name 437 and 65001 are the ones the old byte-at-a-time reader could not
 * survive.  ctrl_event sends a console control event, which is the only way to
 * reach ctrl+break at all - the emulator is started in a process group of its
 * own for those, or the event would come back and kill this harness too.
 *
 * What this does NOT prove: WriteConsoleInput puts records in the console
 * input buffer directly, so it steps past whatever the terminal program itself
 * does with a keystroke first.  If Windows Terminal ever binds ctrl+left for
 * its own use, these tests still pass and the user still loses the key.  Only
 * a person pressing keys can answer that, which is what --manual is for.
 *
 * Build and run:
 *     tests\win_console.bat
 * or, against a cpmemu built somewhere else:
 *     win_console.exe path\to\cpmemu.exe
 *     win_console.exe --manual path\to\cpmemu.exe    (type keys yourself)
 *     win_console.exe --require path\to\cpmemu.exe   (a skip is a failure)
 */

// fopen and snprintf are what this needs; the CRT wants the _s forms
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef ENABLE_QUICK_EDIT_MODE
#define ENABLE_QUICK_EDIT_MODE 0x0040
#endif
#ifndef ENABLE_EXTENDED_FLAGS
#define ENABLE_EXTENDED_FLAGS 0x0080
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

// The CP/M guest programs are shared with tests/pty_console.cc so the two
// harnesses run the same code and report comparable byte strings.
#include "con_guests.h"

// ============================================================================
// Key injection
// ============================================================================

static HANDLE g_con_in = INVALID_HANDLE_VALUE;

struct NamedKey {
    const char* name;
    WORD vk;
    WORD scan;      // what a PC keyboard reports, not the code _getch() returns
    WCHAR ch;
    DWORD ctrl;
    bool with_ctrl_key;
};

static const NamedKey named_keys[] = {
    { "Up",      VK_UP,     0x48, 0,  ENHANCED_KEY, false },
    { "Down",    VK_DOWN,   0x50, 0,  ENHANCED_KEY, false },
    { "Left",    VK_LEFT,   0x4B, 0,  ENHANCED_KEY, false },
    { "Right",   VK_RIGHT,  0x4D, 0,  ENHANCED_KEY, false },
    { "Home",    VK_HOME,   0x47, 0,  ENHANCED_KEY, false },
    { "End",     VK_END,    0x4F, 0,  ENHANCED_KEY, false },
    { "PgUp",    VK_PRIOR,  0x49, 0,  ENHANCED_KEY, false },
    { "PgDn",    VK_NEXT,   0x51, 0,  ENHANCED_KEY, false },
    { "Ins",     VK_INSERT, 0x52, 0,  ENHANCED_KEY, false },
    { "Del",     VK_DELETE, 0x53, 0,  ENHANCED_KEY, false },
    { "C-Left",  VK_LEFT,   0x4B, 0,  ENHANCED_KEY | LEFT_CTRL_PRESSED, true },
    { "C-Right", VK_RIGHT,  0x4D, 0,  ENHANCED_KEY | LEFT_CTRL_PRESSED, true },
    { "C-Up",    VK_UP,     0x48, 0,  ENHANCED_KEY | LEFT_CTRL_PRESSED, true },
    { "C-Down",  VK_DOWN,   0x50, 0,  ENHANCED_KEY | LEFT_CTRL_PRESSED, true },
    { "F1",      VK_F1,     0x3B, 0,  0, false },
    { "F2",      VK_F2,     0x3C, 0,  0, false },
    { "Enter",   VK_RETURN, 0x1C, 13, 0, false },
    { "Bksp",    VK_BACK,   0x0E, 8,  0, false },
    { "Esc",     VK_ESCAPE, 0x01, 27, 0, false },
    { "Space",   VK_SPACE,  0x39, 32, 0, false }
};

// Scan code of each letter key, so an injected ^X carries what a keyboard sends
static WORD letter_scan(char c) {
    static const char* letters = "qwertyuiopasdfghjklzxcvbnm";
    static const WORD code[] = { 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
                                 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
                                 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32 };
    char lower = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    const char* p = strchr(letters, lower);
    return p ? code[p - letters] : 0;
}

static bool write_records(INPUT_RECORD* r, DWORD n) {
    DWORD written = 0;
    return WriteConsoleInputW(g_con_in, r, n, &written) && written == n;
}

static bool inject_key(WORD vk, WORD scan, WCHAR ch, DWORD ctrl, bool with_ctrl_key) {
    INPUT_RECORD r[4];
    DWORD n = 0;
    ZeroMemory(r, sizeof(r));

    // A real ctrl+key sends the ctrl press first.  Injecting it too keeps the
    // record stream honest, and checks that a modifier-only event, which
    // yields no character, is not mistaken for one.
    if (with_ctrl_key) {
        r[n].EventType = KEY_EVENT;
        r[n].Event.KeyEvent.bKeyDown = TRUE;
        r[n].Event.KeyEvent.wRepeatCount = 1;
        r[n].Event.KeyEvent.wVirtualKeyCode = VK_CONTROL;
        r[n].Event.KeyEvent.wVirtualScanCode = 0x1D;
        r[n].Event.KeyEvent.dwControlKeyState = LEFT_CTRL_PRESSED;
        n++;
    }

    r[n].EventType = KEY_EVENT;
    r[n].Event.KeyEvent.bKeyDown = TRUE;
    r[n].Event.KeyEvent.wRepeatCount = 1;
    r[n].Event.KeyEvent.wVirtualKeyCode = vk;
    r[n].Event.KeyEvent.wVirtualScanCode = scan;
    r[n].Event.KeyEvent.uChar.UnicodeChar = ch;
    r[n].Event.KeyEvent.dwControlKeyState = ctrl;
    n++;
    r[n] = r[n - 1];
    r[n].Event.KeyEvent.bKeyDown = FALSE;
    n++;

    if (with_ctrl_key) {
        r[n].EventType = KEY_EVENT;
        r[n].Event.KeyEvent.bKeyDown = FALSE;
        r[n].Event.KeyEvent.wRepeatCount = 1;
        r[n].Event.KeyEvent.wVirtualKeyCode = VK_CONTROL;
        r[n].Event.KeyEvent.wVirtualScanCode = 0x1D;
        n++;
    }
    return write_records(r, n);
}

// One entry of a key script: a named key, ^X for a control character, a single
// literal character, U+XXXX for a character by code point, or wait:MS to leave
// a gap between keystrokes.
static bool inject_spec(const std::string& spec) {
    for (size_t i = 0; i < sizeof(named_keys) / sizeof(named_keys[0]); i++) {
        if (_stricmp(spec.c_str(), named_keys[i].name) == 0) {
            return inject_key(named_keys[i].vk, named_keys[i].scan, named_keys[i].ch,
                              named_keys[i].ctrl, named_keys[i].with_ctrl_key);
        }
    }
    if (spec.size() > 5 && spec.compare(0, 5, "wait:") == 0) {
        Sleep((DWORD)atoi(spec.c_str() + 5));
        return true;
    }
    if (spec.size() == 2 && spec[0] == '^') {
        char c = spec[1];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        return inject_key((WORD)c, letter_scan(c), (WCHAR)(c - 'A' + 1), LEFT_CTRL_PRESSED, true);
    }
    if (spec.size() == 1) {
        SHORT vks = VkKeyScanA(spec[0]);
        WORD vk = (WORD)LOBYTE(vks);
        return inject_key(vk, (WORD)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC),
                          (WCHAR)(unsigned char)spec[0],
                          (HIBYTE(vks) & 1) ? SHIFT_PRESSED : 0, false);
    }
    // U+XXXX names a character by code point, and U+XXXX+YYYY names several
    // sent back to back with nothing between them.  The single-character branch
    // above cannot reach any of these: VkKeyScanA takes a byte, so it stops at
    // U+00FF, and the cast is one byte wide as well.  A character no key on the
    // layout produces arrives with vk 0, scan 0 and only uChar.UnicodeChar set,
    // which is what a paste and an IME produce too.  Above the BMP that is two
    // records, one per UTF-16 code unit.
    //
    // The no-gap form matters on its own: the bug this was written for needed
    // two characters in the console buffer at once, and the harness sleeps
    // between key specs the way a person types.
    if (spec.size() > 2 && (spec[0] == 'U' || spec[0] == 'u') && spec[1] == '+') {
        const char* p = spec.c_str() + 2;
        while (*p) {
            char* end = NULL;
            unsigned long cp = strtoul(p, &end, 16);
            if (end == p || cp > 0x10FFFF) {
                fprintf(stderr, "win_console: bad code point in %s\n", spec.c_str());
                return false;
            }
            if (cp >= 0x10000) {
                unsigned long v = cp - 0x10000;
                if (!inject_key(0, 0, (WCHAR)(0xD800 + (v >> 10)), 0, false)) return false;
                if (!inject_key(0, 0, (WCHAR)(0xDC00 + (v & 0x3FF)), 0, false)) return false;
            } else {
                if (!inject_key(0, 0, (WCHAR)cp, 0, false)) return false;
            }
            p = (*end == '+') ? end + 1 : end;
        }
        return true;
    }
    fprintf(stderr, "win_console: unknown key spec %s\n", spec.c_str());
    return false;
}

// ============================================================================
// Running one case
// ============================================================================

struct Pipe {
    HANDLE read_end;
    HANDLE write_end;
    std::string text;
};

static bool make_pipe(Pipe& p) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&p.read_end, &p.write_end, &sa, 0)) return false;
    SetHandleInformation(p.read_end, HANDLE_FLAG_INHERIT, 0);  // the child gets the write end only
    return true;
}

static void drain(Pipe& p) {
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(p.read_end, NULL, 0, NULL, &avail, NULL) || avail == 0) return;
        char buf[4096];
        DWORD want = avail < sizeof(buf) ? avail : (DWORD)sizeof(buf);
        DWORD got = 0;
        if (!ReadFile(p.read_end, buf, want, &got, NULL) || got == 0) return;
        p.text.append(buf, got);
    }
}

struct Result {
    std::string out;
    std::string err;
    bool timed_out;
    bool mode_restored;
    DWORD exit_code;
};

static bool run_guest(const std::string& emu, const std::string& com, const char* emu_args,
                      const char* keys, bool vt_input, int timeout_ms,
                      HANDLE stdin_override, UINT code_page, DWORD ctrl_event,
                      Result& result) {
    result.timed_out = false;
    result.mode_restored = true;
    result.exit_code = 0;

    DWORD original_mode = 0;
    GetConsoleMode(g_con_in, &original_mode);
    DWORD entry_mode = original_mode;
    if (vt_input) {
        entry_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        SetConsoleMode(g_con_in, entry_mode);
    }
    // The console input code page decides what bytes a character becomes, and
    // it is a property of the console rather than of a process, so the child
    // reads back whatever is set here.  Nothing in this file used to set it at
    // all, which left every case running on whatever the machine was
    // configured for and the two code pages that break the input path
    // untested.  Put back below, next to the mode.
    UINT original_cp = GetConsoleCP();
    if (code_page != 0) {
        SetConsoleCP(code_page);
    }
    FlushConsoleInputBuffer(g_con_in);

    Pipe out;
    Pipe err;
    if (!make_pipe(out) || !make_pipe(err)) return false;

    std::string cmdline = "\"" + emu + "\" ";
    if (emu_args && *emu_args) cmdline += std::string(emu_args) + " ";
    cmdline += "\"" + com + "\"";

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    // The console, unless the case is one of the redirected-input contrasts.
    // For the console cases this has to be the console handle itself, or
    // is_terminal() is false and none of the code under test runs at all.
    si.hStdInput = stdin_override != INVALID_HANDLE_VALUE ? stdin_override : g_con_in;
    si.hStdOutput = out.write_end;
    si.hStdError = err.write_end;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::vector<char> mutable_cmd(cmdline.begin(), cmdline.end());
    mutable_cmd.push_back(0);
    // A control event goes to a process group, and this harness shares the
    // console with the child, so without a group of its own the break would
    // reach this process too and kill the run that is measuring it.
    DWORD flags = ctrl_event != 0 ? CREATE_NEW_PROCESS_GROUP : 0;
    if (!CreateProcessA(NULL, &mutable_cmd[0], NULL, NULL, TRUE, flags, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "win_console: cannot start %s (%lu)\n", emu.c_str(), GetLastError());
        return false;
    }
    CloseHandle(out.write_end);
    CloseHandle(err.write_end);

    DWORD start = GetTickCount();

    // Wait until the emulator has taken the console out of line mode before
    // typing at it.  Without this the first keys can land while the console is
    // still cooked, and the failure looks like a translation bug.
    // A redirected-input case never enters raw mode, so waiting for it there
    // would burn the whole timeout before the case even started
    while (stdin_override == INVALID_HANDLE_VALUE && GetTickCount() - start < (DWORD)timeout_ms) {
        DWORD m = 0;
        if (GetConsoleMode(g_con_in, &m) && (m & ENABLE_LINE_INPUT) == 0) break;
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;
        Sleep(2);
        drain(out);
        drain(err);
    }
    Sleep(60);

    for (const char* p = keys; p && *p; ) {
        const char* comma = strchr(p, ',');
        std::string spec(p, comma ? (size_t)(comma - p) : strlen(p));
        if (!spec.empty() && !inject_spec(spec)) break;
        Sleep(15);          // one key at a time, the way a person types
        drain(out);
        drain(err);
        if (!comma) break;
        p = comma + 1;
    }

    // Sent once the emulator is in raw mode and has had whatever keys the case
    // wanted, because the point of it is what the console is left in.
    // CTRL_BREAK_EVENT is the one that reaches a process whose console has
    // ENABLE_PROCESSED_INPUT cleared; CREATE_NEW_PROCESS_GROUP above also
    // disables ctrl+c for the child, so a CTRL_C_EVENT here would be ignored.
    if (ctrl_event != 0) {
        if (!GenerateConsoleCtrlEvent(ctrl_event, pi.dwProcessId)) {
            fprintf(stderr, "win_console: cannot send control event (%lu)\n", GetLastError());
        }
    }

    for (;;) {
        drain(out);
        drain(err);
        if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) break;
        if (GetTickCount() - start > (DWORD)timeout_ms) {
            result.timed_out = true;
            TerminateProcess(pi.hProcess, 99);
            WaitForSingleObject(pi.hProcess, 2000);
            break;
        }
    }
    drain(out);
    drain(err);

    DWORD exit_mode = 0;
    GetConsoleMode(g_con_in, &exit_mode);
    // A run that was killed never got to restore anything, so only a run that
    // finished on its own can be held to this
    result.mode_restored = result.timed_out || exit_mode == entry_mode;
    GetExitCodeProcess(pi.hProcess, &result.exit_code);

    SetConsoleMode(g_con_in, original_mode);   // never leave the console changed
    SetConsoleCP(original_cp);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(out.read_end);
    CloseHandle(err.read_end);

    result.out = out.text;
    result.err = err.text;
    return true;
}

// ============================================================================
// Cases
// ============================================================================

struct Case {
    const char* name;
    const unsigned char* prog;
    size_t prog_len;
    const char* keys;
    const char* emu_args;
    const char* want_out;       // exact match against the guest's stdout
    const char* want_err;       // substring of stderr, or "" for no requirement
    bool vt_input;
    int timeout_ms;
    // When set, the guest's stdin is a file holding these bytes instead of the
    // console.  Not a console case, but the same status call answers for both,
    // and the file half of it has no other test anywhere.
    const char* stdin_text;
    // When set, the guest's stdin is NUL: a character device, which is what
    // separates "is this a console" from "is this a character device".
    bool stdin_nul;
    // Console input code page for this case, or 0 to leave the machine's own
    // alone.  The bytes a character becomes are decided here and nowhere else.
    UINT code_page;
    // When set, this control event is sent to the emulator once it is in raw
    // mode and has had its keys.  CTRL_BREAK_EVENT is the one that gets past a
    // cleared ENABLE_PROCESSED_INPUT.
    DWORD ctrl_event;
};

#define PROG(p) p, sizeof(p)

static const Case cases[] = {
    // Every entry of the extended key table, in table order, as one string.
    { "diamond: every extended key reaches the guest", PROG(con6hex_com),
      "Up,Down,Left,Right,Home,End,PgUp,PgDn,Ins,Del,C-Left,C-Right,C-Up,C-Down,.", "",
      "05 18 13 04 11 13 11 04 12 03 16 07 01 06 17 1A 2E ", "", false, 15000 },

    // Home and End are two characters from one key.  The second one is in the
    // platform layer's queue where _kbhit() cannot see it, so a status call
    // that only asked _kbhit() would lose it.
    { "diamond: Home and End deliver both characters", PROG(con6hex_com),
      "Home,End,.", "", "11 13 11 04 2E ", "", false, 15000 },

    // ^R, ^V and ^O are the three the POSIX side needed an IEXTEN clear for.
    { "control keys reach the guest untouched", PROG(con6hex_com),
      "^R,^V,^O,^S,^Q,^Z,.", "", "12 16 0F 13 11 1A 2E ", "", false, 15000 },

    { "an untranslated special key is not a character", PROG(con6hex_com),
      "F1,F2,A,.", "", "41 2E ", "", false, 15000 },

    // PgDn is ^C.  Five of them must not be read as someone asking to leave.
    { "five page downs are not five ^C", PROG(con6hex_com),
      "PgDn,PgDn,PgDn,PgDn,PgDn,.", "", "03 03 03 03 03 2E ", "", false, 15000 },

    // The escape hatch itself still has to work.  The fifth ^C exits before
    // the guest can print it, so only four appear.
    { "five typed ^C exit the emulator", PROG(con6hex_com),
      "^C,^C,^C,^C,^C", "", "03 03 03 03 ", "5 consecutive ^C received", false, 15000 },

    { "^C spread past the window does not exit", PROG(con6hex_com),
      "^C,wait:800,^C,wait:800,^C,wait:800,^C,wait:800,^C,wait:200,.", "",
      "03 03 03 03 03 2E ", "", false, 25000 },

    { "--no-ctrl-c-exit hands every ^C to the guest", PROG(con6hex_com),
      "^C,^C,^C,^C,^C,^C,.", "--no-ctrl-c-exit", "03 03 03 03 03 03 2E ", "", false, 15000 },

    { "the blocking read waits past a key that means nothing", PROG(con1hex_com),
      "F1,F2,Up,A,.", "", "05 41 2E ", "", false, 15000 },

    { "BIOS CONST and CONIN see the same keys as BDOS", PROG(bioshex_com),
      "Up,Down,Home,PgUp,Ins,.", "", "05 18 11 13 12 16 2E ", "", false, 15000 },

    // The status call has to be honest about a key it cannot turn into a
    // character.  When it is not, this prints nothing at all and times out:
    // the guest is stuck in CONIN waiting for a keystroke it was promised.
    // The leading S releases the blocking read this guest starts with, so the
    // key that matters is already in the console buffer when the poll begins.
    { "one F1 does not stall a status-then-read guest", PROG(conststall_com),
      "S,F1", "", "T", "", false, 20000 },

    { "a real key still arrives after an ignored one", PROG(conststall_com),
      "S,F1,A", "", "41 T", "", false, 20000 },

    { "a quiet console reports a quiet console", PROG(conststall_com),
      "S", "", "T", "", false, 20000 },

    // A synthesized character is the key the user pressed, not an instruction
    // to the line editor, so Down is stored rather than killing the line.
    { "the line editor stores an arrow, does not obey it", PROG(con10buf_com),
      "A,B,Down,Enter", "", "AB^X\r\n41 42 18 ", "", false, 15000 },

    // Console input mode is a property of the console, not of a process, so
    // this bit can be left on by whatever ran here before.  With it on the
    // console hands _getch() VT sequences and the diamond table never fires.
    { "the diamond survives an inherited VT input mode", PROG(con6hex_com),
      "Up,Down,Home,^R,.", "", "05 18 11 13 12 2E ", "", true, 15000 },

    { "stdout is binary, so CR LF is two bytes", PROG(crlf_com),
      "", "", "\r\n", "", false, 10000 },

    // Not a console case, but the same status call answers for it.
    // PeekNamedPipe fails outright on a handle that is not a pipe, and reading
    // that failure as "no input" left a guest polling BDOS 6 forever with its
    // input sitting unread in the file.  The identical bytes through a pipe
    // always worked, which is how it stayed hidden.
    { "polled input arrives from a file, not only from a pipe", PROG(con6hex_com),
      "", "", "41 42 2E ", "", false, 15000, "AB." },

    { "the blocking read still takes its bytes from a file", PROG(con1hex_com),
      "", "", "41 42 2E ", "", false, 15000, "AB." },

    { "an empty file is end of input: CR once, then ^Z", PROG(coneof_com),
      "", "", "0D 1A ", "", false, 15000, "" },

    // _isatty() is true for any character device, so NUL used to be taken for
    // a console: the read went to the keyboard, the redirect was ignored, and
    // end of input never came.  `cpmemu prog.com < NUL` hung, and so did the
    // </dev/null cases tests/run_tests.sh already had.
    { "NUL is end of input too, not the keyboard", PROG(coneof_com),
      "", "", "0D 1A ", "", false, 15000, NULL, true },

    // The code page cases.
    //
    // These are what the old _getch() path could not survive.  It read one byte
    // at a time and took 0x00 or 0xE0 for a special key prefix, with !_kbhit()
    // as the tie-breaker, so a character whose first byte is 0xE0 was a guess
    // that code page 437 lost intermittently and 65001 lost every time.
    //
    // The bytes expected below are what the guest receives after the `& 0x7F`
    // in bdos_direct_console_io - alpha is E0 in code page 437 and the guest
    // sees 60.  Whether CP/M should see eight bits is settled: the masks stay,
    // so these expectations are final.  A mismatch here is a fault in the
    // console path, not a decision still to be made, and that is the point of
    // writing them out as bytes.  The POSIX twins of these are the seven-bit
    // cases in tests/pty_console.cc, which do run.
    { "an E0 character is a character, not a key prefix", PROG(con6hex_com),
      "U+03B1,.", "", "60 2E ", "", false, 15000, NULL, false, 437 },

    // The one that used to lose both characters: E0 41 reached _getch() as a
    // prefix and a scan code, no table entry matched 0x41, and the guest got
    // neither.  U+03B1+0041 is one injection, so both are in the console buffer
    // together the way they have to be to reproduce it.
    { "an E0 character followed by another loses neither", PROG(con6hex_com),
      "U+03B1+0041,.", "", "60 41 2E ", "", false, 15000, NULL, false, 437 },

    // Code page 65001 turned the race into a certainty: U+0E01 encodes as
    // E0 B8 81 and the old path handed those back one byte at a time.
    { "a three byte UTF-8 character arrives whole", PROG(con6hex_com),
      "U+0E01,.", "", "60 38 01 2E ", "", false, 15000, NULL, false, 65001 },

    // Outside the BMP a character is two records, one per UTF-16 code unit, and
    // only reading them as a pair produces the four UTF-8 bytes.  U+10441 is
    // picked so no byte of it masks to 0x00, which BDOS 6 reads as "no
    // character" and would drop.
    { "a surrogate pair is one character, not two", PROG(con6hex_com),
      "U+10441,.", "", "70 10 11 01 2E ", "", false, 15000, NULL, false, 65001 },

    // ctrl+break.
    //
    // It ends the process without running atexit(), so the console mode is put
    // back by a SetConsoleCtrlHandler or not at all.  With no handler the child
    // exits 0xC000013A with the mode still raw and the shell left with no echo;
    // this case fails on the console mode, not on the output.  The guest loops
    // for ever waiting for '.', so a break that does nothing shows up as the
    // timeout instead.
    { "the console is put back on a ctrl+break", PROG(con6hex_com),
      "A,wait:200", "", "41 ", "", false, 15000, NULL, false, 0, CTRL_BREAK_EVENT }
};

// ============================================================================
// Reporting
// ============================================================================

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
            snprintf(buf, sizeof(buf), "\\x%02X", c);
            out += buf;
        } else out += (char)c;
    }
    return out;
}

static bool write_com(const std::string& path, const unsigned char* data, size_t len) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, len, f) == len;
    fclose(f);
    return ok;
}

// ============================================================================
// Manual mode
// ============================================================================

// Injection writes records straight into the console input buffer, which steps
// past whatever the terminal program does with a keystroke first.  This runs
// the same hex echo with the console attached so a person can press keys and
// read the codes the guest actually receives.
static int manual_mode(const std::string& emu, const std::string& com) {
    printf("Press keys.  Each one prints the bytes the CP/M program received.\n");
    printf("  arrows 05 18 13 04   Home 11 13   End 11 04   PgUp 12   PgDn 03\n");
    printf("  Ins 16   Del 07   ctrl+arrows 01 06 17 1A   ctrl+R 12   ctrl+O 0F\n");
    printf("An F key should print nothing at all.  Press '.' to finish.\n\n");
    fflush(stdout);

    std::string cmdline = "\"" + emu + "\" \"" + com + "\"";
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::vector<char> mutable_cmd(cmdline.begin(), cmdline.end());
    mutable_cmd.push_back(0);
    if (!CreateProcessA(NULL, &mutable_cmd[0], NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "win_console: cannot start %s (%lu)\n", emu.c_str(), GetLastError());
        return 2;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    printf("\n");
    return 0;
}

// ============================================================================

int main(int argc, char** argv) {
    bool manual = false;
    // --require says a skip is a failure.  Every skip below is a case of "this
    // machine cannot run these tests", which is the right answer on a laptop
    // and the wrong one in CI, where these tests are the entire point of the
    // job and exiting 0 having run none of them is indistinguishable from
    // having run them all.  tests\win_console.bat passes it when
    // CPMEMU_REQUIRE_MSVC is set.
    bool require = false;
    std::string emu;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--manual") == 0) manual = true;
        else if (strcmp(argv[i], "--require") == 0) require = true;
        else emu = argv[i];
    }
    if (emu.empty()) emu = "..\\src\\cpmemu.exe";

    if (GetFileAttributesA(emu.c_str()) == INVALID_FILE_ATTRIBUTES) {
        printf("%s  windows console (no emulator at %s)\n",
               require ? "FAIL" : "SKIP", emu.c_str());
        return require ? 1 : 0;
    }

    // A console with no window is normal under a pseudoconsole, so ask CONIN$
    // rather than the window, and allocate one only when there is none at all.
    g_con_in = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (g_con_in == INVALID_HANDLE_VALUE) {
        if (!AllocConsole()) {
            printf("%s  windows console (no console to drive)\n",
                   require ? "FAIL" : "SKIP");
            return require ? 1 : 0;
        }
        HWND w = GetConsoleWindow();
        if (w) ShowWindow(w, SW_HIDE);
        g_con_in = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (g_con_in == INVALID_HANDLE_VALUE) {
            printf("%s  windows console (no console to drive)\n",
                   require ? "FAIL" : "SKIP");
            return require ? 1 : 0;
        }
    }
    // The child's stdin must be inheritable or CreateProcess hands it nothing
    SetHandleInformation(g_con_in, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    char temp_root[MAX_PATH];
    if (!GetTempPathA(sizeof(temp_root), temp_root)) {
        printf("FAIL  windows console (no temp directory)\n");
        return 1;
    }
    std::string dir = std::string(temp_root) + "cpmemu_wincon";
    CreateDirectoryA(dir.c_str(), NULL);

    if (manual) {
        std::string com = dir + "\\keys.com";
        if (!write_com(com, con6hex_com, sizeof(con6hex_com))) {
            printf("FAIL  windows console (cannot write %s)\n", com.c_str());
            return 1;
        }
        int rc = manual_mode(emu, com);
        DeleteFileA(com.c_str());
        RemoveDirectoryA(dir.c_str());
        return rc;
    }

    int passed = 0;
    int failed = 0;
    const size_t count = sizeof(cases) / sizeof(cases[0]);
    for (size_t i = 0; i < count; i++) {
        const Case& c = cases[i];
        char com[MAX_PATH];
        snprintf(com, sizeof(com), "%s\\case%02u.com", dir.c_str(), (unsigned)i);
        if (!write_com(com, c.prog, c.prog_len)) {
            printf("FAIL  %s\n        cannot write %s\n", c.name, com);
            failed++;
            continue;
        }

        // A redirected-input case needs a real file handle, and it has to be
        // inheritable or the child is handed nothing
        char in_path[MAX_PATH];
        in_path[0] = 0;
        HANDLE stdin_file = INVALID_HANDLE_VALUE;
        SECURITY_ATTRIBUTES nul_sa;
        nul_sa.nLength = sizeof(nul_sa);
        nul_sa.lpSecurityDescriptor = NULL;
        nul_sa.bInheritHandle = TRUE;
        if (c.stdin_nul) {
            stdin_file = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     &nul_sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (stdin_file == INVALID_HANDLE_VALUE) {
                printf("FAIL  %s\n        cannot open NUL (%lu)\n", c.name, GetLastError());
                DeleteFileA(com);
                failed++;
                continue;
            }
        } else if (c.stdin_text) {
            snprintf(in_path, sizeof(in_path), "%s\\case%02u.in", dir.c_str(), (unsigned)i);
            if (!write_com(in_path, (const unsigned char*)c.stdin_text, strlen(c.stdin_text))) {
                printf("FAIL  %s\n        cannot write %s\n", c.name, in_path);
                failed++;
                continue;
            }
            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(sa);
            sa.lpSecurityDescriptor = NULL;
            sa.bInheritHandle = TRUE;
            stdin_file = CreateFileA(in_path, GENERIC_READ, FILE_SHARE_READ, &sa,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            // Falling through with no handle would quietly run the case
            // against the console instead, and it would pass having tested
            // nothing of what it is named for
            if (stdin_file == INVALID_HANDLE_VALUE) {
                printf("FAIL  %s\n        cannot open %s (%lu)\n", c.name, in_path, GetLastError());
                DeleteFileA(in_path);
                DeleteFileA(com);
                failed++;
                continue;
            }
        }

        Result r;
        bool ran = run_guest(emu, com, c.emu_args, c.keys, c.vt_input, c.timeout_ms,
                             stdin_file, c.code_page, c.ctrl_event, r);
        if (stdin_file != INVALID_HANDLE_VALUE) {
            CloseHandle(stdin_file);
            if (in_path[0]) DeleteFileA(in_path);
        }
        DeleteFileA(com);
        if (!ran) {
            printf("FAIL  %s\n        the emulator did not start\n", c.name);
            failed++;
            continue;
        }

        bool ok = !r.timed_out && r.out == c.want_out && r.mode_restored;
        if (ok && *c.want_err) ok = r.err.find(c.want_err) != std::string::npos;

        if (ok) {
            printf("PASS  %s\n", c.name);
            passed++;
        } else {
            printf("FAIL  %s\n", c.name);
            if (r.timed_out) printf("        the emulator had to be killed: it never finished\n");
            if (!r.mode_restored) printf("        the console mode was not put back on exit\n");
            if (c.ctrl_event) printf("        exit code: 0x%08lX\n", (unsigned long)r.exit_code);
            printf("        expected: %s\n", show(c.want_out).c_str());
            printf("        got:      %s\n", show(r.out).c_str());
            if (*c.want_err && r.err.find(c.want_err) == std::string::npos)
                printf("        stderr did not mention: %s\n", c.want_err);
            failed++;
        }
    }
    RemoveDirectoryA(dir.c_str());

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
