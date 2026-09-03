/*
 * Windows Platform Implementation
 */

#include "../platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

// Some older MinGW wincon.h headers are missing these console mode bits
#ifndef ENABLE_QUICK_EDIT_MODE
#define ENABLE_QUICK_EDIT_MODE 0x0040
#endif
#ifndef ENABLE_EXTENDED_FLAGS
#define ENABLE_EXTENDED_FLAGS 0x0080
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

namespace platform {

// ============================================================================
// Terminal State
// ============================================================================

static HANDLE hStdin = INVALID_HANDLE_VALUE;
static DWORD original_console_mode = 0;
static bool console_mode_saved = false;

void disable_raw_mode() {
    if (console_mode_saved && hStdin != INVALID_HANDLE_VALUE) {
        // ENABLE_EXTENDED_FLAGS has to be set in the same call or the quick edit
        // bit of the saved mode is ignored, so the user would be left with quick
        // edit off after we exit
        SetConsoleMode(hStdin, original_console_mode | ENABLE_EXTENDED_FLAGS);
        // console_mode_saved is deliberately NOT cleared here.  It means "there
        // is a console mode to put back", which stays true, and clearing it
        // would mean "we already put it back, so there is nothing left to do" -
        // a different claim, and a false one if the restore is ever raced.
        // This matters more here than the same line did on POSIX, because this
        // function has two callers that can run at once: atexit(), on the main
        // thread, and console_ctrl_handler(), which the OS runs on a thread it
        // makes for the purpose.  os/linux/platform.cc:unapply_raw_mode()
        // records what the equivalent cost there - one failure in 120 runs of
        // the seven kill cases, widening to all seven every time with the
        // window opened to 50ms on purpose.  Restoring a console that is
        // already restored writes back the mode it is already in.
    }
}

// The console events that end the process without running atexit(), which
// leaves the console exactly as we set it: no echo, no line editing, and a
// shell the user has to reset by hand.  This is the Windows face of the
// restore_signals[] list in os/linux/platform.cc, and it is the only way to
// reach ctrl+break at all.
//
// Clearing ENABLE_PROCESSED_INPUT keeps ^C away from here - it becomes an
// ordinary 03 for the guest, which is what WordStar wants - but ctrl+break is
// not gated on that bit, and neither is the console window closing, a logoff or
// a shutdown.  With no handler installed the default one ends the process,
// atexit() never runs and disable_raw_mode() never runs with it: measured, the
// child exits 0xC000013A and the console mode stays at 0x1A0 instead of
// returning to 0x1F7, which leaves the shell with no echo.
//
// Returning FALSE hands the event to the next handler and ends at the default
// one, so the process still dies of what it was sent and the exit code a caller
// sees is unchanged - the same reason the POSIX handler re-raises with the
// default disposition rather than exiting tidily.  CTRL_C_EVENT is on the list
// because another process can still send one with GenerateConsoleCtrlEvent even
// though the keyboard cannot.
//
// This runs on a thread the OS makes for it, so it does the one thing that is
// worth doing from there and nothing else: no buffered output is flushed and no
// state is saved, neither of which would be safe.
static BOOL WINAPI console_ctrl_handler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            disable_raw_mode();
            break;
        default:
            break;
    }
    return FALSE;
}

