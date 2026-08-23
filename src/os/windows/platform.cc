/*
 * Windows Platform Implementation
 */

#include "../platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
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
    // Disable quick edit, which is on by default on Windows 10 and later.  With
    // it on, a stray mouse click starts a selection that freezes guest input
    // until the user presses Esc, and while a selection exists Windows Terminal
    // makes ctrl+c copy instead of falling through, so ^C is stolen from the
    // guest in that state.  ENABLE_EXTENDED_FLAGS must be set in the same call
    // for the quick edit bit to be honoured.
    raw_mode |= ENABLE_EXTENDED_FLAGS;
    raw_mode &= ~(ENABLE_QUICK_EDIT_MODE | ENABLE_MOUSE_INPUT);
    // The mode is inherited, so this bit can already be on from whatever ran in
    // this console before us.  With it on the console hands _getch() the VT
    // sequence for a key instead of the 0xE0 prefix pair, so Up arrives as
    // 1B 5B 41 and the extended key table below never fires at all: measured,
    // not guessed.  The whole console path here decodes key codes, so it has to
    // be off.  disable_raw_mode() puts it back with the rest of the saved mode.
    raw_mode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(hStdin, raw_mode);
}

bool is_terminal() {
    // Not _isatty(): the CRT sets that for any character device, NUL included,
    // so `cpmemu prog.com < NUL` was called a console.  The read then went to
    // _getch(), which opens CONIN$ itself and hands the guest whatever is typed
    // at the real keyboard while the redirect is ignored, and a guest waiting
    // for end of input waited for ever.  Asking the console for its mode asks
    // the question that is actually meant: a pipe, a file and NUL all fail it.
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;
}

// ============================================================================
// Extended Key Translation
// ============================================================================

// _getch() reports a special key as 0x00 or 0xE0 followed by a scan code that a
// second call returns.  The scan codes are translated to the WordStar diamond
// so an arrow key arrives as one control character the guest understands.
struct ExtendedKey {
    int scan;    // scan code from the second _getch() call
    int first;   // character handed to the guest
    int second;  // second character of the translation, or -1 if there is none
};

// Insert maps to ^V on purpose: Windows Terminal binds ctrl+v to paste and never
// lets it reach the application (microsoft/terminal#16280), so Insert is the only
// way a WordStar user can reach insert/overtype mode.
// PgDn maps to ^C, which is why console_last_char_synthesized() exists: without
// it, five page downs would trip the five-consecutive-^C emulator exit.
static const ExtendedKey extended_keys[] = {
    { 0x48, 0x05,   -1 },  // Up         -> ^E
    { 0x50, 0x18,   -1 },  // Down       -> ^X
    { 0x4B, 0x13,   -1 },  // Left       -> ^S
    { 0x4D, 0x04,   -1 },  // Right      -> ^D
    { 0x47, 0x11, 0x13 },  // Home       -> ^Q ^S
    { 0x4F, 0x11, 0x04 },  // End        -> ^Q ^D
    { 0x49, 0x12,   -1 },  // PgUp       -> ^R
    { 0x51, 0x03,   -1 },  // PgDn       -> ^C
    { 0x52, 0x16,   -1 },  // Insert     -> ^V
    { 0x53, 0x07,   -1 },  // Delete     -> ^G
    { 0x73, 0x01,   -1 },  // Ctrl+Left  -> ^A  word left
    { 0x74, 0x06,   -1 },  // Ctrl+Right -> ^F  word right
    { 0x8D, 0x17,   -1 },  // Ctrl+Up    -> ^W  scroll up one line
    { 0x91, 0x1A,   -1 }   // Ctrl+Down  -> ^Z  scroll down one line
};

// Characters a key has already produced but the guest has not taken yet.  Two
// slots because one key can be two characters: Home is ^Q ^S.  _kbhit() cannot
// see these, so without the queue the second half of Home and End is invisible
// to BDOS 6 and BIOS CONST.
static int queued_chars[2];
static bool queued_synth[2];
static int queued_count = 0;
static bool last_synthesized = false; // last character came from a special key

