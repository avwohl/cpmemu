/*
 * Console I/O for CP/M Emulators
 *
 * Shared terminal handling for cpmemu and bios-level emulators.
 */

#ifndef CONSOLE_IO_H
#define CONSOLE_IO_H

#include <cstdint>
#include <cstdio>

// Initialize console I/O (call once at startup)
void console_init();

// Cleanup console I/O (called automatically via atexit, but can call manually)
void console_cleanup();

// Enable raw mode (no echo, no line buffering, pass through ^C)
void console_enable_raw_mode();

// Disable raw mode (restore original terminal settings)
void console_disable_raw_mode();

// Check if input is available (non-blocking)
// Returns true if a character is waiting to be read
bool console_has_input();

// Read a character (blocking)
// Returns the character, or 0x1A (^Z) on EOF
// Converts \n to \r for CP/M compatibility
// Tracks ^C for exit (5 consecutive ^C exits the emulator)
int console_read_char();

// Write a character (strips high bit)
void console_write_char(uint8_t ch);

// Write a string (terminates at '$' like CP/M BDOS function 9)
void console_write_string(const char* str);

// Check for ^C exit condition
// Call this after reading each character
// Returns true if we should exit (5 consecutive ^C received)
bool console_check_ctrl_c_exit(int ch);

// Get/set the consecutive ^C count required for exit (default: 5)
int console_get_ctrl_c_count();
void console_set_ctrl_c_count(int count);

//=============================================================================
// Auxiliary Device I/O
//=============================================================================

// Set printer (LST:) output file
void console_set_printer_file(const char* path);

// Set auxiliary input (RDR:) file
void console_set_aux_input_file(const char* path);

// Set auxiliary output (PUN:) file
void console_set_aux_output_file(const char* path);

// Close all auxiliary device files
void console_close_aux_files();

// Printer output
// If printer file is set, writes to it; otherwise outputs to stdout with [PRINTER] prefix
void console_printer_out(uint8_t ch);

// Printer status
// Returns true if printer is ready (always true in this implementation)
bool console_printer_ready();

// Auxiliary input
// Returns character from aux input file, or 0x1A (^Z) if no file or EOF
int console_aux_in();

// Auxiliary output
// If aux output file is set, writes to it; otherwise silently ignored
void console_aux_out(uint8_t ch);

#endif // CONSOLE_IO_H
