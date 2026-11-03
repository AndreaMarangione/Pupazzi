// Orologio e log.
#pragma once

double plat_now_ms(void);   // tempo monotono in millisecondi

typedef void (*plat_sink_t)(const char* line);
void plat_set_sink(plat_sink_t sink);       // dove mandare anche le righe di log (frontend)
bool plat_log_open(const char* path);       // file di log, in aggiunta
void plat_log_close(void);
void plat_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