static void queue_add(int ch, bool synthesized) {
    if (queued_count < 2) {
        queued_chars[queued_count] = ch;
        queued_synth[queued_count] = synthesized;
        queued_count++;
    }
}

static int queue_take() {
    int ch = queued_chars[0];
    last_synthesized = queued_synth[0];
    queued_chars[0] = queued_chars[1];
    queued_synth[0] = queued_synth[1];
    queued_count--;
    return ch;
}

// Take one key from the console and queue what the guest should see for it.
// Returns -1 at end of input, 0 when the key produced nothing the guest can
// use, 1 when it queued at least one character.
static int read_one_key() {
    int ch = _getch();
    if (ch == EOF) return -1;

    // 0x00 and 0xE0 are the special key prefixes.  The scan code has to be
    // consumed here, otherwise it is read later as an ordinary keystroke.
    if (ch != 0x00 && ch != 0xE0) {
        queue_add(ch, false);
        return 1;
    }

    // 0xE0 is also an ordinary character in every OEM code page, so the byte
    // alone does not prove a special key.  The CRT stashes the scan code where
    // _kbhit() can see it immediately, so a false prefix is one the second read
    // would block on - hand it to the guest instead.
    if (!_kbhit()) {
        queue_add(ch, false);
        return 1;
    }

    int scan = _getch();
    if (scan == EOF) return -1;
    const size_t count = sizeof(extended_keys) / sizeof(extended_keys[0]);
    for (size_t i = 0; i < count; i++) {
        if (extended_keys[i].scan == scan) {
            // Whatever goes in the queue now was made up by us, not typed
            queue_add(extended_keys[i].first, true);
            if (extended_keys[i].second >= 0) {
                queue_add(extended_keys[i].second, true);
            }
            return 1;
        }
    }
    return 0;  // an F key, say: not a character, and not an error either
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
        // handle is first, and answer a file from its own position.
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        DWORD type = GetFileType(h) & ~(DWORD)FILE_TYPE_REMOTE;
        if (type == FILE_TYPE_DISK) {
            // Through the CRT handle, so this agrees with the _read() below
            int fd = _fileno(stdin);
            __int64 pos = _telli64(fd);
            __int64 len = _filelengthi64(fd);
            return pos >= 0 && len >= 0 && pos < len;
        }
        DWORD available = 0;
        if (PeekNamedPipe(h, NULL, 0, NULL, &available, NULL)) {
            return available > 0;
        }
        return false;
    }

    // _kbhit() answers for the key, not for what the key means here, and it
    // says yes to an F key, to alt+letter and to ctrl+Home - none of which the
    // table below translates.  Answering yes to those was a lie the caller
    // could not survive: a guest that polls BIOS CONST and then reads got a
    // promise of a character, and the read blocked until some later keystroke
    // arrived, which looks exactly like the emulator hanging on one F1.  So
    // take the key here and let the queue, not _kbhit(), be the answer.  The
    // ones that mean nothing are dropped, which is where they were headed.
    while (queued_count == 0 && _kbhit()) {
        if (read_one_key() < 0) {
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
        // path below never had the split: _kbhit() and _getch() already share
        // the CRT's own console buffer.
        unsigned char c;
        int n = _read(_fileno(stdin), &c, 1);
        if (n != 1) return -1;  // 0 is EOF, -1 is an error
        return c;
    }

    // For console, read one key with _getch() and hand back what it produced
    int r = read_one_key();
    if (r < 0) return -1;
    if (queued_count > 0) return queue_take();

    // Untranslated special key (an F key, say): report "no character" rather
    // than reading again, which would block.  A caller that must have a real
    // keystroke skips this and asks again; a polled caller wants exactly this.
    // Reached only when the caller did not check stdin_has_data() first, since
    // that call drops these keys before they get this far.
    last_synthesized = true;
    return 0;
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
