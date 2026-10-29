// Stato della console letto dal sistema (0.5.4): la batteria, in alto a destra in tutte le
// schermate, e il carico della CPU, nel riquadro delle prestazioni (SELECT). Solo per l'interfaccia:
// la partita non ne dipende (stessa partita su ogni macchina).
#pragma once

struct SysBattery {
  bool present;          // c'e' una batteria (sul PC di solito no: allora non si disegna)
  int percent;           // carica, da 0 a 100
  bool charging;         // in carica (il caricatore e' attaccato e la batteria non e' piena)
};

const int SYS_MAX_CORES = 8;
struct SysCpu {
  bool valid;            // false alla prima lettura (servono due letture per le differenze)
  int total;             // carico di tutta la CPU, da 0 a 100
  int ncore;             // core letti (al massimo SYS_MAX_CORES)
  int core[SYS_MAX_CORES];
  int game;              // quanto ne usa il gioco (tutti i suoi thread), in percentuale di tutta la CPU
};

// Batteria da /sys/class/power_supply/*/ (la voce con type "Battery": capacity e status), riletta
// al massimo ogni 2 secondi (now_ms: plat_now_ms). PUPAZZI_BATTERIA=NN (o NNc, in carica) la
// fissa per le prove.
SysBattery sys_battery(double now_ms);
// Carico della CPU da /proc/stat e /proc/self/stat: differenze rispetto alla lettura prima.
// Chiamarla a intervalli regolari (il riquadro: ogni mezzo secondo).
SysCpu sys_cpu_sample(void);
// Prove: cartelle al posto di /sys e /proc (NULL: quelle vere); azzera le letture tenute.
void sys_set_roots(const char* sys_root, const char* proc_root);