void enable_raw_mode() {
    if (!is_terminal()) {
        return;
    }

    if (hStdin == INVALID_HANDLE_VALUE) {
        hStdin = GetStdHandle(STD_INPUT_HANDLE);
    }

    if (!console_mode_saved) {
        // Only claim to have a console mode to restore if we actually read
        // one.  Going ahead regardless leaves original_console_mode at 0, and
        // disable_raw_mode() would then write 0 | ENABLE_EXTENDED_FLAGS to the
        // console on the way out: no line input, no echo, no processed input,
        // and a shell the user has to reset by hand.  The POSIX side guards
        // its tcgetattr for the same reason and says so - pushing back a
        // zeroed struct is worse than not restoring at all.
        if (!GetConsoleMode(hStdin, &original_console_mode)) {
            return;
        }
        console_mode_saved = true;
        atexit(disable_raw_mode);
        // Nothing to preserve the way the POSIX loop preserves SIG_IGN: a
        // process that wants ctrl+c ignored says so with
        // SetConsoleCtrlHandler(NULL, TRUE), which is a separate flag the
        // events never get past, so adding a handler cannot override it.  A
        // failure here is not worth reporting - it costs the restore, not the
        // run.
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    }

    // Disable line input, echo, and processed input (Ctrl+C handling)
    DWORD raw_mode = original_console_mode;
    raw_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    // Disable quick edit, which is on by default on Windows 10 and later.  With
    // it on, a stray mouse click starts a selection that freezes guest input
    // until the user presses Esc, and while a selection exists Windows Terminal
    // makes ctrl+c copy instead of falling through, so ^C is stolen from the
    // guest in that state.  ENABLE_EXTENDED_FLAGS must be set in the same call
    // for the quick edit bit to be honoured.
    raw_mode |= ENABLE_EXTENDED_FLAGS;
    raw_mode &= ~(ENABLE_QUICK_EDIT_MODE | ENABLE_MOUSE_INPUT);
    // The mode is inherited, so this bit can already be on from whatever ran in
    // this console before us.  With it on the console turns a key into its VT
    // sequence before anything here sees it, so Up arrives as three ordinary
    // characters, 1B 5B 41, and the extended key table below never fires at
    // all: measured, not guessed.  It was measured through _getch(), which the
    // read path no longer uses, but the records ReadConsoleInputW returns carry
    // those same three characters one at a time.  The whole console path here
    // decodes keys, so it has to be off.  disable_raw_mode() puts it back with
    // the rest of the saved mode.
    raw_mode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(hStdin, raw_mode);
}

bool is_terminal() {
    // Not _isatty(): the CRT sets that for any character device, NUL included,
    // so `cpmemu prog.com < NUL` was called a console.  The read then went to
    // _getch(), which opened CONIN$ itself and handed the guest whatever was
    // typed at the real keyboard while the redirect was ignored, and a guest
    // waiting for end of input waited for ever.  Asking the console for its
    // mode asks the question that is actually meant: a pipe, a file and NUL all
    // fail it.
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}

// ============================================================================
// Extended Key Translation
// ============================================================================

// A special key carries no character, so it is recognised by its virtual key
// code together with whether ctrl was down, and translated to the WordStar
// diamond so an arrow key arrives as one control character the guest
// understands.
//
// The keys and the translations are unchanged; only the key of the table
// moved.  These used to be CRT scan codes - the byte a second _getch() returns
// after a 0x00 or 0xE0 prefix - and a scan code and a virtual key code are
// different numbers for the same key: Ctrl+Left was scan 0x73 and is VK_LEFT
// with ctrl down.  Nothing here separates the arrow pad from the numeric
// keypad, which is deliberate: _getch() returned 00 4B for keypad left and
// E0 4B for the arrow, and both matched the one scan code entry, so both still
// match the one VK entry.
struct ExtendedKey {
    WORD vk;     // virtual key code from the input record
    bool ctrl;   // whether this entry wants ctrl down or wants it up
    int first;   // character handed to the guest
    int second;  // second character of the translation, or -1 if there is none
};

// Insert maps to ^V on purpose: Windows Terminal binds ctrl+v to paste and never
// lets it reach the application (microsoft/terminal#16280), so Insert is the only
// way a WordStar user can reach insert/overtype mode.
// PgDn maps to ^C, which is why console_last_char_synthesized() exists: without
// it, five page downs would trip the five-consecutive-^C emulator exit.
static const ExtendedKey extended_keys[] = {
    { VK_UP,     false, 0x05,   -1 },  // Up         -> ^E
    { VK_DOWN,   false, 0x18,   -1 },  // Down       -> ^X
    { VK_LEFT,   false, 0x13,   -1 },  // Left       -> ^S
    { VK_RIGHT,  false, 0x04,   -1 },  // Right      -> ^D
    { VK_HOME,   false, 0x11, 0x13 },  // Home       -> ^Q ^S
    { VK_END,    false, 0x11, 0x04 },  // End        -> ^Q ^D
    { VK_PRIOR,  false, 0x12,   -1 },  // PgUp       -> ^R
    { VK_NEXT,   false, 0x03,   -1 },  // PgDn       -> ^C
    { VK_INSERT, false, 0x16,   -1 },  // Insert     -> ^V
    { VK_DELETE, false, 0x07,   -1 },  // Delete     -> ^G
    { VK_LEFT,   true,  0x01,   -1 },  // Ctrl+Left  -> ^A  word left
    { VK_RIGHT,  true,  0x06,   -1 },  // Ctrl+Right -> ^F  word right
    { VK_UP,     true,  0x17,   -1 },  // Ctrl+Up    -> ^W  scroll up one line
    { VK_DOWN,   true,  0x1A,   -1 }   // Ctrl+Down  -> ^Z  scroll down one line
};

