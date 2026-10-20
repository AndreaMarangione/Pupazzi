// Pupazzi - core libretro: interfaccia con il frontend (RetroArch), comandi generali (pausa,
// uscita, riquadro delle prestazioni), comandi dalle altre console (Remote RetroPad), registro e
// stato di uscita per lo script di avvio.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef __GLIBC__
#include <gnu/libc-version.h>
#endif
#include "libretro.h"
#include "game.h"
#include "swr.h"
#include "glr.h"
#include "plat.h"
#include "audio.h"
#include "save.h"

#ifndef PUPAZZI_BUILD
#define PUPAZZI_BUILD "locale"
#endif
#if defined(__aarch64__)
#define PUPAZZI_ARCH "arm64"
#elif defined(__arm__)
#define PUPAZZI_ARCH "arm32"
#elif defined(__x86_64__)
#define PUPAZZI_ARCH "x86_64"
#else
#define PUPAZZI_ARCH "altro"
#endif

#define MAX_W 960
#define MAX_H 720
#define AUDIO_RATE 48000
#define AUDIO_FRAMES (AUDIO_RATE / 60)
#define STAT_PERIOD 600     // ogni quanti fotogrammi scrivere una riga di stato nel log
#define TAP_FRAMES 40       // START e SELECT valgono solo se premuti per meno di cosi' (vedi handle_controls)
#define QUIT_FRAMES 45      // SELECT+START tenuti cosi' a lungo: si esce

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;
static struct retro_hw_render_callback hw;
static Hud g_hud;
static int16_t g_audio[AUDIO_FRAMES * 2];

static struct {
  int w, h, ncpu;
  bool hud_off, hud_fixed;    // PUPAZZI_HUD=0: niente HUD; =fisso: riquadro delle prestazioni senza tempi (immagini confrontabili)
  bool want_gl, use_gl, gl_ready;
  int gl_wait;
  bool bitmasks;
  int bots;                   // PUPAZZI_BOT=n: i primi n giocatori li guida il computer (prove)
  unsigned max_frames;        // PUPAZZI_FRAMES=n: chiusura automatica dopo n fotogrammi (prove)
  bool perf, trace, trace_m;
  bool stick_seen[MAX_PLAYERS][2];
  // Levetta destra, per il registro: escursione, rumore a riposo, tremolio a levetta inclinata.
  struct StickStats {
    float x0, x1, y0, y1;     // valori minimi e massimi letti
    float rest;               // inclinazione massima a levetta ferma al centro
    int low_n, full_n;        // letture di fila vicino al centro / quasi del tutto inclinata
    float last_a;             // direzione alla lettura prima (gradi)
    int jit[41], jit_n;       // variazioni di direzione tra due letture, a passi di 0,25 gradi
  } ss[MAX_PLAYERS];
  Pad pads[MAX_PLAYERS];
  int start_hold[MAX_PLAYERS];
  int sel_hold, quit_hold;
  bool combo;
  bool shutdown_sent;
  const char* exit_reason;
  double last_start;
  int win_n;
  double win_sum, win_max;
  float fps, frame_avg, frame_max;
  float e_sim, e_geo, e_ras, e_core, e_video, e_audio;
  RenderStats st;
  unsigned frame;
  char out_dir[256];
  int hdmi_force;             // PUPAZZI_HDMI=0/1 (prove): schermo fisso; -1 si legge dal sistema
  int hdmi_src;               // da dove: 0 niente (si resta come con l'HDMI), 1 extcon, 2 switch
} A;

// HDMI collegato? Lo stesso file che legge il RetroArch della console ("HDMI=1"); su altri kernel
// /sys/class/switch/hdmi/state ("1"). Se non c'e' nessuno dei due (PC) si resta come con l'HDMI.
static int hdmi_read(int* src) {
  char buf[64];
  FILE* f = fopen("/sys/class/extcon/hdmi/state", "r");
  if (f) {
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    const char* p = strstr(buf, "HDMI=");
    if (p) {
      *src = 1;
      return p[5] == '1' ? 1 : 0;
    }
  }
  f = fopen("/sys/class/switch/hdmi/state", "r");
  if (f) {
    const int c = fgetc(f);
    fclose(f);
    if (c == '0' || c == '1') {
      *src = 2;
      return c == '1' ? 1 : 0;
    }
  }
  *src = 0;
  return -1;
}

static void hdmi_poll(bool first) {
  int on = A.hdmi_force;
  int src = 3;
  if (on < 0) on = hdmi_read(&src);
  if (first) {
    static const char* const SRC[4] = { "nessun file dell'HDMI (si gioca come con la TV)", "/sys/class/extcon/hdmi/state",
                                        "/sys/class/switch/hdmi/state", "PUPAZZI_HDMI" };
    plat_log("HDMI: %s, da %s", on < 0 ? "non si sa" : (on ? "collegato" : "staccato"), SRC[src]);
    A.hdmi_src = src;
  }
  if (on >= 0) game_set_hdmi(on != 0);
}

static void log_sink(const char* line) {
  if (log_cb) log_cb(RETRO_LOG_INFO, "[pupazzi] %s\n", line);
}

// Antialias alla prova: nei primi 3 secondi in cui e' acceso c'e' il file aa_in_prova.txt nella
// cartella dei risultati. Se all'avvio c'e' ancora, la volta prima il gioco si e' chiuso proprio
// li' (per esempio nel driver della GPU): si parte senza antialias, che si riaccende dalla pausa.
static const int AA_TRY_FRAMES = 180;
static struct {
  char path[320];
  int frames;    // fotogrammi con l'antialias da quando c'e' il file; -1 = nessun file
  bool passed;   // provato per 3 secondi in questa sessione: non serve piu' il file
  bool crashed;  // all'avvio il file c'era ancora: spento anche nel salvataggio (vedi open_save)
} g_aa_try;

