// Pupazzi - libretro core: interface with the frontend (RetroArch), general controls (pause, exit,
// performance overlay), inputs from the other consoles (Remote RetroPad), LAN game with each player
// on their own screen (net.h), log and exit status for the launch script.
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
#include "platform/libretro.h"
#include "game/game.h"
#include "game/view/view.h"
#include "engine/swr.h"
#include "engine/glr.h"
#include "platform/plat.h"
#include "engine/audio.h"
#include "game/save.h"
#include "engine/lang.h"
#include "platform/sysinfo.h"
#include "net/net.h"

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

// Biggest picture (declared to RetroArch at the start: on the console's Mali the GL context cannot be
// made again, so the resolution changes only within it, with SET_GEOMETRY). Since 0.5.6 1280x960.
#define MAX_W 1280
#define MAX_H 960
#define AUDIO_RATE 48000
#define AUDIO_FRAMES (AUDIO_RATE / 60)
#define STAT_PERIOD 600     // every how many frames a status line is written to the log
#define TAP_FRAMES 40       // START and SELECT count only if pressed for less than this (see handle_controls)
#define QUIT_FRAMES 45      // SELECT+START held this long: exit

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
  int w, h, ncpu;             // the frame handed to RetroArch (the resolution of GRAFICA, or PUPAZZI_RES)
  bool res_forced;            // PUPAZZI_RES (tests): the resolution of the settings does not count
  bool aa_adapt;               // PUPAZZI_AA_ADATTA=0: no automatic switch from 8 to 4 samples (tests)
  bool hud_off, hud_fixed;    // PUPAZZI_HUD=0: no HUD; =fisso: performance overlay without timings (comparable images)
  bool want_gl, use_gl, gl_ready;
  int gl_wait;
  bool bitmasks;
  int bots;                   // PUPAZZI_BOT=n: the first n players are bots (tests); in a LAN game, this console's player
  bool net_auto;              // PUPAZZI_RETE: automatic LAN game (tests), no quick game
  bool net_tap;               // in a LAN game: short START of this console's pad, to be sent with its inputs
  unsigned max_frames;        // PUPAZZI_FRAMES=n: automatic shutdown after n frames (tests)
  bool perf, trace, trace_m;
  bool stick_seen[MAX_PLAYERS][2];
  // Right stick, for the log: range of travel, noise at rest, jitter with the stick tilted.
  struct StickStats {
    float x0, x1, y0, y1;     // minimum and maximum values read
    float rest;               // maximum tilt with the stick still at the center
    int low_n, full_n;        // consecutive readings near the center / almost fully tilted
    float last_a;             // direction at the previous reading (degrees)
    int jit[41], jit_n;       // direction changes between two readings, in steps of 0.25 degrees
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
  int hdmi_force;             // PUPAZZI_HDMI=0/1 (tests): fixed screen; -1 read from the system
  int hdmi_src;               // from where: 0 nothing (stays as with HDMI), 1 extcon, 2 switch
  Game* game;                 // the game (created in retro_init, destroyed in retro_deinit)
  View* view;                 // ...and how this console shows it (camera, sounds, music, drawing)
} A;

// HDMI connected? The same file that the console's RetroArch reads ("HDMI=1"); on other kernels
// /sys/class/switch/hdmi/state ("1"). If neither exists (PC), it stays as with HDMI.
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
  if (on >= 0) view_set_hdmi(*A.view, on != 0);
}

static void log_sink(const char* line) {
  if (log_cb) log_cb(RETRO_LOG_INFO, "[pupazzi] %s\n", line);
}

