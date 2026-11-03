// Clock and log.
#pragma once

double plat_now_ms(void);   // monotonic time in milliseconds

typedef void (*plat_sink_t)(const char* line);
void plat_set_sink(plat_sink_t sink);       // where to also send the log lines (frontend)
bool plat_log_open(const char* path);       // log file, in addition
void plat_log_close(void);
void plat_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