static void aa_try_mark(bool on) {
  if (on) {
    FILE* f = fopen(g_aa_try.path, "w");
    if (f) {
      fputs("antialias in prova: se questo file resta, il gioco si e' chiuso mentre lo provava\n", f);
      fclose(f);
    }
    g_aa_try.frames = 0;
  } else {
    remove(g_aa_try.path);
    g_aa_try.frames = -1;
  }
}

// Come e' finita la sessione: lo script di avvio sulla console lo riporta nel suo registro.
static void write_status(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/pupazzi_stato.txt", A.out_dir);
  FILE* f = fopen(path, "w");
  if (!f) return;
  GameInfo gi;
  game_info(&gi);
  fprintf(f, "uscita=%s\nfotogrammi=%u\nrenderer=%s\narch=%s\nmappa=%d\ngiro=%d\nzona=%d\ngiocatori=%d\nmostri_sconfitti=%d\ncadute=%d\nmappe_completate=%d\n"
             "zone_rifatte=%d\nmappe_da_capo=%d\n",
          A.exit_reason ? A.exit_reason : "frontend", A.frame, A.use_gl ? "gpu" : "software", PUPAZZI_ARCH, gi.map, gi.loop, gi.zone,
          gi.players, gi.kills, gi.deaths, gi.maps_done, gi.wipes, gi.restarts);
  fclose(f);
}

// reason: giocatore, limite, gl_assente (senza richiesta resta "frontend").
static void request_shutdown(const char* why, const char* reason) {
  if (A.shutdown_sent) return;
  A.shutdown_sent = true;
  A.exit_reason = reason;
  plat_log("chiusura richiesta: %s", why);
  write_status();
  if (env_cb) env_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

// --- comandi dalle altre console (Remote RetroPad) ----------------------------
// Sulle console-controller gira il core Remote RetroPad di RetroArch: per ogni tasto o asse che
// cambia manda un messaggio UDP di 20 byte (port, device, index, id: interi a 32 bit; state: 16
// bit; little-endian), il giocatore N alla porta 55400+N-1 (il campo port non conta). RetroArch
// ne leggeva uno solo per giocatore a fotogramma: con le levette in movimento ne arrivano anche 4
// o piu' a fotogramma, la coda cresceva e i comandi arrivavano sempre piu' in ritardo (il
// personaggio continuava a muoversi da solo). Ora le porte le apre il gioco (a RetroArch run.sh
// le fa tenere chiuse) e a ogni fotogramma legge tutto quello che e' arrivato: vale lo stato piu'
// recente. Un tasto tenuto non manda nulla: lo stato resta finche' non arriva un cambio (se resta
// fermo 30 secondi ci pensa il gioco, vedi FROZEN_FRAMES).
#define REMOTE_PORT 55400        // giocatore 1; PUPAZZI_REMOTO_PORTA=n per le prove
#define REMOTE_MSG_SIZE 20       // byte di un messaggio: gli altri si ignorano
#define REMOTE_READ_MAX 1024     // messaggi letti al massimo per giocatore in un fotogramma
#define REMOTE_STALL_MS 500.0    // gioco fermo cosi' a lungo (menu di RetroArch): i comandi in coda sono vecchi
#define REMOTE_BURST 8           // tanti messaggi in un fotogramma: arrivati in ritardo tutti insieme

struct RemotePad {
  int fd;                        // -1: porta chiusa
  uint16_t btn;                  // tasti premuti (bit = id RetroPad)
  int16_t ax[4];                 // levetta sinistra x y, destra x y: valori grezzi come arrivano
  uint32_t ip;                   // chi li manda (indirizzo e porta, come arrivano dalla rete)
  uint16_t sport;
  bool seen, err_logged;
  unsigned msgs, frames, max_frame, bursts;   // per il registro di fine sessione
  unsigned bad, stale, senders;
};

static struct {
  bool ready;                    // remote_open fatta (anche se nessuna porta si e' aperta)
  int nopen;                     // porte aperte
  double last_ms;                // ultima lettura (per accorgersi delle pause del gioco)
  RemotePad pl[MAX_PLAYERS];
} R;

static void remote_neutral(RemotePad& r) {
  r.btn = 0;
  for (int a = 0; a < 4; ++a) r.ax[a] = 0;
}

static bool remote_idle(const RemotePad& r) {
  return r.btn == 0 && r.ax[0] == 0 && r.ax[1] == 0 && r.ax[2] == 0 && r.ax[3] == 0;
}

static bool remote_same(const RemotePad& a, const RemotePad& b) {
  return a.btn == b.btn && a.ax[0] == b.ax[0] && a.ax[1] == b.ax[1] && a.ax[2] == b.ax[2] && a.ax[3] == b.ax[3];
}

// Porta UDP non bloccante; se non si apre restituisce -errno. Niente SO_REUSEADDR: se lo chiedono
// due programmi (RetroArch lo chiede sempre) il sistema lascia aprire la stessa porta a entrambi e
// i messaggi vanno solo all'ultimo arrivato, senza errori. Senza, chi arriva secondo non la apre e
// il registro lo dice (RetroArch apre le sue dopo il caricamento del gioco: con il Remote RetroPad
// rimasto acceso sarebbe lui a restare senza porte).
static int remote_socket(int port) {
  // O_NONBLOCK gia' alla creazione (con fcntl il core a 32 bit vorrebbe una glibc 2.28).
  const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) return -errno;
  // Coda piu' lunga (il sistema la limita e poi la raddoppia): regge meglio un fotogramma lento.
  int sz = 0;
  socklen_t sl = sizeof(sz);
  if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, &sl) == 0 && sz < 512 * 1024) {
    sz = 256 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
  }
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((uint16_t)port);
  if (bind(fd, (const struct sockaddr*)&a, sizeof(a)) < 0) {
    const int e = errno;
    close(fd);
    return -e;
  }
  return fd;
}