// Antialiasing on trial: during the first 3 seconds it is on, the file aa_in_prova.txt exists in
// the results directory. If it is still there at startup, the previous time the game closed right
// there (for example in the GPU driver): start without antialiasing, which can be turned back on
// from the pause.
static const int AA_TRY_FRAMES = 180;
static struct {
  char path[320];
  int frames;    // frames with antialiasing since the file was created; -1 = no file
  bool passed;   // tried for 3 seconds in this session: the file is no longer needed
  bool crashed;  // at startup the file was still there: off in the save too (see open_save)
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

// How the session ended: the launch script on the console reports it in its log.
static void write_status(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/pupazzi_stato.txt", A.out_dir);
  FILE* f = fopen(path, "w");
  if (!f) return;
  GameInfo gi;
  game_info(*A.game, &gi);
  fprintf(f, "uscita=%s\nfotogrammi=%u\nrenderer=%s\narch=%s\nmappa=%d\ngiro=%d\nzona=%d\ngiocatori=%d\nmostri_sconfitti=%d\ncadute=%d\nmappe_completate=%d\n"
             "zone_rifatte=%d\nmappe_da_capo=%d\n",
          A.exit_reason ? A.exit_reason : "frontend", A.frame, A.use_gl ? "gpu" : "software", PUPAZZI_ARCH, gi.map, gi.loop, gi.zone,
          gi.players, gi.kills, gi.deaths, gi.maps_done, gi.wipes, gi.restarts);
  fclose(f);
}

// reason: giocatore, limite, gl_assente (without a request it stays "frontend").
static void request_shutdown(const char* why, const char* reason) {
  if (A.shutdown_sent) return;
  A.shutdown_sent = true;
  A.exit_reason = reason;
  plat_log("chiusura richiesta: %s", why);
  write_status();
  if (env_cb) env_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

// --- inputs from the other consoles (Remote RetroPad) -------------------------
// The controller consoles run RetroArch's Remote RetroPad core: for every button or axis that
// changes it sends a 20-byte UDP message (port, device, index, id: 32-bit integers; state: 16
// bits; little-endian), player N to port 55400+N-1 (the port field does not matter). RetroArch
// read only one per player per frame: with the sticks moving, even 4 or more arrive per frame, so
// the queue grew and the inputs arrived later and later (the character kept moving on its own).
// Now the game opens the ports (run.sh makes RetroArch keep them closed) and every frame it reads
// everything that has arrived: the most recent state counts. A held button sends nothing: the
// state stays until a change arrives (if it stays still for 30 seconds the game takes care of it,
// see FROZEN_FRAMES).
#define REMOTE_PORT 55400        // player 1; PUPAZZI_REMOTO_PORTA=n for tests
#define REMOTE_MSG_SIZE 20       // bytes of a message: the others are ignored
#define REMOTE_READ_MAX 1024     // max messages read per player in one frame
#define REMOTE_STALL_MS 500.0    // game stopped this long (RetroArch menu): the queued inputs are old
#define REMOTE_BURST 8           // this many messages in one frame: they arrived late all together

struct RemotePad {
  int fd;                        // -1: port closed
  uint16_t btn;                  // buttons pressed (bit = RetroPad id)
  int16_t ax[4];                 // left stick x y, right x y: raw values as they arrive
  uint32_t ip;                   // who sends them (address and port, as they come from the network)
  uint16_t sport;
  bool seen, err_logged;
  unsigned msgs, frames, max_frame, bursts;   // for the end-of-session log
  unsigned bad, stale, senders;
};

static struct {
  bool ready;                    // remote_open done (even if no port opened)
  int nopen;                     // open ports
  double last_ms;                // last read (to notice the game's pauses)
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

// Non-blocking UDP port; if it does not open, returns -errno. No SO_REUSEADDR: if two programs ask
// for it (RetroArch always does) the system lets both open the same port and the messages go only
// to the last one that arrived, with no errors. Without it, whoever comes second does not open it
// and the log says so (RetroArch opens its own after the game is loaded: with the Remote RetroPad
// left on, RetroArch would be the one left without ports).
static int remote_socket(int port) {
  // O_NONBLOCK already at creation (with fcntl the 32-bit core would want glibc 2.28).
  const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) return -errno;
  // Longer queue (the system caps it and then doubles it): copes better with a slow frame.
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

// From retro_load_game: one port per player. PUPAZZI_REMOTO=0 does not open them.
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

// From retro_unload_game and retro_deinit: closes the ports and logs what arrived.
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

// One message, as RetroArch reads it (input_remote_parse_packet), but without negative indices.
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

// Every frame, before reading the pads: all the messages that arrived on each port, in order.
static void remote_poll(void) {
  if (!R.ready || R.nopen == 0) return;
  const double now = plat_now_ms();
  const double gap = R.last_ms > 0.0 ? now - R.last_ms : 0.0;
  R.last_ms = now;
  // Game left stopped (RetroArch menu): the queue may have filled up and then the system
  // dropped the most recent messages. The remaining ones are old (a button released in the
  // meantime would stay pressed): they are all dropped and everything starts again from rest.
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
        // Another console on the same port, or the Remote RetroPad restarted (new source
        // port): whoever restarts sends only what is not at rest, so start again from rest.
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
    // Tests (PUPAZZI_TRACE): state from the network after this frame, whenever something arrives
    // or it changes (also when starting again from rest). Without messages it writes nothing.
    if (A.trace && (n > 0 || !remote_same(before, r)))
      plat_log("rete %u giocatore %d: %u messaggi, tasti %04x, levette %d %d %d %d", A.frame, p + 1, n, (unsigned)r.btn,
               (int)r.ax[0], (int)r.ax[1], (int)r.ax[2], (int)r.ax[3]);
  }
  if (who[0])
    plat_log("comandi dalla rete: gioco fermo per %.1f s (menu di RetroArch?), %u comandi arrivati nel frattempo scartati: giocatori%s ripartono da fermi",
             gap / 1000.0, dropped, who);
}

// --- input ----------------------------------------------------------------

// Stick: raw value (console pad or network) in [-1, 1].
static float axis_raw(int raw) {
  const float v = (float)raw / 32767.0f;
  return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

// Left stick: per-axis dead zone (moving along the axes stays easy).
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
  if (s.low_n > 30 && m > s.rest) s.rest = m;     // still at the center for half a second
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

// Direction change below which the fraction q of the readings falls (degrees, in steps of 0.25).
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
             p + 1, (double)s.x0, (double)s.x1, (double)s.y0, (double)s.y1, (double)s.rest, jit, game_aim_name(*A.game, p));
  }
}

