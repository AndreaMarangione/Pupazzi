// Batteria e carico della CPU letti dal sistema (vedi sysinfo.h). Niente allocazioni; file piccoli
// letti tutti in una volta.
#include "sysinfo.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

char g_sys[200] = "/sys", g_proc[200] = "/proc";
SysBattery g_bat;
double g_bat_ms = -1e30;
bool g_bat_env_read;
int g_bat_env = -1;                 // PUPAZZI_BATTERIA: -1 non c'e'
bool g_bat_env_charging;

// Un file piccolo in buf (al massimo n-1 byte, chiuso da 0). false se non si legge.
bool read_small(const char* path, char* buf, int n) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  const size_t k = fread(buf, 1, (size_t)n - 1, f);
  const bool bad = ferror(f) != 0;
  fclose(f);
  buf[k] = 0;
  return !bad && k > 0;
}

void read_battery(SysBattery* b) {
  memset(b, 0, sizeof(*b));
  char dir[260];
  snprintf(dir, sizeof(dir), "%s/class/power_supply", g_sys);
  DIR* d = opendir(dir);
  if (!d) return;
  while (struct dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    char path[600], buf[64];
    snprintf(path, sizeof(path), "%s/%s/type", dir, e->d_name);
    if (!read_small(path, buf, sizeof(buf)) || strncmp(buf, "Battery", 7)) continue;
    snprintf(path, sizeof(path), "%s/%s/capacity", dir, e->d_name);
    if (!read_small(path, buf, sizeof(buf))) continue;
    const int pc = atoi(buf);
    b->present = true;
    b->percent = pc < 0 ? 0 : (pc > 100 ? 100 : pc);
    snprintf(path, sizeof(path), "%s/%s/status", dir, e->d_name);
    b->charging = read_small(path, buf, sizeof(buf)) && !strncmp(buf, "Charging", 8);
    break;
  }
  closedir(d);
}

// /proc/stat: per la riga "cpu" (tutta la CPU) e "cpuN" i tempi occupati e totali.
struct Ticks { unsigned long long busy, total; };
Ticks g_prev_all, g_prev_core[SYS_MAX_CORES];
unsigned long long g_prev_game;
int g_prev_ncore = -1;

bool parse_cpu_line(const char* s, Ticks* t) {
  unsigned long long v[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  const int n = sscanf(s, "%llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
  if (n < 4) return false;
  unsigned long long total = 0;
  for (int i = 0; i < 8; ++i) total += v[i];
  t->total = total;
  t->busy = total - v[3] - v[4];      // meno idle e iowait
  return true;
}

// a - b, ma 0 se b e' piu' grande (iowait sui kernel SMP a volte torna indietro).
unsigned long long sub0(unsigned long long a, unsigned long long b) { return a > b ? a - b : 0ull; }

int pct(unsigned long long part, unsigned long long whole) {
  if (whole == 0) return 0;
  const unsigned long long p = (part * 100ull + whole / 2ull) / whole;
  return p > 100ull ? 100 : (int)p;
}

}  // namespace

void sys_set_roots(const char* sys_root, const char* proc_root) {
  snprintf(g_sys, sizeof(g_sys), "%s", sys_root ? sys_root : "/sys");
  snprintf(g_proc, sizeof(g_proc), "%s", proc_root ? proc_root : "/proc");
  g_bat_ms = -1e30;
  g_bat_env_read = false;
  g_prev_ncore = -1;
}

SysBattery sys_battery(double now_ms) {
  if (!g_bat_env_read) {
    g_bat_env_read = true;
    g_bat_env = -1;
    const char* s = getenv("PUPAZZI_BATTERIA");
    if (s && *s >= '0' && *s <= '9') {
      g_bat_env = atoi(s);
      if (g_bat_env > 100) g_bat_env = 100;
      g_bat_env_charging = strchr(s, 'c') != NULL;
    }
  }
  if (g_bat_env >= 0) {
    SysBattery b = { true, g_bat_env, g_bat_env_charging };
    return b;
  }
  if (now_ms - g_bat_ms >= 2000.0) {
    g_bat_ms = now_ms;
    read_battery(&g_bat);
  }
  return g_bat;
}

SysCpu sys_cpu_sample(void) {
  SysCpu out;
  memset(&out, 0, sizeof(out));
  static char buf[8192];
  char path[260];
  snprintf(path, sizeof(path), "%s/stat", g_proc);
  if (!read_small(path, buf, sizeof(buf))) return out;
  Ticks all = { 0, 0 }, core[SYS_MAX_CORES];
  int ncore = 0;
  bool have_all = false;
  for (char* line = buf; line && *line;) {
    char* next = strchr(line, '\n');
    if (next) *next++ = 0;
    if (!strncmp(line, "cpu ", 4)) {
      have_all = parse_cpu_line(line + 4, &all);
    } else if (!strncmp(line, "cpu", 3) && line[3] >= '0' && line[3] <= '9') {
      const char* p = line + 3;
      while (*p >= '0' && *p <= '9') ++p;
      if (ncore < SYS_MAX_CORES && parse_cpu_line(p, &core[ncore])) ncore++;
    }
    line = next;
  }
  if (!have_all) return out;
  // /proc/self/stat: utime e stime sono il 14esimo e il 15esimo campo; il nome (2) e' tra parentesi
  // e puo' avere spazi: si conta dopo l'ultima ')'.
  unsigned long long game = 0;
  snprintf(path, sizeof(path), "%s/self/stat", g_proc);
  char sb[1024];
  if (read_small(path, sb, sizeof(sb))) {
    const char* p = strrchr(sb, ')');
    unsigned long long ut = 0, st = 0;
    if (p && sscanf(p + 1, " %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &ut, &st) == 2) game = ut + st;
  }
  if (g_prev_ncore == ncore && all.total > g_prev_all.total) {
    out.valid = true;
    const unsigned long long dt = all.total - g_prev_all.total;
    out.total = pct(sub0(all.busy, g_prev_all.busy), dt);
    out.ncore = ncore;
    for (int i = 0; i < ncore; ++i) out.core[i] = pct(sub0(core[i].busy, g_prev_core[i].busy), sub0(core[i].total, g_prev_core[i].total));
    out.game = pct(sub0(game, g_prev_game), dt);
  }
  g_prev_all = all;
  for (int i = 0; i < ncore; ++i) g_prev_core[i] = core[i];
  g_prev_ncore = ncore;
  g_prev_game = game;
  return out;
}