// Da retro_load_game: una porta per giocatore. PUPAZZI_REMOTO=0 non le apre.
static void remote_open(void) {
  memset(&R, 0, sizeof(R));
  for (int p = 0; p < MAX_PLAYERS; ++p) R.pl[p].fd = -1;
  R.ready = true;
  const char* off = getenv("PUPAZZI_REMOTO");
  if (off && !strcmp(off, "0")) {
    plat_log("comandi dalle altre console: spenti (PUPAZZI_REMOTO=0)");
    return;
  }
  const char* pv = getenv("PUPAZZI_REMOTO_PORTA");
  int base = (pv && *pv) ? atoi(pv) : REMOTE_PORT;
  if (base < 1 || base > 65535 - (MAX_PLAYERS - 1)) base = REMOTE_PORT;
  char ok[64] = "", bad[400] = "";
  bool in_use = false;
  for (int p = 0; p < MAX_PLAYERS; ++p) {
    const int fd = remote_socket(base + p);
    size_t n;
    if (fd >= 0) {
      R.pl[p].fd = fd;
      R.nopen++;
      n = strlen(ok);
      snprintf(ok + n, sizeof(ok) - n, " %d", base + p);
    } else {
      in_use |= -fd == EADDRINUSE;
      n = strlen(bad);
      snprintf(bad + n, sizeof(bad) - n, "; porta %d (giocatore %d) non disponibile: %s", base + p, p + 1, strerror(-fd));
    }
  }
  const char* hint = in_use ? " (gia' in uso da un altro programma: RetroArch con il Remote RetroPad acceso?)" : "";
  if (R.nopen == MAX_PLAYERS)
    plat_log("comandi dalle altre console: porte %d-%d aperte (giocatori 1-%d)", base, base + MAX_PLAYERS - 1, MAX_PLAYERS);
  else if (R.nopen > 0)
    plat_log("comandi dalle altre console: porte aperte%s%s%s", ok, bad, hint);
  else
    plat_log("comandi dalle altre console: nessuna porta aperta%s%s", bad, hint);
}

// Da retro_unload_game e retro_deinit: chiude le porte e scrive nel registro cosa e' arrivato.
static void remote_close(void) {
  if (!R.ready) return;
  R.ready = false;
  bool any = false;
  for (int p = 0; p < MAX_PLAYERS; ++p) {
    RemotePad& r = R.pl[p];
    if (r.fd >= 0) close(r.fd);
    r.fd = -1;
    if (!r.seen && !r.bad && !r.stale) continue;
    any = true;
    char extra[160] = "";
    size_t n = 0;
    if (r.bad) n += (size_t)snprintf(extra + n, sizeof(extra) - n, "; %u di misura sbagliata ignorati", r.bad);
    if (r.stale && n < sizeof(extra)) n += (size_t)snprintf(extra + n, sizeof(extra) - n, "; %u scartati dopo una pausa del gioco", r.stale);
    if (r.senders && n < sizeof(extra)) snprintf(extra + n, sizeof(extra) - n, "; %u cambi di console o di Remote RetroPad", r.senders);
    plat_log("comandi dalla rete giocatore %d: %u messaggi in %u fotogrammi, al massimo %u in un fotogramma (fotogrammi con %d o piu': %u)%s",
             p + 1, r.msgs, r.frames, r.max_frame, REMOTE_BURST, r.bursts, extra);
  }
  if (R.nopen > 0 && !any) plat_log("comandi dalla rete: nessun messaggio arrivato");
  for (int p = 0; p < MAX_PLAYERS; ++p) remote_neutral(R.pl[p]);
}

static int32_t le32(const uint8_t* b) {
  return (int32_t)((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24);
}

// Un messaggio, come lo legge RetroArch (input_remote_parse_packet), ma senza indici negativi.
static void remote_apply(RemotePad& r, const uint8_t* m) {
  const int32_t device = le32(m + 4), index = le32(m + 8), id = le32(m + 12);
  const uint16_t state = (uint16_t)(m[16] | m[17] << 8);
  if (device == RETRO_DEVICE_JOYPAD && id >= 0 && id < 16) {
    const uint16_t bit = (uint16_t)(1u << id);
    r.btn = (uint16_t)(state ? (r.btn | bit) : (r.btn & ~bit));
  } else if (device == RETRO_DEVICE_ANALOG && index >= 0 && index < 2 && id >= 0 && id < 2) {
    r.ax[index * 2 + id] = (int16_t)state;
  }
}

static void ip_text(uint32_t ip, char* out, size_t n) {
  struct in_addr a;
  a.s_addr = ip;
  if (!inet_ntop(AF_INET, &a, out, (socklen_t)n)) snprintf(out, n, "?");
}

// A ogni fotogramma, prima di leggere i pad: tutti i messaggi arrivati su ogni porta, in ordine.
static void remote_poll(void) {
  if (!R.ready || R.nopen == 0) return;
  const double now = plat_now_ms();
  const double gap = R.last_ms > 0.0 ? now - R.last_ms : 0.0;
  R.last_ms = now;
  // Gioco rimasto fermo (menu di RetroArch): la coda puo' essersi riempita e allora il sistema
  // ha buttato i messaggi piu' recenti. Quelli rimasti sono vecchi (un tasto lasciato nel
  // frattempo resterebbe premuto): si buttano tutti e si riparte da fermi.
  const bool stalled = gap > REMOTE_STALL_MS;
  unsigned dropped = 0;
  char who[16] = "";
  for (int p = 0; p < MAX_PLAYERS; ++p) {
    RemotePad& r = R.pl[p];
    if (r.fd < 0) continue;
    const RemotePad before = r;
    unsigned n = 0, nd = 0;
    for (int k = 0; k < REMOTE_READ_MAX; ++k) {
      uint8_t m[64];
      struct sockaddr_in from;
      socklen_t fl = sizeof(from);
      memset(&from, 0, sizeof(from));
      const ssize_t got = recvfrom(r.fd, m, sizeof(m), MSG_DONTWAIT, (struct sockaddr*)&from, &fl);
      if (got < 0) {
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK && !r.err_logged) {
          r.err_logged = true;
          plat_log("giocatore %d: errore leggendo i comandi dalla rete: %s", p + 1, strerror(errno));
        }
        break;
      }
      if (got != REMOTE_MSG_SIZE) {
        r.bad++;
        continue;
      }
      if (stalled) {
        nd++;
        continue;
      }
      const uint32_t ip = from.sin_addr.s_addr;
      const uint16_t sport = from.sin_port;
      if (!r.seen) {
        char t[INET_ADDRSTRLEN];
        ip_text(ip, t, sizeof(t));
        plat_log("giocatore %d: comandi dalla rete ricevuti (da %s)", p + 1, t);
        r.seen = true;
      } else if (ip != r.ip || sport != r.sport) {
        // Un'altra console sulla stessa porta, o il Remote RetroPad ripartito (nuova porta di
        // partenza): chi riparte manda solo cio' che non e' a riposo, quindi si riparte da fermi.
        if (++r.senders <= 3) {
          char t0[INET_ADDRSTRLEN], t1[INET_ADDRSTRLEN];
          ip_text(r.ip, t0, sizeof(t0));
          ip_text(ip, t1, sizeof(t1));
          plat_log("giocatore %d: comandi dalla rete ora da %s:%u (prima da %s:%u): Remote RetroPad ripartito o un'altra console sulla stessa porta, si riparte da fermi",
                   p + 1, t1, (unsigned)ntohs(sport), t0, (unsigned)ntohs(r.sport));
        }
        remote_neutral(r);
      }
      r.ip = ip;
      r.sport = sport;
      remote_apply(r, m);
      n++;
    }
    if (stalled && (nd > 0 || !remote_idle(r))) {
      remote_neutral(r);
      r.stale += nd;
      dropped += nd;
      const size_t l = strlen(who);
      snprintf(who + l, sizeof(who) - l, " %d", p + 1);
    }
    if (n > 0) {
      r.msgs += n;
      r.frames++;
      if (n > r.max_frame) r.max_frame = n;
      if (n >= REMOTE_BURST) r.bursts++;
    }
    // Prove (PUPAZZI_TRACE): stato dalla rete dopo questo fotogramma, ogni volta che arriva
    // qualcosa o che cambia (anche quando si riparte da fermi). Senza messaggi non scrive nulla.
    if (A.trace && (n > 0 || !remote_same(before, r)))
      plat_log("rete %u giocatore %d: %u messaggi, tasti %04x, levette %d %d %d %d", A.frame, p + 1, n, (unsigned)r.btn,
               (int)r.ax[0], (int)r.ax[1], (int)r.ax[2], (int)r.ax[3]);
  }
  if (who[0])
    plat_log("comandi dalla rete: gioco fermo per %.1f s (menu di RetroArch?), %u comandi arrivati nel frattempo scartati: giocatori%s ripartono da fermi",
             gap / 1000.0, dropped, who);
}