// Console pad (frontend) and network inputs together: buttons pressed on either one; for each
// axis the value farthest from the center (a pad on the console and a controller console can act
// as the same player). Dead zones and sticks then as for a single pad.
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
    // Right stick (aim): full values, the game applies the circular dead zone according to the
    // chosen aim; here only the noise around the center is zeroed.
    float rx = axis_raw(raw[2]);
    float ry = axis_raw(raw[3]);
    stick_stats((int)p, rx, ry);
    if (rx * rx + ry * ry < 0.2f * 0.2f) rx = ry = 0.0f;
    A.pads[p].rx = rx;
    A.pads[p].ry = ry;
    // The first time something arrives from a stick it is written to the log: if aiming does not
    // work on a controller, the log shows whether the right stick reaches the game.
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

// START and SELECT act on release and only after a short press: on the console, holding START
// for two seconds opens the RetroArch menu, and nothing must happen on return.
//  - short START, any player: pause during a game, confirm in the menus;
//  - short SELECT, player 1: performance overlay;
//  - SELECT+START held, player 1: exits and goes back to the console menu.
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
    // In a LAN game the short START of this console's pad travels with its inputs (valid on all).
    if (tap && net_session()) A.net_tap |= p == 0;
    else if (tap) game_start_tap(*A.game, p);
  }
  if (sel) {
    if (A.sel_hold < 10000) A.sel_hold++;
  } else {
    if (A.sel_hold > 0 && A.sel_hold <= TAP_FRAMES && !A.combo) A.perf = !A.perf;
    A.sel_hold = 0;
  }
  if (A.combo && !sel && !start0) A.combo = false;   // the two buttons of the combination do not count as single presses
}

// 0.5.4: with the "best" antialiasing at 8 samples, if while playing the game cannot keep up 60
// frames per second (less than 55 in 3 measurements in a row, half a second each) it drops to 4
// for the rest of the session and writes that to the log; 8X can always be chosen from the pause.
static void aa_adapt(void) {
  static int slow;
  if (!A.aa_adapt || !A.use_gl || !A.gl_ready || game_aa_level(*A.game) != 1 || game_aa_wanted(*A.game) < 8 || !game_playing(*A.game)) {
    slow = 0;
    return;
  }
  slow = A.fps > 0.0f && A.fps < 55.0f ? slow + 1 : 0;
  if (slow >= 3) {
    game_aa_cap(*A.game, 4);
    plat_log("antialias: con 8 campioni %.1f fotogrammi al secondo, si passa a 4 (dalla pausa si sceglie di nuovo 8X)", (double)A.fps);
    slow = 0;
  }
}

// --- performance overlay (SELECT) --------------------------------------------

