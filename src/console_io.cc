/*
 * Console I/O Implementation
 */

#include "console_io.h"
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <cerrno>

// Terminal state management
static struct termios original_termios;
static bool termios_saved = false;
static bool raw_mode_enabled = false;

// ^C exit handling
static int consecutive_ctrl_c = 0;
static int ctrl_c_exit_count = 5;

// Auxiliary device files
static FILE* printer_file = nullptr;
static FILE* aux_in_file = nullptr;
static FILE* aux_out_file = nullptr;

void console_cleanup() {
  console_disable_raw_mode();
  console_close_aux_files();
}

void console_init() {
  // Register cleanup handler
  static bool initialized = false;
  if (!initialized) {
    atexit(console_cleanup);
    initialized = true;
  }
}

void console_disable_raw_mode() {
  if (termios_saved && raw_mode_enabled) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
    raw_mode_enabled = false;
  }
}

void console_enable_raw_mode() {
  if (!isatty(STDIN_FILENO)) {
    // Not a terminal, don't try to set raw mode
    return;
  }

  if (!termios_saved) {
    tcgetattr(STDIN_FILENO, &original_termios);
    termios_saved = true;
  }

  struct termios raw = original_termios;
  // Disable canonical mode (line buffering), echo, and signal generation
  // ISIG disabled so ^C passes through to CP/M program instead of killing emulator
  raw.c_lflag &= ~(ICANON | ECHO | ISIG);
  // Set minimum characters to 1 and timeout to 0
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  raw_mode_enabled = true;
}

bool console_has_input() {
  fd_set readfds;
  struct timeval tv;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0;
}

bool console_check_ctrl_c_exit(int ch) {
  if (ch == 0x03) {  // ^C
    consecutive_ctrl_c++;
    if (consecutive_ctrl_c >= ctrl_c_exit_count) {
      fprintf(stderr, "\n[Exiting: %d consecutive ^C received]\n", ctrl_c_exit_count);
      console_disable_raw_mode();
      exit(0);
    }
    return false;  // Pass ^C through to CP/M program
  } else {
    consecutive_ctrl_c = 0;  // Reset counter on any other input
    return false;
  }
}

int console_read_char() {
  int ch = getchar();
  if (ch == EOF) ch = 0x1A;  // EOF becomes ^Z
  console_check_ctrl_c_exit(ch);
  if (ch == '\n') ch = '\r';  // Convert LF to CR for CP/M
  return ch & 0x7F;
}

void console_write_char(uint8_t ch) {
  putchar(ch & 0x7F);
  fflush(stdout);
}

void console_write_string(const char* str) {
  while (*str && *str != '$') {
    putchar(*str & 0x7F);
    str++;
  }
  fflush(stdout);
}

int console_get_ctrl_c_count() {
  return ctrl_c_exit_count;
}

void console_set_ctrl_c_count(int count) {
  if (count > 0) {
    ctrl_c_exit_count = count;
  }
}

//=============================================================================
// Auxiliary Device I/O
//=============================================================================

void console_set_printer_file(const char* path) {
  if (printer_file) {
    fclose(printer_file);
    printer_file = nullptr;
  }
  if (path && *path) {
    printer_file = fopen(path, "w");
    if (!printer_file) {
      fprintf(stderr, "Warning: Cannot open printer file '%s': %s\n",
              path, strerror(errno));
    }
  }
}

void console_set_aux_input_file(const char* path) {
  if (aux_in_file) {
    fclose(aux_in_file);
    aux_in_file = nullptr;
  }
  if (path && *path) {
    aux_in_file = fopen(path, "r");
    if (!aux_in_file) {
      fprintf(stderr, "Warning: Cannot open aux input file '%s': %s\n",
              path, strerror(errno));
    }
  }
}

void console_set_aux_output_file(const char* path) {
  if (aux_out_file) {
    fclose(aux_out_file);
    aux_out_file = nullptr;
  }
  if (path && *path) {
    aux_out_file = fopen(path, "w");
    if (!aux_out_file) {
      fprintf(stderr, "Warning: Cannot open aux output file '%s': %s\n",
              path, strerror(errno));
    }
  }
}

void console_close_aux_files() {
  if (printer_file) {
    fclose(printer_file);
    printer_file = nullptr;
  }
  if (aux_in_file) {
    fclose(aux_in_file);
    aux_in_file = nullptr;
  }
  if (aux_out_file) {
    fclose(aux_out_file);
    aux_out_file = nullptr;
  }
}

void console_printer_out(uint8_t ch) {
  if (printer_file) {
    fputc(ch & 0x7F, printer_file);
    fflush(printer_file);
  } else {
    // No printer file - output to stdout with prefix
    fprintf(stdout, "[PRINTER] %c", ch & 0x7F);
    fflush(stdout);
  }
}

bool console_printer_ready() {
  return true;  // Always ready
}

int console_aux_in() {
  if (aux_in_file) {
    int ch = fgetc(aux_in_file);
    if (ch == EOF) ch = 0x1A;  // ^Z on EOF
    return ch & 0x7F;
  }
  return 0x1A;  // ^Z if no file
}

void console_aux_out(uint8_t ch) {
  if (aux_out_file) {
    fputc(ch & 0x7F, aux_out_file);
    fflush(aux_out_file);
  }
  // Silently ignore if no file
}