// --- input ----------------------------------------------------------------

// Levetta: valore grezzo (pad della console o rete) in [-1, 1].
static float axis_raw(int raw) {
  const float v = (float)raw / 32767.0f;
  return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

// Levetta sinistra: zona morta per asse (muoversi lungo gli assi resta facile).
static float axis(int raw) {
  float v = axis_raw(raw);
  const float dead = 0.20f;
  if (v > -dead && v < dead) return 0.0f;
  v = (v > 0.0f ? v - dead : v + dead) / (1.0f - dead);
  return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

static void stick_stats(int p, float x, float y) {
  auto& s = A.ss[p];
  if (x < s.x0) s.x0 = x;
  if (x > s.x1) s.x1 = x;
  if (y < s.y0) s.y0 = y;
  if (y > s.y1) s.y1 = y;
  const float m = sqrtf(x * x + y * y);
  s.low_n = m < 0.3f ? s.low_n + 1 : 0;
  if (s.low_n > 30 && m > s.rest) s.rest = m;     // ferma al centro da mezzo secondo
  if (m < 0.85f) {
    s.full_n = 0;
    return;
  }
  const float a = atan2f(y, x) * (180.0f / 3.14159265f);
  if (s.full_n++ > 0) {
    float d = fabsf(a - s.last_a);
    if (d > 180.0f) d = 360.0f - d;
    const int bin = d >= 10.0f ? 40 : (int)(d * 4.0f);
    s.jit[bin]++;
    s.jit_n++;
  }
  s.last_a = a;
}

// Variazione di direzione sotto cui sta la frazione q delle letture (gradi, a passi di 0,25).
static float jit_quantile(const int* jit, int n, float q) {
  int acc = 0;
  for (int b = 0; b < 41; ++b) {
    acc += jit[b];
    if ((float)acc >= q * (float)n) return b >= 40 ? 10.0f : (float)(b + 1) * 0.25f;
  }
  return 10.0f;
}

static void log_stick_stats(void) {
  for (int p = 0; p < MAX_PLAYERS; ++p) {
    if (!A.stick_seen[p][1]) continue;
    const auto& s = A.ss[p];
    char jit[96] = "mai inclinata del tutto";
    if (s.jit_n > 0)
      snprintf(jit, sizeof(jit), "tremolio a levetta inclinata: meta' sotto %.2f gradi, 9 su 10 sotto %.2f (%d letture)",
               (double)jit_quantile(s.jit, s.jit_n, 0.5f), (double)jit_quantile(s.jit, s.jit_n, 0.9f), s.jit_n);
    plat_log("levetta destra giocatore %d: x da %.2f a %.2f, y da %.2f a %.2f, a riposo fino a %.2f; %s; mira %s",
             p + 1, (double)s.x0, (double)s.x1, (double)s.y0, (double)s.y1, (double)s.rest, jit, game_aim_name(p));
  }
}

// Pad della console (frontend) e comandi dalla rete insieme: tasti premuti su uno o sull'altro;
// per ogni asse il valore piu' lontano dal centro (un pad sulla console e una console-controller
// possono fare da stesso giocatore). Zone morte e levette poi come per un pad solo.
static void read_input(void) {
  input_poll_cb();
  remote_poll();
  static const unsigned AX_INDEX[4] = { RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_INDEX_ANALOG_LEFT,
                                        RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_INDEX_ANALOG_RIGHT };
  static const unsigned AX_ID[4] = { RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_ID_ANALOG_Y, RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_ID_ANALOG_Y };
  for (unsigned p = 0; p < MAX_PLAYERS; ++p) {
    const RemotePad& rp = R.pl[p];
    uint16_t b = rp.btn;
    if (A.bitmasks) {
      b |= (uint16_t)input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    } else {
      for (unsigned id = 0; id < 16; ++id)
        if (input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, id)) b |= (uint16_t)(1u << id);
    }
    int raw[4];
    for (int a = 0; a < 4; ++a) {
      const int local = input_state_cb(p, RETRO_DEVICE_ANALOG, AX_INDEX[a], AX_ID[a]);
      const int net = rp.ax[a];
      raw[a] = abs(net) > abs(local) ? net : local;
    }
    A.pads[p].btn = b;
    A.pads[p].lx = axis(raw[0]);
    A.pads[p].ly = axis(raw[1]);
    // Levetta destra (mira): valori interi, la zona morta circolare la applica il gioco secondo la
    // mira scelta; qui si azzera solo il rumore attorno al centro.
    float rx = axis_raw(raw[2]);
    float ry = axis_raw(raw[3]);
    stick_stats((int)p, rx, ry);
    if (rx * rx + ry * ry < 0.2f * 0.2f) rx = ry = 0.0f;
    A.pads[p].rx = rx;
    A.pads[p].ry = ry;
    // La prima volta che arriva qualcosa da una levetta lo si scrive nel registro: se la mira non
    // funziona su un controller, dal registro si capisce se la levetta destra arriva al gioco.
    if (!A.stick_seen[p][0] && (A.pads[p].lx != 0.0f || A.pads[p].ly != 0.0f)) {
      A.stick_seen[p][0] = true;
      plat_log("giocatore %u: levetta sinistra ricevuta", p + 1);
    }
    if (!A.stick_seen[p][1] && rx * rx + ry * ry > 0.3f * 0.3f) {
      A.stick_seen[p][1] = true;
      plat_log("giocatore %u: levetta destra ricevuta", p + 1);
    }
  }
}

// START e SELECT agiscono al rilascio e solo dopo una pressione breve: sulla console tenere
// premuto START due secondi apre il menu di RetroArch, e al ritorno non deve succedere nulla.
//  - START breve, qualunque giocatore: pausa in partita, conferma nei menu;
//  - SELECT breve, giocatore 1: riquadro delle prestazioni;
//  - SELECT+START tenuti, giocatore 1: esce e torna al menu della console.
static void handle_controls(void) {
  const uint16_t m_sel = 1u << PAD_SELECT, m_start = 1u << PAD_START;
  const bool sel = (A.pads[0].btn & m_sel) != 0, start0 = (A.pads[0].btn & m_start) != 0;
  if (sel && start0) {
    A.combo = true;
    if (++A.quit_hold >= QUIT_FRAMES) request_shutdown("SELECT+START", "giocatore");
  } else {
    A.quit_hold = 0;
  }
  for (int p = 0; p < MAX_PLAYERS; ++p) {
    if (A.pads[p].btn & m_start) {
      if (A.start_hold[p] < 10000) A.start_hold[p]++;
      continue;
    }
    const bool tap = A.start_hold[p] > 0 && A.start_hold[p] <= TAP_FRAMES && !(p == 0 && A.combo);
    A.start_hold[p] = 0;
    if (tap) game_start_tap(p);
  }
  if (sel) {
    if (A.sel_hold < 10000) A.sel_hold++;
  } else {
    if (A.sel_hold > 0 && A.sel_hold <= TAP_FRAMES && !A.combo) A.perf = !A.perf;
    A.sel_hold = 0;
  }
  if (A.combo && !sel && !start0) A.combo = false;   // i due tasti della combinazione non valgono come pressioni singole
}

// --- riquadro delle prestazioni (SELECT) -------------------------------------

// Una riga in basso a sinistra, sotto le finestre di chi entra in partita (che finiscono a 432).
static void perf_overlay(Hud* h) {
  char buf[160];
  snprintf(buf, sizeof(buf), "%.1f fps · %.1f ms (max %.1f) · %s %.1f ms · tri %d/%d", (double)A.fps, (double)A.frame_avg,
           (double)A.frame_max, A.use_gl ? (game_aa_wanted() ? "GPU+AA" : "GPU") : "CPU", (double)A.e_core, A.st.tris_drawn,
           A.st.tris_in);
  const float w = hud_text_w(HF_S13, buf) + 24.0f;
  hud_box(h, 24, 440, w, 24, 6, hud_a(0x18081E, 0.85f));
  hud_text(h, 36, hud_mid(HF_S13, 452), HF_S13, 0xFFFFFFFFu, buf);
}

// --- contesto hardware ----------------------------------------------------

static void context_reset(void) {
  plat_log("context_reset: il frontend ha creato il contesto GL");
  A.gl_ready = glr_init((glr_get_proc_t)hw.get_proc_address);
  if (!A.gl_ready) plat_log("GL: preparazione fallita, lo schermo restera' nero");
  game_set_aa_available(A.gl_ready && glr_aa_available());
}

static void context_destroy(void) {
  plat_log("context_destroy");
  glr_context_lost();
  A.gl_ready = false;
  game_set_aa_available(false);
}

static bool try_hw_render(void) {
  memset(&hw, 0, sizeof(hw));
  hw.context_type = RETRO_HW_CONTEXT_OPENGLES2;
  hw.version_major = 2;
  hw.version_minor = 0;
  hw.context_reset = context_reset;
  hw.context_destroy = context_destroy;
  hw.depth = true;
  hw.stencil = true;              // con RetroArch e' l'unico modo di avere 24 bit di profondita'
  hw.bottom_left_origin = true;
  return env_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw);
}