// One line at the bottom left, below the windows of players joining the game (which end at 432);
// since 0.5.4, above it a second line with the CPU load (read every half second while visible).
static void perf_overlay(Hud* h) {
  static unsigned cpu_t;
  static SysCpu cpu;
  if (cpu_t++ % 30u == 0u) {
    const SysCpu c = sys_cpu_sample();
    if (c.valid || !cpu.valid) cpu = c;
  }
  if (A.hud_fixed) {                 // comparable images (PUPAZZI_HUD=fisso): fixed numbers
    memset(&cpu, 0, sizeof(cpu));
    cpu.valid = true;
    cpu.ncore = 4;
  }
  char buf[160], rend[16];
  const int aa = A.use_gl ? game_aa_wanted(*A.game) : 0;
  if (aa > 0) snprintf(rend, sizeof(rend), "GPU+%dX", aa);
  else snprintf(rend, sizeof(rend), "%s", A.use_gl ? "GPU" : "CPU");
  snprintf(buf, sizeof(buf), "%.1f fps · %.1f ms (max %.1f) · %s %.1f ms · tri %d/%d", (double)A.fps, (double)A.frame_avg,
           (double)A.frame_max, rend, (double)A.e_core, A.st.tris_drawn, A.st.tris_in);
  float w = hud_text_w(HF_S13, buf) + 24.0f;
  hud_box(h, 24, 440, w, 24, 6, hud_a(0x18081E, 0.85f));
  hud_text(h, 36, hud_mid(HF_S13, 452), HF_S13, 0xFFFFFFFFu, buf);
  // In a LAN game (0.5.4): round-trip time, buffer, times it ran out (joining console).
  NetStats ns;
  net_stats(&ns);
  if (ns.on) {
    if (A.hud_fixed) {
      ns.rtt_ms = 0;
      ns.buffer = ns.target = 0;
      ns.stalls = 0;
    }
    if (ns.host) snprintf(buf, sizeof(buf), "%s · %d ms · %d console", tr("RETE"), ns.rtt_ms, ns.players);
    else snprintf(buf, sizeof(buf), "%s · %d ms · %s %d/%d · %u", tr("RETE"), ns.rtt_ms, tr("scorta"), ns.buffer, ns.target, ns.stalls);
    w = hud_text_w(HF_S13, buf) + 24.0f;
    hud_box(h, 24, cpu.valid ? 384 : 412, w, 24, 6, hud_a(0x18081E, 0.85f));
    hud_text(h, 36, hud_mid(HF_S13, cpu.valid ? 396.0f : 424.0f), HF_S13, 0xFFFFFFFFu, buf);
  }
  if (!cpu.valid) return;
  int n = snprintf(buf, sizeof(buf), "CPU %d%% ·", cpu.total);
  for (int i = 0; i < cpu.ncore && n < (int)sizeof(buf) - 8; ++i) n += snprintf(buf + n, sizeof(buf) - (size_t)n, " %d", cpu.core[i]);
  if (n < (int)sizeof(buf)) snprintf(buf + n, sizeof(buf) - (size_t)n, " · %s %d%%", tr("gioco"), cpu.game);
  w = hud_text_w(HF_S13, buf) + 24.0f;
  hud_box(h, 24, 412, w, 24, 6, hud_a(0x18081E, 0.85f));
  hud_text(h, 36, hud_mid(HF_S13, 424), HF_S13, cpu.total >= 90 ? 0xFFFF8A6Au : 0xFFFFFFFFu, buf);
}

// --- hardware context -----------------------------------------------------

static void context_reset(void) {
  plat_log("context_reset: il frontend ha creato il contesto GL");
  A.gl_ready = glr_init((glr_get_proc_t)hw.get_proc_address);
  if (!A.gl_ready) plat_log("GL: preparazione fallita, lo schermo restera' nero");
  game_set_aa_available(*A.game, A.gl_ready ? glr_aa_max() : 0);
}

static void context_destroy(void) {
  plat_log("context_destroy");
  glr_context_lost();
  A.gl_ready = false;
  game_set_aa_available(*A.game, 0);
}

static bool try_hw_render(void) {
  memset(&hw, 0, sizeof(hw));
  hw.context_type = RETRO_HW_CONTEXT_OPENGLES2;
  hw.version_major = 2;
  hw.version_minor = 0;
  hw.context_reset = context_reset;
  hw.context_destroy = context_destroy;
  hw.depth = true;
  hw.stencil = true;              // with RetroArch it is the only way to get 24 bits of depth
  hw.bottom_left_origin = true;
  return env_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw);
}

// --- video output ---------------------------------------------------------

// How a frame reaches RetroArch (Strategy): the GPU renderer draws it straight into the frontend's
// framebuffer, the software renderer draws it in memory and hands it over as pixels. Both take the
// same FrameDesc. Chosen once in retro_load_game (g_video); the frame loop always calls draw, which
// returns when the drawing ended (before handing the frame over: timings of the overlay).
struct VideoOut {
  const char* name;                                  // in the log: "gpu" or "software"
  double (*draw)(FrameDesc& fd, RenderStats* st);
};

static double draw_gpu(FrameDesc& fd, RenderStats* st) {
  if (!A.gl_ready) {                       // no context (not yet, or lost): an empty frame
    const double t = plat_now_ms();
    video_cb(NULL, (unsigned)A.w, (unsigned)A.h, 0);
    return t;
  }
  fd.aa = game_aa_wanted(*A.game);
  if (!g_aa_try.passed && (fd.aa > 0) != (g_aa_try.frames >= 0)) aa_try_mark(fd.aa > 0);
  glr_render(&fd, (unsigned)hw.get_current_framebuffer(), st, false);
  game_set_aa_available(*A.game, glr_aa_max());   // if the GPU refused antialiasing (or the 8 samples), the pause knows
  if (g_aa_try.frames >= 0 && ++g_aa_try.frames >= AA_TRY_FRAMES) {
    aa_try_mark(false);
    g_aa_try.passed = true;
  }
  const double t = plat_now_ms();
  video_cb(RETRO_HW_FRAME_BUFFER_VALID, (unsigned)A.w, (unsigned)A.h, 0);
  return t;
}

