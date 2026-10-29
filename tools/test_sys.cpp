// Prove di sysinfo (0.5.4): batteria da una /sys finta (carica, in carica, piena, nessuna batteria,
// riletta ogni 2 secondi, PUPAZZI_BATTERIA) e carico della CPU da una /proc finta.
//   make build/native/test_sys && ./build/native/test_sys
#include "sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail = 0;
static char g_base[64] = "/tmp/pupazzi_test_sys_XXXXXX";

static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "  ok    " : "  ERRORE", what);
  if (!ok) g_fail++;
}

static void put(const char* rel, const char* text) {
  char path[512], dir[512];
  snprintf(path, sizeof(path), "%s/%s", g_base, rel);
  // crea le cartelle del percorso
  snprintf(dir, sizeof(dir), "%s", path);
  for (char* p = dir + strlen(g_base) + 1; *p; ++p)
    if (*p == '/') {
      *p = 0;
      mkdir(dir, 0755);
      *p = '/';
    }
  FILE* f = fopen(path, "w");
  if (!f) return;
  fputs(text, f);
  fclose(f);
}

int main() {
  if (!mkdtemp(g_base)) return 1;
  unsetenv("PUPAZZI_BATTERIA");
  char sys[128], proc[128];
  snprintf(sys, sizeof(sys), "%s/sys", g_base);
  snprintf(proc, sizeof(proc), "%s/proc", g_base);

  printf("1) batteria\n");
  {
    sys_set_roots(sys, proc);
    SysBattery b = sys_battery(0.0);
    check(!b.present, "nessuna /sys/class/power_supply: niente batteria");
    put("sys/class/power_supply/axp2202-usb/type", "USB\n");
    put("sys/class/power_supply/axp2202-usb/online", "1\n");
    put("sys/class/power_supply/axp2202-battery/type", "Battery\n");
    put("sys/class/power_supply/axp2202-battery/capacity", "87\n");
    put("sys/class/power_supply/axp2202-battery/status", "Discharging\n");
    b = sys_battery(1000.0);
    check(!b.present, "letta da meno di 2 secondi: resta quella di prima");
    b = sys_battery(2500.0);
    check(b.present && b.percent == 87 && !b.charging, "87%, non in carica (e l'alimentatore USB non conta)");
    put("sys/class/power_supply/axp2202-battery/status", "Charging\n");
    put("sys/class/power_supply/axp2202-battery/capacity", "88\n");
    b = sys_battery(3000.0);
    check(b.percent == 87 && !b.charging, "dopo mezzo secondo ancora 87%");
    b = sys_battery(4600.0);
    check(b.present && b.percent == 88 && b.charging, "dopo 2 secondi: 88%, in carica");
    put("sys/class/power_supply/axp2202-battery/status", "Full\n");
    put("sys/class/power_supply/axp2202-battery/capacity", "104\n");
    b = sys_battery(7000.0);
    check(b.percent == 100 && !b.charging, "piena: 100% (104 riportato a 100), non in carica");
    setenv("PUPAZZI_BATTERIA", "12c", 1);
    sys_set_roots(sys, proc);
    b = sys_battery(0.0);
    check(b.present && b.percent == 12 && b.charging, "PUPAZZI_BATTERIA=12c: 12%, in carica");
    unsetenv("PUPAZZI_BATTERIA");
    sys_set_roots(sys, proc);
    put("sys/class/power_supply/axp2202-battery/type", "Mains\n");
    b = sys_battery(0.0);
    check(!b.present, "nessuna voce di tipo Battery: niente batteria");
  }

  printf("2) CPU\n");
  {
    sys_set_roots(sys, proc);
    put("proc/stat",
        "cpu  100 0 100 800 0 0 0 0 0 0\n"
        "cpu0 25 0 25 200 0 0 0 0 0 0\n"
        "cpu1 25 0 25 200 0 0 0 0 0 0\n"
        "cpu2 25 0 25 200 0 0 0 0 0 0\n"
        "cpu3 25 0 25 200 0 0 0 0 0 0\n"
        "intr 1 2 3\n");
    put("proc/self/stat", "1234 (retro arch) S 1 1 1 0 -1 4194560 100 0 0 0 40 10 0 0 20 0 5 0 100 1000 100\n");
    SysCpu c = sys_cpu_sample();
    check(!c.valid, "prima lettura: niente differenze ancora");
    // Mezzo secondo dopo: 400 tick in tutto (100 per core); occupati 200 (core 0 tutto, 1 meta',
    // 2 un quarto, 3 un quarto); il gioco ne ha usati 100.
    put("proc/stat",
        "cpu  250 0 150 1000 0 0 0 0 0 0\n"
        "cpu0 75 0 50 200 0 0 0 0 0 0\n"
        "cpu1 50 0 50 250 0 0 0 0 0 0\n"
        "cpu2 50 0 25 275 0 0 0 0 0 0\n"
        "cpu3 50 0 25 275 0 0 0 0 0 0\n");
    put("proc/self/stat", "1234 (retro arch) S 1 1 1 0 -1 4194560 100 0 0 0 120 30 0 0 20 0 5 0 100 1000 100\n");
    c = sys_cpu_sample();
    char buf[160];
    snprintf(buf, sizeof(buf), "carico %d%% (core %d %d %d %d), gioco %d%%", c.total, c.core[0], c.core[1], c.core[2], c.core[3], c.game);
    check(c.valid && c.ncore == 4 && c.total == 50 && c.core[0] == 100 && c.core[1] == 50 && c.core[2] == 25 && c.core[3] == 25 &&
              c.game == 25,
          buf);
    put("proc/stat", "cpu  250 0 150 1000 0 0 0 0 0 0\n");
    c = sys_cpu_sample();
    check(!c.valid, "cambia il numero dei core: si riparte");
    sys_set_roots(sys, "/nessuna");
    c = sys_cpu_sample();
    check(!c.valid, "senza /proc: niente");
    // Quella vera, se c'e': due letture, numeri nei limiti.
    sys_set_roots(NULL, NULL);
    sys_cpu_sample();
    usleep(100000);
    c = sys_cpu_sample();
    bool ok = !c.valid || (c.total >= 0 && c.total <= 100 && c.game >= 0 && c.game <= 100 && c.ncore >= 1);
    check(ok, "la /proc di questa macchina: valori tra 0 e 100");
  }

  char cmd[160];
  snprintf(cmd, sizeof(cmd), "rm -rf %s", g_base);
  if (system(cmd) != 0) printf("  (non riesco a cancellare %s)\n", g_base);
  printf(g_fail ? "\n%d prove fallite\n" : "\ntutto bene\n", g_fail);
  return g_fail ? 1 : 0;
}