// --- interfaccia libretro -------------------------------------------------

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_set_environment(retro_environment_t cb) {
  // Chiamata piu' volte, anche su un caricamento di prova senza retro_init: niente effetti collaterali.
  env_cb = cb;
  bool no_game = true;
  cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
  static const struct retro_variable vars[] = {
    { "pupazzi_renderer", "Renderer (serve riavviare il core); gpu|software" },
    { NULL, NULL }
  };
  cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
  struct retro_log_callback lg;
  lg.log = NULL;
  if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &lg) && lg.log) log_cb = lg.log;
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }

RETRO_API void retro_get_system_info(struct retro_system_info* info) {
  memset(info, 0, sizeof(*info));
  info->library_name = "Pupazzi";
  info->library_version = PUPAZZI_VERSION;
  info->valid_extensions = "";
  info->need_fullpath = false;
  info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info) {
  memset(info, 0, sizeof(*info));
  info->geometry.base_width = (unsigned)A.w;
  info->geometry.base_height = (unsigned)A.h;
  info->geometry.max_width = MAX_W;
  info->geometry.max_height = MAX_H;
  info->geometry.aspect_ratio = 4.0f / 3.0f;
  info->timing.fps = 60.0;
  info->timing.sample_rate = (double)AUDIO_RATE;
}