// TV margin with the software renderer: the picture (drawn at its size) in the middle of the frame,
// black around it. The frame is cleared only when the size or the margin changes.
static const uint32_t* framed(const uint32_t* pic, const FrameDesc& fd) {
  static uint32_t* buf;
  static int bw, bh, bx, by;
  if (!buf) buf = (uint32_t*)calloc((size_t)MAX_W * MAX_H, sizeof(uint32_t));
  if (!buf) return pic;
  if (bw != fd.fw || bh != fd.fh || bx != fd.ox || by != fd.oy) {
    memset(buf, 0, (size_t)fd.fw * fd.fh * sizeof(uint32_t));
    bw = fd.fw; bh = fd.fh; bx = fd.ox; by = fd.oy;
  }
  for (int y = 0; y < fd.h; ++y) memcpy(buf + (size_t)(fd.oy + y) * fd.fw + fd.ox, pic + (size_t)y * fd.w, (size_t)fd.w * sizeof(uint32_t));
  return buf;
}

static double draw_software(FrameDesc& fd, RenderStats* st) {
  const uint32_t* fb = swr_render(&fd, st);
  if (fd.fw > fd.w || fd.fh > fd.h) fb = framed(fb, fd);
  const double t = plat_now_ms();
  video_cb(fb, (unsigned)A.w, (unsigned)A.h, (size_t)A.w * 4);
  return t;
}

static const VideoOut VIDEO_GPU = { "gpu", draw_gpu };
static const VideoOut VIDEO_SOFTWARE = { "software", draw_software };
static const VideoOut* g_video = &VIDEO_SOFTWARE;

// --- graphics settings (0.5.6, GRAFICA in the pause) ------------------------------------------

// Every frame: the settings of this console (in save_data, changed from the pause) go to the view
// (camera, details) and to the frame. The resolution changes with SET_GEOMETRY within MAX_W x MAX_H.
static void apply_gfx(void) {
  const SaveData* sd = save_data();
  view_set_gfx(*A.view, sd->cam_dist, sd->cam_pitch, sd->detail == 0);
  if (A.res_forced) return;
  const int w = sd->res == 320 || sd->res == 960 || sd->res == 1280 ? sd->res : 640;
  if (w == A.w) return;
  A.w = w;
  A.h = w * 3 / 4;
  struct retro_game_geometry geo;
  memset(&geo, 0, sizeof(geo));
  geo.base_width = (unsigned)A.w;
  geo.base_height = (unsigned)A.h;
  geo.max_width = MAX_W;
  geo.max_height = MAX_H;
  geo.aspect_ratio = 4.0f / 3.0f;
  const bool ok = env_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geo);
  plat_log("risoluzione %dx%d%s", A.w, A.h, ok ? "" : " (il frontend non accetta SET_GEOMETRY)");
}

// The picture inside the frame: with the TV margin (0 to 4) 2.5% smaller on every side per step.
static void picture_rect(int* x, int* y, int* w, int* h) {
  const int m = save_data()->margin <= SAVE_MARGIN_MAX ? save_data()->margin : SAVE_MARGIN_MAX;
  *x = m * A.w / 40;
  *y = m * A.h / 40;
  *w = A.w - 2 * *x;
  *h = A.h - 2 * *y;
}

// --- libretro interface ---------------------------------------------------

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_set_environment(retro_environment_t cb) {
  // Called several times, even on a trial load without retro_init: no side effects.
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

// Tests: with bots (or with PUPAZZI_MAPPA) the game starts right away from map PUPAZZI_MAPPA
// (1 = the first one), with PUPAZZI_GIOCATORI players and the characters PUPAZZI_PERSONAGGI.
static void quick_start(void) {
  const int map = env_int("PUPAZZI_MAPPA", 0);
  if (A.net_auto || (!A.bots && map <= 0)) return;
  const int players = A.bots ? A.bots : env_int("PUPAZZI_GIOCATORI", 1);
  game_quick_start(*A.game, map > 0 ? map - 1 : 0, players, getenv("PUPAZZI_PERSONAGGI"));
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
    // Sound: sound effects and music synthesized at startup (a few tenths of a second on console).
    const double t = plat_now_ms();
    audio_init(AUDIO_RATE);
    plat_log("sonoro pronto in %.0f ms", plat_now_ms() - t);
  }
  A.game = game_create();
  A.view = view_create();
  game_set_events(*A.game, view_events(A.view));
  game_init(*A.game);
  snprintf(g_aa_try.path, sizeof(g_aa_try.path), "%s/aa_in_prova.txt", A.out_dir);
  g_aa_try.frames = -1;
  g_aa_try.passed = false;
  g_aa_try.crashed = false;
  if (FILE* f = fopen(g_aa_try.path, "r")) {
    fclose(f);
    remove(g_aa_try.path);
    game_set_aa_level(*A.game, 0);
    g_aa_try.crashed = true;
    plat_log("antialias spento all'avvio: la volta scorsa il gioco si e' chiuso mentre lo provava (si riaccende dalla pausa)");
  }
  if (!swr_init(MAX_W, MAX_H)) plat_log("ERRORE: memoria insufficiente per il renderer software");
  A.ncpu = (int)sysconf(_SC_NPROCESSORS_CONF);
  // Resolution: 640x480 like the console screen until the settings are read (retro_load_game, then
  // GRAFICA in the pause); PUPAZZI_RES=320, 640, 960 or 1280 fixes it (tests).
  const int res = env_int("PUPAZZI_RES", 0);
  A.res_forced = res > 0;
  A.w = res == 320 ? 320 : (res == 960 ? 960 : (res == 1280 ? 1280 : 640));
  A.h = A.w * 3 / 4;
  const char* hud = getenv("PUPAZZI_HUD");
  A.hud_off = hud && *hud == '0';
  A.hud_fixed = hud && !strcmp(hud, "fisso");
  A.aa_adapt = env_int("PUPAZZI_AA_ADATTA", 1) != 0;
  A.hdmi_force = env_int("PUPAZZI_HDMI", -1);
  if (A.hdmi_force > 1) A.hdmi_force = 1;
  hdmi_poll(true);
  A.bots = env_int("PUPAZZI_BOT", 0);
  if (A.bots < 0) A.bots = 0;
  if (A.bots > MAX_PLAYERS) A.bots = MAX_PLAYERS;
  A.max_frames = (unsigned)env_int("PUPAZZI_FRAMES", 0);
  A.perf = env_int("PUPAZZI_PERF", 0) != 0;
  A.trace = env_int("PUPAZZI_TRACE", 0) != 0;   // game state every frame in the log (tests)
  A.trace_m = env_int("PUPAZZI_TRACE", 0) >= 2;  // ... and all the monster fields
  const char* rete = getenv("PUPAZZI_RETE");
  const char* dopo = getenv("PUPAZZI_RETE_DOPO");
  A.net_auto = rete && *rete && !(dopo && atoi(dopo) > 0);   // with PUPAZZI_RETE_DOPO you play alone first
  quick_start();
}