// Characters a key has already produced but the guest has not taken yet.  Two
// slots were enough while one key could only be two characters - Home is ^Q ^S
// - but one keystroke is now up to four bytes: console input code page 65001
// encodes a single character as up to four, and a character outside the BMP
// arrives as a surrogate pair which encodes to four.  Eight is past anything
// one keystroke can produce.  A repeat count is not expanded in here; see
// pending_repeats below.  Nothing the console can be asked can see this queue,
// so without it the second half of Home and End is invisible to BDOS 6 and
// BIOS CONST.
static const int queue_slots = 8;
static int queued_chars[queue_slots];
static bool queued_synth[queue_slots];
static int queued_count = 0;
static bool last_synthesized = false; // last character came from a special key

static void queue_add(int ch, bool synthesized) {
    if (queued_count < queue_slots) {
        queued_chars[queued_count] = ch;
        queued_synth[queued_count] = synthesized;
        queued_count++;
    }
}

static int queue_take() {
    int ch = queued_chars[0];
    last_synthesized = queued_synth[0];
    for (int i = 1; i < queued_count; i++) {
        queued_chars[i - 1] = queued_chars[i];
        queued_synth[i - 1] = queued_synth[i];
    }
    queued_count--;
    return ch;
}

// The console input handle.  enable_raw_mode() fills hStdin in, but the read
// path must not depend on having been through it.  STD_INPUT_HANDLE rather
// than CONIN$ on purpose: is_terminal() has already established that stdin is
// a console, and it was _getch() opening CONIN$ whatever stdin happened to be
// that made `cpmemu prog.com < NUL` read the keyboard.
static HANDLE console_handle() {
    if (hStdin == INVALID_HANDLE_VALUE) {
        hStdin = GetStdHandle(STD_INPUT_HANDLE);
    }
    return hStdin;
}

// A key held down arrives as one record with wRepeatCount above 1.  Keeping
// the record and replaying it one press at a time costs nothing, where
// expanding 0xFFFF repeats would need a queue that size.
static INPUT_RECORD pending_record;
static DWORD pending_repeats = 0;

// A character outside the BMP arrives as two records, one per UTF-16 code
// unit.  The first is held here until the second turns up.
static WCHAR pending_high_surrogate = 0;

// What one input record turned into.
enum RecordResult {
    record_error   = -1,  // the console handle failed: treat it as end of input
    record_ignored = 0,   // a key release, a modifier, a focus change
    record_queued  = 1,   // at least one character is in the queue now
    record_nothing = 2    // a key press that means nothing here, an F key say
};

// Encode one UTF-16 character in the console input code page and queue its
// bytes.  This is the part _getch() could not do: it hands back one byte at a
// time with nothing to say whether a byte is a whole character or the first of
// several, which is why 0xE0 alone was taken for a special key prefix.  In code
// page 437 0xE0 is alpha; in 65001 it leads every character from U+0800 to
// U+FFFF.
static int queue_encoded(const WCHAR* units, int count) {
    char bytes[8];
    // NULL for both of the trailing arguments is required rather than tidy: a
    // default character is rejected outright for CP_UTF8 and CP_UTF7.
    int n = WideCharToMultiByte(GetConsoleCP(), 0, units, count,
                                bytes, (int)sizeof(bytes), NULL, NULL);
    if (n <= 0) return record_nothing;
    for (int i = 0; i < n; i++) {
        queue_add((unsigned char)bytes[i], false);
    }
    return record_queued;
}

// A modifier on its own is a key press with no character and no meaning here.
// _getch() never reported one; the record stream does, so they are dropped here
// rather than reaching the guest as a synthesized 0.
static bool is_modifier_key(WORD vk) {
    switch (vk) {
        case VK_SHIFT:   case VK_CONTROL:  case VK_MENU:
        case VK_LSHIFT:  case VK_RSHIFT:
        case VK_LCONTROL: case VK_RCONTROL:
        case VK_LMENU:   case VK_RMENU:
        case VK_LWIN:    case VK_RWIN:     case VK_APPS:
        case VK_CAPITAL: case VK_NUMLOCK:  case VK_SCROLL:
            return true;
        default:
            return false;
    }
}