static int env_int(const char* name, int def) {
  const char* v = getenv(name);
  return (v && *v) ? atoi(v) : def;
}

// Prove: con i giocatori automatici (o con PUPAZZI_MAPPA) si parte subito dalla mappa
// PUPAZZI_MAPPA (1 = la prima), con PUPAZZI_GIOCATORI giocatori e i personaggi PUPAZZI_PERSONAGGI.
static void quick_start(void) {
  const int map = env_int("PUPAZZI_MAPPA", 0);
  if (!A.bots && map <= 0) return;
  const int players = A.bots ? A.bots : env_int("PUPAZZI_GIOCATORI", 1);
  game_quick_start(map > 0 ? map - 1 : 0, players, getenv("PUPAZZI_PERSONAGGI"));
}

RETRO_API void retro_init(void) {
  memset(&A, 0, sizeof(A));
  plat_set_sink(log_sink);
  const char* out = getenv("PUPAZZI_OUT");
  snprintf(A.out_dir, sizeof(A.out_dir), "%s", (out && *out) ? out : "/tmp");
  char path[320];
  snprintf(path, sizeof(path), "%s/pupazzi_log.txt", A.out_dir);
  plat_log_open(path);
  {
    const time_t now = time(NULL);
    struct tm tmv;
    char stamp[40];
    localtime_r(&now, &tmv);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);
    plat_log("=== pupazzi %s (build %s) | %s | arch %s ===", PUPAZZI_VERSION, PUPAZZI_BUILD, stamp, PUPAZZI_ARCH);
  }
  {
    // Sonoro: effetti e musica sintetizzati all'avvio (qualche decimo di secondo sulla console).
    const double t = plat_now_ms();
    audio_init(AUDIO_RATE);
    plat_log("sonoro pronto in %.0f ms", plat_now_ms() - t);
  }
  game_init();
  snprintf(g_aa_try.path, sizeof(g_aa_try.path), "%s/aa_in_prova.txt", A.out_dir);
  g_aa_try.frames = -1;
  g_aa_try.passed = false;
  g_aa_try.crashed = false;
  if (FILE* f = fopen(g_aa_try.path, "r")) {
    fclose(f);
    remove(g_aa_try.path);
    game_set_aa_on(false);
    g_aa_try.crashed = true;
    plat_log("antialias spento all'avvio: la volta scorsa il gioco si e' chiuso mentre lo provava (si riaccende dalla pausa)");
  }
  if (!swr_init(MAX_W, MAX_H)) plat_log("ERRORE: memoria insufficiente per il renderer software");
  A.ncpu = (int)sysconf(_SC_NPROCESSORS_CONF);
  // Risoluzione: 640x480 come lo schermo della console; PUPAZZI_RES=320 o 960 per le prove.
  const int res = env_int("PUPAZZI_RES", 640);
  A.w = res == 320 ? 320 : (res == 960 ? 960 : 640);
  A.h = A.w * 3 / 4;
  const char* hud = getenv("PUPAZZI_HUD");
  A.hud_off = hud && *hud == '0';
  A.hud_fixed = hud && !strcmp(hud, "fisso");
  A.hdmi_force = env_int("PUPAZZI_HDMI", -1);
  if (A.hdmi_force > 1) A.hdmi_force = 1;
  hdmi_poll(true);
  A.bots = env_int("PUPAZZI_BOT", 0);
  if (A.bots < 0) A.bots = 0;
  if (A.bots > MAX_PLAYERS) A.bots = MAX_PLAYERS;
  A.max_frames = (unsigned)env_int("PUPAZZI_FRAMES", 0);
  A.perf = env_int("PUPAZZI_PERF", 0) != 0;
  A.trace = env_int("PUPAZZI_TRACE", 0) != 0;   // stato della partita a ogni fotogramma nel registro (prove)
  A.trace_m = env_int("PUPAZZI_TRACE", 0) >= 2;  // ... e tutti i campi dei mostri
  quick_start();
}

RETRO_API void retro_deinit(void) {
  if (g_aa_try.frames >= 0) aa_try_mark(false);   // uscita normale: l'antialias non ha fatto danni
  remote_close();    // se il frontend non ha chiamato retro_unload_game
  log_stick_stats();
  GameInfo gi;
  game_info(&gi);
  plat_log("fine sessione dopo %u fotogrammi (uscita: %s): mappa %d (giro %d) zona %d, %d giocatori, mostri sconfitti %d, cadute %d, zone rifatte %d, mappe ricominciate da capo %d, mappe completate %d",
           A.frame, A.exit_reason ? A.exit_reason : "frontend", gi.map, gi.loop, gi.zone, gi.players, gi.kills, gi.deaths, gi.wipes, gi.restarts,
           gi.maps_done);
  write_status();
  save_write();      // cio' che il gioco ha cambiato e non ha ancora scritto (se nulla e' cambiato non tocca il file)
  glr_shutdown();
  swr_shutdown();
  game_shutdown();
  plat_log_close();
}