RETRO_API void retro_deinit(void) {
  if (g_aa_try.frames >= 0) aa_try_mark(false);   // normal exit: antialiasing did no harm
  net_shutdown();    // LAN game: says bye to the other consoles (and, if it joined, restores its own saves)
  remote_close();    // if the frontend did not call retro_unload_game
  log_stick_stats();
  GameInfo gi;
  game_info(*A.game, &gi);
  plat_log("fine sessione dopo %u fotogrammi (uscita: %s): mappa %d (giro %d) zona %d, %d giocatori, mostri sconfitti %d, cadute %d, zone rifatte %d, mappe ricominciate da capo %d, mappe completate %d",
           A.frame, A.exit_reason ? A.exit_reason : "frontend", gi.map, gi.loop, gi.zone, gi.players, gi.kills, gi.deaths, gi.wipes, gi.restarts,
           gi.maps_done);
  write_status();
  save_write();      // what the game changed and has not written yet (if nothing changed it does not touch the file)
  glr_shutdown();
  swr_shutdown();
  game_shutdown(*A.game);
  game_destroy(A.game);
  view_destroy(A.view);
  A.game = NULL;
  A.view = NULL;
  plat_log_close();
}

// Does the directory exist (or is it created now)?
static bool dir_ready(const char* d) {
  struct stat st;
  if (stat(d, &st) != 0) {
    if (mkdir(d, 0777) == 0) return true;
    if (errno != EEXIST || stat(d, &st) != 0) return false;     // errno says why
  }
  if (S_ISDIR(st.st_mode)) return true;
  errno = ENOTDIR;
  return false;
}

// Save in RetroArch's saves directory (an update of the MOD empties APPS, not that one),
// otherwise in the results directory; PUPAZZI_SALVATAGGI=<directory> overrides both (tests).
// From retro_load_game: before that the frontend has not prepared the directories yet.
// The console's RetroArch puts it at /mnt/mmc/saves_RA/ (/mnt/sdcard/... with the second card)
// and creates it at startup: if it is missing and cannot be created, saving goes to the results.
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
    // Antialiasing made the game close: it stays off next time too (it can be turned on again from the pause).
    save_data()->antialias = 0;
    save_write();
  }
  // PUPAZZI_LINGUA=it|en|es|pt|zh|ja|hi: the language for this time only (tests; in game it is
  // chosen from the first screen), instead of the saved one, which stays as it was in the file.
  const uint8_t saved_lang = save_data()->lang;
  const char* lg = getenv("PUPAZZI_LINGUA");
  for (int l = 0; lg && l < SAVE_LANGS; ++l)
    if (!strcmp(lg, save_lang_id(l))) {
      save_data()->lang = (uint8_t)l;
      plat_log("lingua (PUPAZZI_LINGUA): %s", lg);
    }
  game_save_loaded(*A.game);
  save_data()->lang = saved_lang;
  // PUPAZZI_MIRA=0..3: extra soft, soft, medium or fast aim for everyone (tests; in game it is
  // chosen in the pause). It overrides the saved aim.
  const int mira = env_int("PUPAZZI_MIRA", -1);
  if (mira >= 0)
    for (int p = 0; p < MAX_PLAYERS; ++p) game_set_aim_level(*A.game, p, mira);
}

