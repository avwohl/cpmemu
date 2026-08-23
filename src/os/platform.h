/*
 * Platform Abstraction Layer for cpmemu
 *
 * This header defines platform-agnostic interfaces for OS-specific
 * functionality. Implementations are provided in os/linux/ and os/windows/.
 */

#ifndef CPMEMU_PLATFORM_H
#define CPMEMU_PLATFORM_H

#include <string>
#include <vector>
#include <cstdint>

namespace platform {

// ============================================================================
// Terminal Handling
// ============================================================================

// Enable raw terminal mode (disable line buffering, echo, signal handling)
// This allows character-by-character input for CP/M console emulation
void enable_raw_mode();

// Restore terminal to original mode
void disable_raw_mode();

// Check if stdin is connected to a terminal/console
bool is_terminal();

// Check if input is available on stdin without blocking.
// True means the next console_getchar() returns a character without waiting.
// On Windows this may take a key off the console to find out: _kbhit() reports
// keys that translate to nothing here, and a status call that counted those
// would promise a character the following read could not produce.  Keys it
// discards are ones console_getchar() would have discarded anyway.
bool stdin_has_data();

// Read a single character from console (unbuffered)
// Returns the character, or -1 on EOF.  On Windows a special key (arrow,
// Home, ...) is translated to a WordStar diamond control code; a special
// key with no translation returns 0.
int console_getchar();

// True when the character last returned by console_getchar() was
// synthesized from a special key rather than typed by the user.  The ^C
// exit escape hatch uses this so a translated ^C (WordStar page-down on
// Windows) does not count toward the exit.
bool console_last_char_synthesized();

// ============================================================================
// File System
// ============================================================================

// File type returned by get_file_type()
enum class FileType {
    Regular,
    Directory,
    Other,
    NotFound
};

// Get the type of a file at the given path
FileType get_file_type(const char* path);

// Get the size of a file in bytes, returns -1 on error
int64_t get_file_size(const char* path);

// Delete a file, returns true on success
bool delete_file(const char* path);

// Directory entry information
struct DirEntry {
    std::string name;
    bool is_directory;
};

// List files in a directory
// Returns empty vector on error
std::vector<DirEntry> list_directory(const char* path);

// ============================================================================
// Path Handling
// ============================================================================

// Get the path separator for the current platform ('/' or '\\')
char path_separator();

// Extract the base name (filename) from a path
std::string basename(const std::string& path);

// Change working directory (returns 0 on success, -1 on error)
int change_directory(const char* path);

// ============================================================================
// Initialization
// ============================================================================

// Initialize platform-specific subsystems (call once at startup)
void init();

// Cleanup platform-specific subsystems (called automatically via atexit)
void cleanup();

} // namespace platform

#endif // CPMEMU_PLATFORM_H