// La cartella c'e' (o la si crea adesso)?
static bool dir_ready(const char* d) {
  struct stat st;
  if (stat(d, &st) != 0) {
    if (mkdir(d, 0777) == 0) return true;
    if (errno != EEXIST || stat(d, &st) != 0) return false;     // errno dice perche'
  }
  if (S_ISDIR(st.st_mode)) return true;
  errno = ENOTDIR;
  return false;
}

// Salvataggio nella cartella dei salvataggi di RetroArch (un aggiornamento della MOD svuota APPS,
// quella no), altrimenti in quella dei risultati; PUPAZZI_SALVATAGGI=<cartella> vale piu' di
// entrambe (prove). Da retro_load_game: prima il frontend non ha ancora preparato le cartelle.
// Il RetroArch della console la porta su /mnt/mmc/saves_RA/ (/mnt/sdcard/... con la seconda
// scheda) e la crea all'avvio: se non c'e' e non si riesce a crearla, si salva tra i risultati.
static void open_save(void) {
  const char* dir = getenv("PUPAZZI_SALVATAGGI");
  if (!(dir && *dir)) {
    const char* ra = NULL;
    dir = A.out_dir;
    if (env_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &ra) && ra && *ra) {
      if (dir_ready(ra)) dir = ra;
      else plat_log("salvataggio: la cartella di RetroArch %s non c'e' e non si puo' creare (%s): si salva tra i risultati",
                    ra, strerror(errno));
    }
  }
  save_open(dir);
  plat_log("salvataggio: %s (%s)", save_path(), save_status());
  if (g_aa_try.crashed) {
    // L'antialias ha fatto chiudere il gioco: resta spento anche le prossime volte (si riaccende dalla pausa).
    save_data()->antialias = false;
    save_write();
  }
  game_save_loaded();
  // PUPAZZI_MIRA=0..3: mira extra morbida, morbida, media o rapida per tutti (prove; in gioco si
  // sceglie nella pausa). Vale piu' della mira salvata.
  const int mira = env_int("PUPAZZI_MIRA", -1);
  if (mira >= 0)
    for (int p = 0; p < MAX_PLAYERS; ++p) game_set_aim_level(p, mira);
}

RETRO_API bool retro_load_game(const struct retro_game_info* info) {
  (void)info;
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  if (!env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
    plat_log("ERRORE: il frontend non accetta XRGB8888");
    return false;
  }
  static const struct retro_input_descriptor desc[] = {
#define PUPAZZI_PAD(p) \
    { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Sinistra" }, \
    { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Destra" }, \
    { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Su" }, \
    { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Giu'" }, \
    { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Indietro (menu)" }, \
    { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Conferma (menu)" }, \
    { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "Colpo normale" }, \
    { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "Colpo speciale" }, \
    { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "Salto" }, \
    { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "Bomba" }, \
    { p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Pausa" }, \
    { p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Movimento X" }, \
    { p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Movimento Y" }, \
    { p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Mira X" }, \
    { p, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Mira Y" },
    PUPAZZI_PAD(0) PUPAZZI_PAD(1) PUPAZZI_PAD(2) PUPAZZI_PAD(3)
#undef PUPAZZI_PAD
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Prestazioni (con START tenuti: esci)" },
    { 0, 0, 0, 0, NULL }
  };
  env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)desc);
  A.bitmasks = env_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);

  // Renderer: GPU salvo richiesta diversa (variabile d'ambiente, poi opzione del core).
  const char* want = getenv("PUPAZZI_RENDERER");
  struct retro_variable var = { "pupazzi_renderer", NULL };
  if (!(want && *want) && env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) want = var.value;
  A.want_gl = !(want && (!strcmp(want, "software") || !strcmp(want, "sw") || !strcmp(want, "cpu")));
  A.use_gl = false;
  A.gl_ready = false;
  if (A.want_gl) {
    A.use_gl = try_hw_render();
    if (!A.use_gl) plat_log("il frontend ha rifiutato il contesto OpenGL ES: si usa il renderer software");
  }

#ifdef __GLIBC__
  plat_log("glibc in uso %s (compilato per %d.%d)", gnu_get_libc_version(), __GLIBC__, __GLIBC_MINOR__);
#endif
  plat_log("cpu presenti %d, accese ora %d; risoluzione %dx%d", A.ncpu, (int)sysconf(_SC_NPROCESSORS_ONLN), A.w, A.h);
  plat_log("renderer richiesto: %s, ottenuto: %s", A.want_gl ? "gpu" : "software", A.use_gl ? "gpu" : "software");
  float hz = 0.0f;
  if (env_cb(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE, &hz)) plat_log("frequenza di aggiornamento del frontend: %.3f Hz", (double)hz);
  const char* self = NULL;
  if (env_cb(RETRO_ENVIRONMENT_GET_LIBRETRO_PATH, &self) && self) plat_log("core caricato da %s", self);
  plat_log("maschere di input: %s; cartella dei risultati: %s", A.bitmasks ? "si" : "no", A.out_dir);
  if (A.bots) plat_log("giocatori automatici: %d", A.bots);
  open_save();
  remote_close();    // retro_load_game di nuovo senza retro_unload_game: le porte di prima
  remote_open();
  return true;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t num) {
  (void)type; (void)info; (void)num;
  return false;
}

RETRO_API void retro_unload_game(void) { remote_close(); }
RETRO_API void retro_reset(void) {
  plat_log("reset: si torna al titolo");
  audio_reset();
  game_reset();
  quick_start();
}
RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void* data, size_t size) { (void)data; (void)size; return false; }
RETRO_API bool retro_unserialize(const void* data, size_t size) { (void)data; (void)size; return false; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char* code) { (void)index; (void)enabled; (void)code; }
RETRO_API void* retro_get_memory_data(unsigned id) { (void)id; return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }

static inline float ema(float old, double sample) { return old + ((float)sample - old) * 0.1f; }