// Button names for RetroArch's controls menu, in the game's language; for Chinese, Japanese and
// Hindi in English (RetroArch's glyphs usually do not cover them). They are sent again when the
// language changes.
static int g_desc_lang = -1;

static void send_descriptors(void) {
  struct Key { unsigned device, index, id; const char* name; };
  static const Key KEYS[] = {
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, N_("Sinistra") },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, N_("Destra") },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, N_("Su") },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, N_("Giù") },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, N_("Indietro (menu)") },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, N_("Conferma (menu)") },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, N_("Colpo normale") },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, N_("Colpo speciale") },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, N_("Salto") },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, N_("Bomba") },
    { RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, N_("Pausa") },
    { RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, N_("Movimento X") },
    { RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, N_("Movimento Y") },
    { RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, N_("Mira X") },
    { RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, N_("Mira Y") },
  };
  const int NK = (int)(sizeof(KEYS) / sizeof(KEYS[0]));
  static struct retro_input_descriptor desc[MAX_PLAYERS * NK + 2];
  const int l = lang_get(), dl = l == LANG_ZH || l == LANG_JA || l == LANG_HI ? LANG_EN : l;
  int n = 0;
  for (unsigned p = 0; p < MAX_PLAYERS; ++p)
    for (int k = 0; k < NK; ++k) desc[n++] = { p, KEYS[k].device, KEYS[k].index, KEYS[k].id, tr_in(dl, KEYS[k].name) };
  desc[n++] = { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, tr_in(dl, N_("Prestazioni (con START tenuti: esci)")) };
  desc[n] = { 0, 0, 0, 0, NULL };
  env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)desc);
  g_desc_lang = l;
}

RETRO_API bool retro_load_game(const struct retro_game_info* info) {
  (void)info;
  enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
  if (!env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
    plat_log("ERRORE: il frontend non accetta XRGB8888");
    return false;
  }
  A.bitmasks = env_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL);

  // Renderer: GPU unless requested otherwise (environment variable, then core option).
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
  g_video = A.use_gl ? &VIDEO_GPU : &VIDEO_SOFTWARE;
  plat_log("renderer richiesto: %s, ottenuto: %s", A.want_gl ? "gpu" : "software", g_video->name);
  float hz = 0.0f;
  if (env_cb(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE, &hz)) plat_log("frequenza di aggiornamento del frontend: %.3f Hz", (double)hz);
  const char* self = NULL;
  if (env_cb(RETRO_ENVIRONMENT_GET_LIBRETRO_PATH, &self) && self) plat_log("core caricato da %s", self);
  plat_log("maschere di input: %s; cartella dei risultati: %s", A.bitmasks ? "si" : "no", A.out_dir);
  if (A.bots) plat_log("giocatori automatici: %d", A.bots);
  open_save();
  if (!A.res_forced) {
    // The resolution chosen on this console (GRAFICA): RetroArch asks for it after retro_load_game.
    A.w = save_data()->res;
    A.h = A.w * 3 / 4;
    plat_log("risoluzione dalle impostazioni: %dx%d", A.w, A.h);
  }
  send_descriptors();
  remote_close();    // retro_load_game again without retro_unload_game: the previous ports
  remote_open();
  net_init(*A.game);
  return true;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info* info, size_t num) {
  (void)type; (void)info; (void)num;
  return false;
}