// Take one record from the console and queue what the guest should see for it.
// Blocks when the console is empty, so a caller that must not block asks
// console_input_waiting() first.
static int consume_one_record() {
    INPUT_RECORD rec;

    if (pending_repeats > 0) {
        rec = pending_record;
        pending_repeats--;
    } else {
        HANDLE h = console_handle();
        if (h == INVALID_HANDLE_VALUE) return record_error;
        DWORD got = 0;
        if (!ReadConsoleInputW(h, &rec, 1, &got) || got != 1) return record_error;
        if (rec.EventType != KEY_EVENT) return record_ignored;
        if (!rec.Event.KeyEvent.bKeyDown) {
            // The one key release that carries a character: alt plus a numeric
            // keypad code point is delivered by the console on the alt release.
            // Not measured here - nobody on this side has a Windows keyboard -
            // but a key-down filter with no exception for it would lose the
            // whole alt+numpad path, which _getch() did deliver.
            if (rec.Event.KeyEvent.wVirtualKeyCode == VK_MENU &&
                rec.Event.KeyEvent.uChar.UnicodeChar != 0) {
                WCHAR alt = rec.Event.KeyEvent.uChar.UnicodeChar;
                return queue_encoded(&alt, 1);
            }
            return record_ignored;
        }
        WORD repeats = rec.Event.KeyEvent.wRepeatCount;
        if (repeats > 1) {
            pending_record = rec;
            pending_repeats = (DWORD)(repeats - 1);
        }
    }

    const KEY_EVENT_RECORD& key = rec.Event.KeyEvent;
    WCHAR ch = key.uChar.UnicodeChar;

    if (ch >= 0xD800 && ch <= 0xDBFF) {
        pending_high_surrogate = ch;
        return record_ignored;
    }
    if (pending_high_surrogate != 0) {
        WCHAR pair[2] = { pending_high_surrogate, ch };
        pending_high_surrogate = 0;
        if (ch >= 0xDC00 && ch <= 0xDFFF) {
            return queue_encoded(pair, 2);
        }
        // A high surrogate with nothing after it is not a character.  Whatever
        // did arrive is still translated below on its own account.
    }
    if (ch >= 0xDC00 && ch <= 0xDFFF) {
        return record_nothing;  // a low surrogate alone encodes to nothing
    }
    if (ch != 0) {
        return queue_encoded(&ch, 1);
    }
    if (is_modifier_key(key.wVirtualKeyCode)) {
        return record_ignored;
    }

    bool ctrl = (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    const size_t count = sizeof(extended_keys) / sizeof(extended_keys[0]);
    for (size_t i = 0; i < count; i++) {
        if (extended_keys[i].vk == key.wVirtualKeyCode &&
            extended_keys[i].ctrl == ctrl) {
            // Whatever goes in the queue now was made up by us, not typed
            queue_add(extended_keys[i].first, true);
            if (extended_keys[i].second >= 0) {
                queue_add(extended_keys[i].second, true);
            }
            return record_queued;
        }
    }
    return record_nothing;  // an F key, say: not a character, and not an error
}

// True when consume_one_record() can run without blocking.  This replaces
// _kbhit(), which answered for the CRT's own console buffer; nothing here uses
// that buffer any more.
static bool console_input_waiting() {
    if (pending_repeats > 0) return true;
    HANDLE h = console_handle();
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD events = 0;
    return GetNumberOfConsoleInputEvents(h, &events) != 0 && events > 0;
}

bool stdin_has_data() {
    if (queued_count > 0) {
        return true;
    }

    if (!is_terminal()) {
        // PeekNamedPipe fails outright on a handle that is not a pipe, and the
        // old code read that failure as "no input".  With stdin redirected from
        // a file rather than a pipe, every status call therefore said no, so a
        // guest polling BDOS 6 sat there forever with its input sitting on disk
        // unread - while the same bytes through a pipe worked.  Ask what the
        // handle is first.
        //
        // "Readable" means "a read will not block", not "a byte will come
        // back", and the difference is the whole of the end-of-input path.
        // os/linux/platform.cc:stdin_has_data() says the same at length and
        // for the same reason: the caller reads, gets 0, and counts it, and
        // the 1024-read give-up in cpmemu.cc ends a run the guest cannot end
        // itself, because BDOS 6 spells "no character" and "end of input" both
        // as 0.  Answering false at the end of input makes the read
        // unreachable, so note_console_eof() never counts and the give-up can
        // never fire - the guest spins until it is killed.  That is the bug
        // this file had in all three redirected shapes while the POSIX side
        // had already fixed it.
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        DWORD type = GetFileType(h) & ~(DWORD)FILE_TYPE_REMOTE;
        if (type == FILE_TYPE_DISK) {
            // A read on a regular file does not block whether or not anything
            // is left, so this is true at EOF as well.  It used to be
            // pos < len, which is exactly the "a byte will come back" answer.
            return true;
        }
        if (type == FILE_TYPE_CHAR) {
            // NUL, and any other character device: a read returns 0 at once.
            return true;
        }
        DWORD available = 0;
        if (PeekNamedPipe(h, NULL, 0, NULL, &available, NULL)) {
            // An open pipe with nothing in it is the one shape where a read
            // really would block, so this stays a data question.
            return available > 0;
        }
        // The writer has gone.  A read returns 0 immediately, which is end of
        // input, so this is readable - the POSIX select() says the same of a
        // pipe with no writer left.
        return GetLastError() == ERROR_BROKEN_PIPE;
    }

    // A waiting record answers for the key, not for what the key means here:
    // an F key, alt+letter and ctrl+Home are all records, and none of them is
    // translated below.  Answering yes to those was a lie the caller could not
    // survive: a guest that polls BIOS CONST and then reads got a promise of a
    // character, and the read blocked until some later keystroke arrived, which
    // looks exactly like the emulator hanging on one F1.  So take the record
    // here and let the queue be the answer.  The ones that mean nothing are
    // dropped, which is where they were headed.
    while (queued_count == 0 && console_input_waiting()) {
        if (consume_one_record() < 0) {
            break;
        }
    }
    return queued_count > 0;
}

int console_getchar() {
    last_synthesized = false;

    // Hand back what an earlier key produced before reading another one
    if (queued_count > 0) {
        return queue_take();
    }

    if (!is_terminal()) {
        // Read the pipe or file directly rather than through getchar().  The
        // status check above asks PeekNamedPipe what the pipe holds, and a
        // stdio buffer between the two would hide bytes 2..N of a burst from
        // it: every status call would report "no character" while input sat
        // in the buffer.  _read is the CRT's unbuffered layer, so the two
        // agree.  init() has already put stdin in _O_BINARY, so this reads the
        // same bytes getchar() did - only without the buffer.  The console
        // path below has no second buffer either: the records are read from
        // the console itself and everything they produce is in the queue.
        unsigned char c;
        int n = _read(_fileno(stdin), &c, 1);
        if (n != 1) return -1;  // 0 is EOF, -1 is an error
        return c;
    }

    // Read records until one produces something.  A key release, a modifier
    // and a focus change are not keystrokes and must not end a blocking read;
    // _getch() filtered those inside the CRT and this loop is what replaces it.
    for (;;) {
        int r = consume_one_record();
        if (r == record_error) return -1;
        if (queued_count > 0) return queue_take();
        if (r == record_nothing) {
            // Untranslated special key (an F key, say): report "no character"
            // rather than reading again, which would block.  A caller that must
            // have a real keystroke skips this and asks again; a polled caller
            // wants exactly this.  Reached only when the caller did not check
            // stdin_has_data() first, since that call drops these keys before
            // they get this far.
            last_synthesized = true;
            return 0;
        }
    }
}

bool console_last_char_synthesized() {
    return last_synthesized;
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

bool delete_file(const char* path) {
    return DeleteFileA(path) != 0;
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
    // FindFirstFile order is the filesystem's - sorted on NTFS, not promised
    // anywhere - so sort here too, so both platforms answer alike: see the
    // contract in os/platform.h.
    std::sort(entries.begin(), entries.end(),
              [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
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
    // Set stdin/stdout to binary mode to prevent CR/LF translation
    // This is critical for proper CP/M console emulation
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

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