RETRO_API void retro_run(void) {
  const double t0 = plat_now_ms();
  const double dt = A.last_start > 0.0 ? t0 - A.last_start : 0.0;
  A.last_start = t0;
  if (dt > 0.0) {
    A.win_sum += dt;
    A.win_n++;
    if (dt > A.win_max) A.win_max = dt;
    if (A.win_n >= 30) {
      A.frame_avg = (float)(A.win_sum / A.win_n);
      A.frame_max = (float)A.win_max;
      A.fps = 1000.0f / A.frame_avg;
      A.win_n = 0;
      A.win_sum = A.win_max = 0.0;
    }
  }

  read_input();
  if (A.frame % 60 == 59 && A.hdmi_force < 0 && A.hdmi_src != 0) hdmi_poll(false);   // ogni secondo: si puo' staccare in partita
  if (A.bots) game_bot_pads(A.pads, A.bots);
  handle_controls();
  if (A.use_gl && !A.gl_ready && ++A.gl_wait == 600) {
    plat_log("ERRORE: dopo 600 fotogrammi il contesto GL non e' ancora pronto");
    if (A.max_frames) request_shutdown("GL non disponibile", "gl_assente");
  }
  if (A.max_frames && A.frame + 1 >= A.max_frames) request_shutdown("limite di fotogrammi raggiunto", "limite");

  game_update(A.pads);
  if (A.trace) plat_log("traccia %u %016llx", A.frame, (unsigned long long)game_state_hash());
  if (A.trace_m) {
    plat_log("fotogramma %u", A.frame);
    game_trace_monsters([](const char* line) { plat_log("%s", line); });
  }
  if (game_wants_quit()) request_shutdown("menu: Esci", "giocatore");
  FrameDesc fd;
  game_build_frame(&fd, A.w, A.h, A.hud_off ? NULL : &g_hud);
  if (!A.hud_off && A.perf) {
    if (A.hud_fixed) A.fps = A.frame_avg = A.frame_max = A.e_core = 0.0f;
    perf_overlay(&g_hud);
  }
  const double t1 = plat_now_ms();

  RenderStats st;
  memset(&st, 0, sizeof(st));
  double t2;
  if (A.use_gl) {
    if (A.gl_ready) {
      fd.aa = game_aa_wanted();
      if (!g_aa_try.passed && fd.aa != (g_aa_try.frames >= 0)) aa_try_mark(fd.aa);
      glr_render(&fd, (unsigned)hw.get_current_framebuffer(), &st, false);
      game_set_aa_available(glr_aa_available());   // se la GPU ha rifiutato l'antialias, la pausa lo sa
      if (g_aa_try.frames >= 0 && ++g_aa_try.frames >= AA_TRY_FRAMES) {
        aa_try_mark(false);
        g_aa_try.passed = true;
      }
      t2 = plat_now_ms();
      video_cb(RETRO_HW_FRAME_BUFFER_VALID, (unsigned)A.w, (unsigned)A.h, 0);
    } else {
      t2 = plat_now_ms();
      video_cb(NULL, (unsigned)A.w, (unsigned)A.h, 0);
    }
  } else {
    const uint32_t* fb = swr_render(&fd, &st);
    t2 = plat_now_ms();
    video_cb(fb, (unsigned)A.w, (unsigned)A.h, (size_t)A.w * 4);
  }
  const double t3 = plat_now_ms();
  // Sonoro: sempre inviato (e' cio' che tiene il passo quando il video del frontend e' su un altro thread).
  audio_render(g_audio, AUDIO_FRAMES);
  const double t4 = plat_now_ms();
  if (audio_batch_cb) audio_batch_cb(g_audio, AUDIO_FRAMES);

  A.e_sim = ema(A.e_sim, t1 - t0);
  A.e_geo = ema(A.e_geo, st.ms_geo);
  A.e_ras = ema(A.e_ras, st.ms_ras);
  A.e_core = ema(A.e_core, t2 - t0);
  A.e_video = ema(A.e_video, t3 - t2);
  A.e_audio = ema(A.e_audio, t4 - t3);
  A.st = st;
  A.frame++;
  // Riga di stato periodica: prestazioni e situazione della partita.
  if (A.frame % STAT_PERIOD == 0) {
    GameInfo gi;
    game_info(&gi);
    static const char* const SCREEN[] = { "titolo", "modalita'", "mappe", "personaggi", "partita", "fine mappa", "storia",
                                          "salvataggi", "lingua" };
    const int NSCREEN = (int)(sizeof(SCREEN) / sizeof(SCREEN[0]));
    AudioStats as;
    audio_get_stats(&as);
    audio_clear_stats();
    // Vite di ogni posto ("3 2 - -": - chi non gioca).
    char lives[24];
    int nl = 0;
    for (int p = 0; p < MAX_PLAYERS; ++p)
      nl += snprintf(lives + nl, sizeof(lives) - (size_t)nl, gi.lives[p] >= 0 ? "%s%d" : "%s-", p ? " " : "", gi.lives[p] < 99 ? gi.lives[p] : 99);
    plat_log("stato: f=%u fps %.1f, frame %.1f (max %.1f) | core %.2f = sim %.2f + disegno %.2f | audio %.2f (voci %d, scartate %u) | RA %.2f | tri %d/%d%s | %s, mappa %d (giro %d) zona %d mostri %d giocatori %d/%d vite %s%s",
             A.frame, (double)A.fps, (double)A.frame_avg, (double)A.frame_max, (double)A.e_core, (double)A.e_sim, (double)(A.e_geo + A.e_ras),
             (double)A.e_audio, as.voices_peak, as.dropped, (double)A.e_video, st.tris_drawn, st.tris_in, A.use_gl && game_aa_wanted() ? " aa" : "",
             SCREEN[gi.screen < NSCREEN && gi.screen >= 0 ? gi.screen : 0], gi.map, gi.loop, gi.zone, gi.enemies, gi.alive, gi.players, lives,
             game_paused() ? " (pausa)" : "");
  }
}
