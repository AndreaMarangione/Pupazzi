// Console status read from the system (0.5.4): the battery, top right on every screen, and the
// CPU load, in the performance overlay (SELECT). Only for the interface: the game does not depend
// on it (same game on every machine).
#pragma once

struct SysBattery {
  bool present;          // there is a battery (usually not on a PC: then it is not drawn)
  int percent;           // charge, from 0 to 100
  bool charging;         // charging (the charger is plugged in and the battery is not full)
};

const int SYS_MAX_CORES = 8;
struct SysCpu {
  bool valid;            // false on the first reading (two readings are needed for the differences)
  int total;             // load of the whole CPU, from 0 to 100
  int ncore;             // cores read (at most SYS_MAX_CORES)
  int core[SYS_MAX_CORES];
  int game;              // how much of it the game uses (all its threads), as a percentage of the whole CPU
};

// Battery from /sys/class/power_supply/*/ (the entry with type "Battery": capacity and status),
// read again at most every 2 seconds (now_ms: plat_now_ms). PUPAZZI_BATTERIA=NN (or NNc, charging)
// fixes it for tests.
SysBattery sys_battery(double now_ms);
// CPU load from /proc/stat and /proc/self/stat: differences from the previous reading.
// Call it at regular intervals (the overlay: every half second).
SysCpu sys_cpu_sample(void);
// Tests: directories in place of /sys and /proc (NULL: the real ones); clears the stored readings.
void sys_set_roots(const char* sys_root, const char* proc_root);
