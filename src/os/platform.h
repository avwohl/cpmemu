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

// Check if input is available on stdin without blocking
bool stdin_has_data();

// Read a single character from console (unbuffered)
// Returns the character, or -1 on EOF
int console_getchar();

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