RETRO_API void retro_unload_game(void) {
  net_shutdown();
  remote_close();
}
RETRO_API void retro_reset(void) {
  plat_log("reset: si torna al titolo");
  net_shutdown();
  audio_reset();
  game_reset(*A.game);
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
      aa_adapt();
    }
  }

  read_input();
  if (lang_get() != g_desc_lang) send_descriptors();
  if (A.frame % 60 == 59 && A.hdmi_force < 0 && A.hdmi_src != 0) hdmi_poll(false);   // each second: it can be unplugged mid-game
  if (A.bots && !A.net_auto && !net_session()) game_bot_pads(*A.game, A.pads, A.bots);
  handle_controls();
  if (A.use_gl && !A.gl_ready && ++A.gl_wait == 600) {
    plat_log("ERRORE: dopo 600 fotogrammi il contesto GL non e' ancora pronto");
    if (A.max_frames) request_shutdown("GL non disponibile", "gl_assente");
  }
  if (A.max_frames && A.frame + 1 >= A.max_frames) request_shutdown("limite di fotogrammi raggiunto", "limite");

  net_frame_begin();
  if (net_session()) {
    // LAN game: this console's inputs are sent, the frames are run when everyone's inputs are there
    // (the host: always one; the others: from none to a few more).
    Pad mine = A.pads[0];
    if (A.bots) game_bot_pad(*A.game, game_net_local(*A.game), &mine);
    net_local_pad(mine, A.net_tap);
    A.net_tap = false;
    Pad pads[MAX_PLAYERS];
    bool taps[MAX_PLAYERS];
    uint8_t consoles = 0, silent = 0;
    while (net_step(pads, taps, &consoles, &silent)) {
      game_net_frame(*A.game, consoles, silent, taps);
      game_update(*A.game, pads);
      view_step(*A.view, *A.game);
      const uint64_t hh = game_state_hash(*A.game);
      net_step_done(hh);
      if (A.trace) plat_log("rete traccia %u %016llx", net_frame_no(), (unsigned long long)hh);
      if (A.trace_m) {
        plat_log("fotogramma rete %u", net_frame_no());
        game_trace_monsters(*A.game, [](const char* line) { plat_log("%s", line); });
      }
      if (!net_session()) break;
    }
  } else {
    A.net_tap = false;
    game_update(*A.game, A.pads);
    view_step(*A.view, *A.game);
    if (A.trace) plat_log("traccia %u %016llx", A.frame, (unsigned long long)game_state_hash(*A.game));
    if (A.trace_m) {
      plat_log("fotogramma %u", A.frame);
      game_trace_monsters(*A.game, [](const char* line) { plat_log("%s", line); });
    }
  }
  int req_arg = 0;
  const int req = game_net_request(*A.game, &req_arg);
  if (req != NR_NONE) net_request(req, req_arg);
  net_frame_end();
  if (game_wants_quit(*A.game)) request_shutdown("menu: Esci", "giocatore");
  apply_gfx();
  FrameDesc fd;
  int px, py, pw, ph;
  picture_rect(&px, &py, &pw, &ph);
  view_build_frame(*A.view, *A.game, &fd, pw, ph, A.hud_off ? NULL : &g_hud);
  fd.fw = A.w;
  fd.fh = A.h;
  fd.ox = px;
  fd.oy = py;
  if (!A.hud_off && A.perf) {
    if (A.hud_fixed) A.fps = A.frame_avg = A.frame_max = A.e_core = 0.0f;
    perf_overlay(&g_hud);
  }
  const double t1 = plat_now_ms();

  RenderStats st;
  memset(&st, 0, sizeof(st));
  const double t2 = g_video->draw(fd, &st);
  const double t3 = plat_now_ms();
  // Sound: always sent (it is what keeps the pace when the frontend's video is on another thread).
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
  // Periodic status line: performance and state of the game.
  if (A.frame % STAT_PERIOD == 0) {
    GameInfo gi;
    game_info(*A.game, &gi);
    static const char* const SCREEN[] = { "titolo", "modalita'", "mappe", "personaggi", "partita", "fine mappa", "salvataggi",
                                          "lingua", "nome", "rete", "sala d'attesa" };
    static_assert(sizeof(SCREEN) / sizeof(SCREEN[0]) == 11, "un nome per ogni schermata (SC_* in game_int.h)");
    const int NSCREEN = (int)(sizeof(SCREEN) / sizeof(SCREEN[0]));
    AudioStats as;
    audio_get_stats(&as);
    audio_clear_stats();
    // Lives of each slot ("3 2 - -": - for whoever is not playing).
    char lives[24];
    int nl = 0;
    for (int p = 0; p < MAX_PLAYERS; ++p)
      nl += snprintf(lives + nl, sizeof(lives) - (size_t)nl, gi.lives[p] >= 0 ? "%s%d" : "%s-", p ? " " : "", gi.lives[p] < 99 ? gi.lives[p] : 99);
    char aa_s[12] = "";
    if (A.use_gl && game_aa_wanted(*A.game) > 0) snprintf(aa_s, sizeof(aa_s), " aa%d", game_aa_wanted(*A.game));
    plat_log("stato: f=%u fps %.1f, frame %.1f (max %.1f) | core %.2f = sim %.2f + disegno %.2f | audio %.2f (voci %d, scartate %u) | RA %.2f | tri %d/%d%s | %s, mappa %d (giro %d) zona %d mostri %d giocatori %d/%d vite %s%s",
             A.frame, (double)A.fps, (double)A.frame_avg, (double)A.frame_max, (double)A.e_core, (double)A.e_sim, (double)(A.e_geo + A.e_ras),
             (double)A.e_audio, as.voices_peak, as.dropped, (double)A.e_video, st.tris_drawn, st.tris_in, aa_s,
             SCREEN[gi.screen < NSCREEN && gi.screen >= 0 ? gi.screen : 0], gi.map, gi.loop, gi.zone, gi.enemies, gi.alive, gi.players, lives,
             game_paused(*A.game) ? " (pausa)" : "");
  }
}
