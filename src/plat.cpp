#include "plat.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static FILE* g_log;
static plat_sink_t g_sink;

double plat_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

void plat_set_sink(plat_sink_t sink) { g_sink = sink; }

bool plat_log_open(const char* path) {
  plat_log_close();
  g_log = fopen(path, "a");
  return g_log != 0;
}

void plat_log_close(void) {
  if (g_log) fclose(g_log);
  g_log = 0;
}

void plat_log(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (g_log) {
    fputs(buf, g_log);
    fputc('\n', g_log);
    fflush(g_log);
  }
  if (g_sink) g_sink(buf);
}
