#include "qkz80.h"

#include <stdio.h>
#include <stdlib.h>
#include <cstdarg>

void qkz80_global_fatal(const char *fmt,...) {
  va_list ap;
  va_start(ap,fmt);
  (void)vprintf(fmt, ap);
  va_end(ap);
  fprintf(stdout,"\n");
  exit(1);
}

