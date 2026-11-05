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

// Whether two paths name the same existing file: device and inode on POSIX,
// volume serial number and file index on Windows.  False if either does not
// exist.  Two spellings of one file are ordinary - X.TXT and x.txt on a
// case-insensitive disk, ./x.txt and x.txt anywhere - and the emulator keys
// its open files by path.
bool same_file(const char* a, const char* b);

// Directory entry information
struct DirEntry {
    std::string name;
    bool is_directory;
};

// List files in a directory
// Returns empty vector on error
//
// Sorted by name, byte order, and that is part of the contract rather than an
// implementation detail.  Unsorted, this returns readdir(3) order on POSIX and
// FindFirstFile order on Windows - which is arbitrary on ext4 and sorted on
// NTFS - so a CP/M DIR of the same drive directory listed differently on
// different hosts, and on the same host after the files were rewritten.  Found
// by tests/drv_dir.asm: "drive: search scopes to the drive" asserted the order
// two files come back in, passed on the machine it was written on, and failed
// on a GitHub ubuntu runner the first time CI ran it.
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
// Sockets (the GDB stub)
// ============================================================================

// A TCP socket.  A plain integer on both hosts: a POSIX descriptor, or a
// Winsock SOCKET, which is a handle and is small in practice.
typedef intptr_t socket_t;
const socket_t kNoSocket = -1;

// Listen on the IPv4 address addr, in host byte order: 0x7F000001 is
// 127.0.0.1, 0 is every interface.  Port 0 lets the system choose.
// *bound_port gets the port in use.  kNoSocket on failure, with the reason
// printed to stderr.
socket_t net_listen(uint32_t addr, int port, int* bound_port);

// net_listen on 127.0.0.1.
socket_t net_listen_local(int port, int* bound_port);

// Wait for one connection on a listening socket and return it.  kNoSocket on
// failure, or when a termination signal arrived while waiting (POSIX).
socket_t net_accept(socket_t listener);

// Receive up to len bytes.  timeout_ms < 0 waits without limit, 0 only looks.
// Returns the byte count (> 0), 0 when the peer closed the connection, -1 on
// an error, -2 when the timeout ran out first, and -3 when a signal interrupted
// the wait (POSIX), so the caller can look at termination_requested().
int net_recv(socket_t s, void* buf, int len, int timeout_ms);

// Send all len bytes.  False on failure.
bool net_send_all(socket_t s, const void* buf, int len);

// Close a socket.
void net_close(socket_t s);

// Wait until the console has input, or the socket is readable, or the timeout
// runs out.  Returns a mask: 1 = the console has input (or is at end of input),
// 2 = the socket is readable (or closed), 0 = timeout, -1 = interrupted by a
// signal.  The GDB stub uses this so that ^C from the debugger reaches a guest
// that is waiting for a key.  s may be kNoSocket: then only the console counts.
int wait_console_or_socket(socket_t s, int timeout_ms);

// ============================================================================
// Termination
// ============================================================================

// A text file's change can be held in memory until its FCB is closed (see
// TextImage in cpmemu.cc), so a signal that ends the process where it stands
// loses it.  After catch_termination(), SIGHUP, SIGINT and SIGTERM only note
// that they arrived, and a console read they interrupt returns -1 at once;
// the emulator then closes its files and calls end_by_signal().  A second one
// while the first is being dealt with ends the process at once, the terminal
// put back, as any of them did before.  A signal the process was started
// ignoring - nohup, a background job - stays ignored.  Call it after
// enable_raw_mode(), whose handlers for the same signals it replaces.
//
// POSIX only.  On Windows these do nothing and termination_requested() is
// always 0: the console close and logoff events end the process as before.
void catch_termination();

// The signal that asked the process to end, or 0.
int termination_requested();

// End the process by `sig`, as though it had not been caught: the terminal put
// back, the default disposition restored and the signal raised again, so a
// parent waiting on it sees the signal.  Does not return.
void end_by_signal(int sig);

// ============================================================================
// Initialization
// ============================================================================

// Initialize platform-specific subsystems (call once at startup)
void init();

// Cleanup platform-specific subsystems (called automatically via atexit)
void cleanup();

} // namespace platform

#endif // CPMEMU_PLATFORM_H
