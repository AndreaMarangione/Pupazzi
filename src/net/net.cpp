// LAN game on the same Wi-Fi (0.5.4): see net.h. Everything in the retro_run thread, with
// non-blocking UDP sockets: every frame, everything that arrived is read and what is needed is sent.
//
// Messages (little-endian integers; each starts with "PUPZ", the protocol version and the type):
//   DISCOVER  searcher -> whole network       "any games?"
//   OFFER     host -> searcher                name, planet and round, consoles in the room, version,
//                                             maps hash
//   JOIN      searcher -> host                version, maps hash, aim
//   WELCOME / REFUSE                          slot in the game / why not
//   LOBBY / PING                              waiting room, 5 times per second both ways
//   START     host -> the others              the game starts: number for shuffling, slots, aims,
//                                             the host's progress (until it answers)
//   INPUT     the others -> host              the inputs not used yet (up to 16 frames) and the
//                                             first game frame it is missing
//   BUNDLE    host -> the others              the game frames (everyone's inputs) that are missing
//                                             on that console (up to 16 per message), its inputs
//                                             already used, the hash every 30
//   BYE       both ways                       I am leaving (and why)
#include "net/net.h"
#include "game/sim/maps.h"
#include "platform/plat.h"
#include "game/save.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace {

const int NET_PORT = 55410, NET_PORTS = 10;
const uint8_t PROTO = 1;
enum { T_DISCOVER = 1, T_OFFER, T_JOIN, T_WELCOME, T_REFUSE, T_LOBBY, T_PING, T_BYE, T_START, T_INPUT, T_BUNDLE };
const int HIST = 1024;              // frames kept (host) and received (the others): 17 seconds
const int IN_RING = 64;             // incoming inputs from a console waiting to be used
// Input numbers of a console (they restart from 0 every game): the first one to arrive must be
// below FIRST_MAX (the first ones can get lost), and one more than AHEAD_MAX ahead of those waiting is
// a bad message. The differences are computed in signed 32 bits: they also survive the wraparound.
const uint32_t FIRST_MAX = 1u << 16;
const int32_t AHEAD_MAX = 1 << 20;
const int WIN = 16;                 // max frames or inputs in one message
const int MAX_PKTS = 4;             // max messages per console in one frame (when it is behind)
const int MAX_STEPS = 4;            // max game frames in one frame (to catch up)
const int MAX_TARGET = 8;           // max buffer
const int CHECK_EVERY = 30;         // frames between one hash check and the next
const int PKT = 1400;               // max bytes in one message (below the 1472 of an Ethernet packet)
const double SILENT_MS = 250.0;     // console that has sent nothing for this long: neutral inputs
const double LOBBY_MS = 3000.0;     // waiting room: whoever has not been heard from for this long is gone
const double LOBBY_SEND_MS = 200.0;
const double JOIN_RETRY_MS = 250.0, JOIN_GIVEUP_MS = 3000.0;
const double DISCOVER_MS = 500.0, GAME_EXPIRE_MS = 2500.0;
const double START_ACK_MS = 5000.0; // a console that does not confirm the start within this time leaves
const double CLOSE_MS = 5000.0;     // after the end (title) the host waits this long for those behind
const double CALM_MS = 6000.0;      // this long without running out of frames (always 2 buffered): buffer drops by 1
const double STAT_MS = 10000.0;     // a line in the log every so often
const double IP_MS = 2000.0;        // address of this console read again every so often

// A player's inputs in one frame, as they travel: buttons, sticks in 1/32767, short START.
struct NetPad {
  uint16_t btn;
  int16_t ax[4];
  uint8_t flags;
};
enum { NP_TAP = 1 };

struct Frame {
  uint32_t no;                      // frame number (the slot in the ring is no % HIST)
  bool ok;
  uint8_t consoles, silent;         // slots with a console; consoles that do not answer (HUD only)
  bool check;
  uint64_t hash;                    // game hash after this frame (if check)
  NetPad pad[MAX_PLAYERS];
};

// A console that has joined (as seen by the host).
struct Peer {
  bool used;
  uint32_t id;                      // random number of the console
  sockaddr_in addr;
  int player;
  int aim;
  double last_rx, joined_ms;
  // During the game.
  bool acked;                       // has confirmed START
  double start_ms;
  uint32_t want;                    // first frame it is missing
  NetPad rec[IN_RING];              // inputs received (slot: number % IN_RING)
  uint32_t rec_no[IN_RING];
  bool rec_ok[IN_RING];
  uint32_t next, top;               // next input to use, highest received
  bool any;                         // at least one input has arrived
  NetPad last;                      // the last ones used
  int gap_t;                        // frames with a gap (lost input) before skipping it
  uint32_t echo;                    // console time in the last message (round-trip time)
  double echo_rx;
  bool silent;
  bool done;                        // after the end: it has reached the end
  // Statistics.
  int rtt, rtt_max;                 // as measured by the console
  unsigned repeats, merges, skips, silences, msgs;
};

struct Found {
  bool used;
  sockaddr_in addr;
  uint32_t session;
  NetGame game;
  double seen;
};

enum { R_OFF = 0, R_HOST_LOBBY, R_SEARCH, R_JOINING, R_JOINED, R_HOST_PLAY, R_CLIENT_PLAY, R_HOST_CLOSING };

struct State {
  int role;
  int fd;
  int port;                         // host: open port
  int base;                         // first port (55410)
  uint32_t session;
  uint32_t my_id;
  uint64_t content;
  char version[16];
  NetView view;
  double now, ip_t, stat_t;
  uint32_t last_done;               // last game frame run (traces)
  int budget;                       // game frames that can be run in this frame
  // Host.
  Peer peer[MAX_PLAYERS];           // slot = player (0 = host: not used)
  double lobby_t;
  NetGame summary;
  NetStart start;
  uint8_t st_open[SAVE_WORLDS], st_giro[SAVE_WORLDS], st_stars[256];   // progress at the start (START)
  int st_nm;
  Frame hist[HIST];                 // host: frames run; the others: frames received
  uint32_t frame;                   // host: next frame to run
  NetPad host_pad;
  uint8_t consoles;
  uint32_t close_frame;
  double close_t;
  // The others.
  double discover_t;
  Found found[NET_MAX_GAMES];
  sockaddr_in host;
  double join_t, join_first;
  double host_rx;                   // last message from the host
  bool host_over;                   // the host said the game is over
  int player;
  uint32_t contig;                  // frames received in a row (the first missing one)
  uint32_t next;                    // next one to run
  uint32_t cseq;                    // inputs of this console produced so far
  NetPad mine[IN_RING];             // inputs of this console not yet used by the host
  uint32_t acked;                   // the host has used the inputs up to this one (excluded)
  int target, stall_run, min_avail;
  int avail;                        // buffered frames at the start of this frame (performance overlay)
  double calm_t;
  float rtt;
  int rtt_max;
  unsigned stalls, catchup;
  // Tests.
  int auto_mode;                    // 0 none, 1 crea, 2 unisciti
  int auto_wait;                    // crea: how many other consoles to wait for
  bool force;                       // unisciti: even to an incompatible game (refusal test)
  int auto_delay;                   // frames before crea/unisciti (before that you play alone)
  int loss, delay, jitter;          // percent, ms, ms
  double drop_ms;                   // a console that has not answered for this long leaves
  uint32_t rnd;
};
State S;
Game* g_game;                       // the game (net_init): menus and LAN start/stop go through it
bool g_inited;                      // net_init done (before that S.fd is not valid)

// Outgoing messages held back (tests: PUPAZZI_RETE_RITARDO).
struct Delayed {
  double at;
  sockaddr_in to;
  int n;
  uint8_t b[PKT];
};
const int NDELAY = 256;
Delayed g_delay[NDELAY];
int g_ndelay;

uint32_t rnd_next(void) {
  uint32_t x = S.rnd ? S.rnd : 0x12345678u;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return S.rnd = x;
}

uint32_t fresh_id(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint32_t v = (uint32_t)ts.tv_nsec ^ ((uint32_t)ts.tv_sec * 2654435761u) ^ ((uint32_t)getpid() << 16) ^ rnd_next();
  for (int k = 0; k < 3; ++k) { v ^= v << 13; v ^= v >> 17; v ^= v << 5; }
  return v ? v : 1u;
}

// --- writing and reading messages -----------------------------------------------------------

struct Wr {
  uint8_t b[PKT];
  int n;
  bool bad;
  void u8(uint32_t v) { if (n + 1 > PKT) { bad = true; return; } b[n++] = (uint8_t)v; }
  void u16(uint32_t v) { u8(v & 255u); u8((v >> 8) & 255u); }
  void u32(uint32_t v) { u16(v & 0xFFFFu); u16(v >> 16); }
  void u64(uint64_t v) { u32((uint32_t)v); u32((uint32_t)(v >> 32)); }
  void str(const char* s, int len) {          // fixed length, padded with 0
    const int l = s ? (int)strlen(s) : 0;
    for (int k = 0; k < len; ++k) u8(k < l ? (uint8_t)s[k] : 0u);
  }
  void pad(const NetPad& p) {
    u16(p.btn);
    for (int a = 0; a < 4; ++a) u16((uint16_t)p.ax[a]);
    u8(p.flags);
  }
  void head(int type) {
    n = 0;
    bad = false;
    u8('P'); u8('U'); u8('P'); u8('Z'); u8(PROTO); u8((uint32_t)type);
  }
};

struct Rd {
  const uint8_t* b;
  int n, pos;
  bool bad;
  uint32_t u8(void) { if (pos + 1 > n) { bad = true; return 0; } return b[pos++]; }
  uint32_t u16(void) { const uint32_t lo = u8(); return lo | u8() << 8; }
  uint32_t u32(void) { const uint32_t lo = u16(); return lo | u16() << 16; }
  uint64_t u64(void) { const uint64_t lo = u32(); return lo | (uint64_t)u32() << 32; }
  void str(char* out, int len) {
    for (int k = 0; k < len; ++k) out[k] = (char)u8();
    out[len - 1] = 0;
  }
  NetPad pad(void) {
    NetPad p;
    p.btn = (uint16_t)u16();
    for (int a = 0; a < 4; ++a) p.ax[a] = (int16_t)(uint16_t)u16();
    p.flags = (uint8_t)u8();
    return p;
  }
};

bool same_addr(const sockaddr_in& a, const sockaddr_in& b) { return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port; }

const char* addr_text(const sockaddr_in& a) {
  static char buf[4][32];
  static int k;
  char* out = buf[k++ & 3];
  char ip[INET_ADDRSTRLEN];
  if (!inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip))) snprintf(ip, sizeof(ip), "?");
  snprintf(out, 32, "%s:%u", ip, (unsigned)ntohs(a.sin_port));
  return out;
}

void raw_send(const sockaddr_in& to, const uint8_t* b, int n) {
  if (S.fd >= 0) sendto(S.fd, b, (size_t)n, MSG_DONTWAIT | MSG_NOSIGNAL, (const sockaddr*)&to, sizeof(to));
}

void send_to(const sockaddr_in& to, const Wr& w) {
  if (w.bad || S.fd < 0) return;
  if (S.loss > 0 && (int)(rnd_next() % 100u) < S.loss) return;
  if (S.delay > 0 || S.jitter > 0) {
    if (g_ndelay < NDELAY) {
      Delayed& d = g_delay[g_ndelay++];
      d.at = S.now + (double)S.delay + (S.jitter > 0 ? (double)(rnd_next() % (uint32_t)(S.jitter + 1)) : 0.0);
      d.to = to;
      d.n = w.n;
      memcpy(d.b, w.b, (size_t)w.n);
    }
    return;
  }
  raw_send(to, w.b, w.n);
}

// Held-back messages whose time has come (all: all of them, before closing), in sending order:
// with the random delay they can overtake each other, as over the air.
void flush_delayed(bool all) {
  int k = 0;
  for (int i = 0; i < g_ndelay; ++i) {
    Delayed& d = g_delay[i];
    if (all || d.at <= S.now) {
      raw_send(d.to, d.b, d.n);
    } else {
      if (k != i) g_delay[k] = d;
      k++;
    }
  }
  g_ndelay = k;
}

// --- sockets -----------------------------------------------------------------------------

int open_socket(int port, bool broadcast) {
  const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) return -errno;
  int one = 1;
  if (broadcast) setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
  int sz = 256 * 1024;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
  // "Voice" priority on the Wi-Fi (WMM): less waiting over the air for small, frequent messages.
  int tos = 0xB8;
  setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
  sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((uint16_t)port);
  if (bind(fd, (const sockaddr*)&a, sizeof(a)) < 0) {
    const int e = errno;
    close(fd);
    return -e;
  }
  return fd;
}

void close_socket(void) {
  flush_delayed(true);
  if (S.fd >= 0) close(S.fd);
  S.fd = -1;
}

// Address of this console (the first IPv4 that is up and is not 127.x; Wi-Fi first).
void read_ip(void) {
  S.ip_t = S.now;
  S.view.ip[0] = 0;
  struct ifaddrs* list = 0;
  if (getifaddrs(&list) != 0) return;
  int best = 0;
  for (struct ifaddrs* i = list; i; i = i->ifa_next) {
    if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
    const int score = !strncmp(i->ifa_name, "wlan", 4) ? 2 : 1;
    if (score <= best) continue;
    best = score;
    inet_ntop(AF_INET, &((const sockaddr_in*)i->ifa_addr)->sin_addr, S.view.ip, sizeof(S.view.ip));
  }
  freeifaddrs(list);
}

void error(int e) {
  S.view.error = e;
  S.view.error_seq++;
}

// --- inputs ---------------------------------------------------------------------------------

int16_t quant(float v) {
  v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
  return (int16_t)(v * 32767.0f + (v >= 0.0f ? 0.5f : -0.5f));
}

// From here on all consoles use the same values (exact division, identical on every processor).
Pad unpack(const NetPad& n) {
  Pad p;
  p.btn = n.btn;
  p.lx = (float)n.ax[0] / 32767.0f;
  p.ly = (float)n.ax[1] / 32767.0f;
  p.rx = (float)n.ax[2] / 32767.0f;
  p.ry = (float)n.ax[3] / 32767.0f;
  return p;
}

NetPad neutral(void) {
  NetPad p;
  memset(&p, 0, sizeof(p));
  return p;
}

bool is_host(void) { return S.role == R_HOST_LOBBY || S.role == R_HOST_PLAY || S.role == R_HOST_CLOSING; }

uint8_t present_mask(void) {
  uint8_t m = 1;
  for (int p = 1; p < MAX_PLAYERS; ++p)
    if (S.peer[p].used) m |= (uint8_t)(1u << p);
  return m;
}

int count_peers(void) {
  int n = 0;
  for (int p = 1; p < MAX_PLAYERS; ++p) n += S.peer[p].used;
  return n;
}

const char* reason_text(int reason) {
  switch (reason) {
    case NE_DESYNC: return "impronta diversa";
    case NE_SLOW: return "rimasta troppo indietro";
    case NE_OVER: return "fine della partita";
    case NE_LOST: return "non risponde";
    case NE_HOST_LEFT: return "chi l'ha creata e' uscito";
    default: return "uscita";
  }
}

void send_bye(const sockaddr_in& to, int reason, int times) {
  Wr w;
  w.head(T_BYE);
  w.u32(S.session);
  w.u32(is_host() ? 0u : S.my_id);
  w.u8((uint32_t)reason);
  for (int k = 0; k < times; ++k) send_to(to, w);
}

// --- host -----------------------------------------------------------------------------------

bool host_open(void) {
  close_socket();
  int fd = -1, err = 0;
  for (int k = 0; k < NET_PORTS && fd < 0; ++k) {
    fd = open_socket(S.base + k, false);
    if (fd < 0) err = -fd;
    else S.port = S.base + k;
  }
  if (fd < 0) {
    plat_log("rete: non riesco ad aprire una porta tra %d e %d (%s)", S.base, S.base + NET_PORTS - 1, strerror(err));
    error(NE_SOCKET);
    S.role = R_OFF;
    return false;
  }
  S.fd = fd;
  S.role = R_HOST_LOBBY;
  S.session = fresh_id();
  memset(S.peer, 0, sizeof(S.peer));
  game_net_summary(&S.summary);
  S.lobby_t = 0.0;
  read_ip();
  plat_log("rete: partita creata (porta %d, indirizzo %s, salvataggio %d \"%s\"), aspetto le altre console", S.port,
           S.view.ip[0] ? S.view.ip : "nessuno", S.summary.slot + 1, S.summary.name);
  return true;
}

void offer(const sockaddr_in& to, uint32_t nonce) {
  Wr w;
  w.head(T_OFFER);
  w.u32(nonce);
  w.u32(S.session);
  w.str(S.version, 16);
  w.u64(S.content);
  const int n = 1 + count_peers();
  w.u8(S.role == R_HOST_LOBBY ? (n >= MAX_PLAYERS ? (uint32_t)NG_FULL : (uint32_t)NG_OPEN) : (uint32_t)NG_STARTED);
  w.u8((uint32_t)n);
  w.u8((uint32_t)S.summary.slot);
  w.u8((uint32_t)S.summary.world);
  w.u8((uint32_t)S.summary.giro);
  w.str(S.summary.name, NET_NAME_LEN);
  send_to(to, w);
}

void refuse(const sockaddr_in& to, uint32_t id, int reason) {
  Wr w;
  w.head(T_REFUSE);
  w.u32(id);
  w.u8((uint32_t)reason);
  w.str(S.version, 16);
  for (int k = 0; k < 2; ++k) send_to(to, w);
}

void lobby_msg(const Peer& p) {
  Wr w;
  w.head(T_LOBBY);
  w.u32(S.session);
  w.u32(p.id);
  w.u8((uint32_t)p.player);
  w.u8(present_mask());
  w.u8((uint32_t)S.summary.slot);
  w.u8((uint32_t)S.summary.world);
  w.u8((uint32_t)S.summary.giro);
  w.str(S.summary.name, NET_NAME_LEN);
  send_to(p.addr, w);
}

void welcome(const Peer& p) {
  Wr w;
  w.head(T_WELCOME);
  w.u32(p.id);
  w.u32(S.session);
  w.u8((uint32_t)p.player);
  send_to(p.addr, w);
  lobby_msg(p);
}

void host_join(Rd& r, const sockaddr_in& from) {
  const uint32_t id = r.u32();
  char ver[16];
  r.str(ver, 16);
  const uint64_t content = r.u64();
  const int aim = (int)r.u8();
  if (r.bad) return;
  for (int p = 1; p < MAX_PLAYERS; ++p)
    if (S.peer[p].used && S.peer[p].id == id) {      // repeated JOIN (the WELCOME got lost)
      if (S.role != R_HOST_LOBBY) return;
      S.peer[p].addr = from;
      S.peer[p].last_rx = S.now;
      welcome(S.peer[p]);
      return;
    }
  if (S.role != R_HOST_LOBBY) { refuse(from, id, NE_STARTED); return; }
  if (strcmp(ver, S.version)) {
    plat_log("rete: %s ha la versione %s (qui %s): non puo' entrare", addr_text(from), ver, S.version);
    refuse(from, id, NE_VERSION);
    return;
  }
  if (content != S.content) {
    plat_log("rete: %s ha mappe diverse: non puo' entrare", addr_text(from));
    refuse(from, id, NE_MAPS);
    return;
  }
  int slot = -1;
  for (int p = 1; p < MAX_PLAYERS && slot < 0; ++p)
    if (!S.peer[p].used) slot = p;
  if (slot < 0) { refuse(from, id, NE_FULL); return; }
  Peer& p = S.peer[slot];
  memset(&p, 0, sizeof(p));
  p.used = true;
  p.id = id;
  p.addr = from;
  p.player = slot;
  p.aim = aim;
  p.last_rx = p.joined_ms = S.now;
  plat_log("rete: %s entra come giocatore %d", addr_text(from), slot + 1);
  welcome(p);
}

void host_drop(int pl, int reason, bool tell) {
  Peer& p = S.peer[pl];
  if (!p.used) return;
  if (tell) send_bye(p.addr, reason, 3);
  if (S.role == R_HOST_LOBBY) {
    plat_log("rete: giocatore %d (%s) esce dalla sala d'attesa (%s)", pl + 1, addr_text(p.addr), reason_text(reason));
  } else {
    plat_log("rete: giocatore %d (%s) esce dalla partita al fotogramma %u (%s): andata e ritorno %d ms (al massimo %d), comandi ripetuti %u, uniti %u, saltati %u, silenzi %u",
             pl + 1, addr_text(p.addr), S.frame, reason_text(reason), p.rtt, p.rtt_max, p.repeats, p.merges, p.skips, p.silences);
    S.consoles &= (uint8_t)~(1u << pl);          // from the next frame: its hero leaves
  }
  p.used = false;
}

// The next input of console p for the frame being run: the inputs are used in the order the
// console read them, one per frame.
NetPad host_take(Peer& p) {
  if (!p.acked || !p.any) return neutral();
  if (p.silent) {                              // not answering: neutral (the hero stops)
    p.last = neutral();
    return p.last;
  }
  // Too many waiting (the console had stopped, or the messages all arrived together): only the
  // last ones are kept.
  if ((int32_t)(p.top - p.next) >= 8) {
    p.skips += p.top - 2u - p.next;
    p.next = p.top - 2u;
  }
  const int slot = (int)(p.next % IN_RING);
  if (p.rec_ok[slot] && p.rec_no[slot] == p.next) {
    NetPad out = p.rec[slot];
    p.rec_ok[slot] = false;
    p.next++;
    p.gap_t = 0;
    // Still more than one waiting: two are used in this frame (the buttons pressed in the first one
    // stay pressed, so a short press is not lost).
    const int s2 = (int)(p.next % IN_RING);
    if ((int32_t)(p.top - p.next) >= 1 && p.rec_ok[s2] && p.rec_no[s2] == p.next) {
      const NetPad b = p.rec[s2];
      p.rec_ok[s2] = false;
      p.next++;
      const uint16_t pressed = (uint16_t)(out.btn & ~p.last.btn);
      out.btn = (uint16_t)(b.btn | pressed);
      out.flags |= b.flags;
      for (int a = 0; a < 4; ++a) out.ax[a] = b.ax[a];
      p.merges++;
    }
    p.last = out;
    return out;
  }
  // No new input: the last one is repeated (without the short START). A gap (lost input) that
  // persists is skipped.
  if ((int32_t)(p.top - p.next) >= 0 && ++p.gap_t > 3) {
    for (int k = 0; k <= IN_RING && (int32_t)(p.top - p.next) >= 0; ++k) {
      const int s = (int)(p.next % IN_RING);
      if (p.rec_ok[s] && p.rec_no[s] == p.next) break;
      p.next++;
      p.skips++;
    }
    p.gap_t = 0;
  }
  p.repeats++;
  NetPad out = p.last;
  out.flags = 0;
  return out;
}

void host_input(Rd& r, const sockaddr_in& from) {
  const uint32_t session = r.u32(), id = r.u32();
  if (r.bad || session != S.session) return;
  Peer* pp = 0;
  for (int p = 1; p < MAX_PLAYERS; ++p)
    if (S.peer[p].used && S.peer[p].id == id) pp = &S.peer[p];
  if (!pp) return;
  Peer& p = *pp;
  const uint32_t want = r.u32(), t = r.u32();
  const int rtt = (int)r.u16();
  const uint32_t first = r.u32();
  const int n = (int)r.u8();
  if (r.bad || n > WIN) return;
  p.addr = from;
  p.last_rx = S.now;
  p.msgs++;
  p.silent = false;
  if (!p.acked) {
    p.acked = true;
    plat_log("rete: giocatore %d pronto (%.0f ms dopo l'inizio)", p.player + 1, S.now - p.start_ms);
  }
  if (want > p.want) p.want = want;
  p.echo = t;
  p.echo_rx = S.now;
  if (rtt > 0) {
    p.rtt = rtt;
    if (rtt > p.rtt_max) p.rtt_max = rtt;
  }
  for (int k = 0; k < n; ++k) {
    const NetPad pad = r.pad();
    if (r.bad) return;
    const uint32_t no = first + (uint32_t)k;
    if (!p.any) {
      if (no >= FIRST_MAX) return;                 // a bad message
      p.any = true;
      p.next = p.top = no;
    }
    const int32_t ahead = (int32_t)(no - p.next);
    if (ahead < 0 || ahead >= AHEAD_MAX) continue; // already used, or a bad message
    if (ahead >= IN_RING) {                        // far ahead (the host had stopped): jump there
      p.skips += (uint32_t)(ahead - (IN_RING - 1));
      p.next = no - (uint32_t)(IN_RING - 1);
    }
    const int s = (int)(no % IN_RING);
    p.rec[s] = pad;
    p.rec_no[s] = no;
    p.rec_ok[s] = true;
    if ((int32_t)(no - p.top) > 0) p.top = no;
  }
}

void start_msg(const Peer& p) {
  const NetStart& st = S.start;
  Wr w;
  w.head(T_START);
  w.u32(S.session);
  w.u32(p.id);
  w.u8((uint32_t)p.player);
  w.u32(st.seed);
  w.u8(st.consoles);
  for (int i = 0; i < MAX_PLAYERS; ++i) w.u8(st.aim[i]);
  w.u8((uint32_t)st.map_sel);
  w.u8(SAVE_WORLDS);
  for (int k = 0; k < SAVE_WORLDS; ++k) w.u8(S.st_open[k]);
  for (int k = 0; k < SAVE_WORLDS; ++k) w.u8(S.st_giro[k]);
  w.u8((uint32_t)S.st_nm);
  for (int i = 0; i < S.st_nm; ++i) w.u8(S.st_stars[i]);
  w.str(S.summary.name, NET_NAME_LEN);
  send_to(p.addr, w);
}

void host_start(void) {
  if (S.role != R_HOST_LOBBY || count_peers() == 0) return;
  NetStart& st = S.start;
  memset(&st, 0, sizeof(st));
  game_net_prepare(*g_game, &st);
  st.local = 0;
  st.consoles = present_mask();
  for (int p = 1; p < MAX_PLAYERS; ++p) st.aim[p] = S.peer[p].used ? (uint8_t)S.peer[p].aim : (uint8_t)0;
  // The progress as it is now: the other consoles play with this (even if START arrives late).
  const SaveData* sd = save_data();
  for (int k = 0; k < SAVE_WORLDS; ++k) {
    S.st_open[k] = sd->maps_open[k];
    S.st_giro[k] = sd->giro[k];
  }
  S.st_nm = maps_count() < 256 ? maps_count() : 255;
  for (int i = 0; i < S.st_nm; ++i) S.st_stars[i] = save_stars(maps_id(i));
  S.consoles = st.consoles;
  S.frame = 0;
  S.last_done = 0;
  memset(S.hist, 0, sizeof(S.hist));
  S.host_pad = neutral();
  S.stat_t = S.now;
  S.role = R_HOST_PLAY;
  for (int p = 1; p < MAX_PLAYERS; ++p) {
    Peer& q = S.peer[p];
    if (!q.used) continue;
    q.acked = q.any = q.silent = q.done = false;
    q.start_ms = q.last_rx = S.now;
    q.want = 0;
    memset(q.rec_ok, 0, sizeof(q.rec_ok));
    q.last = neutral();
  }
  plat_log("rete: si comincia con %d console (giocatori 1%s%s%s), numero %08x", 1 + count_peers(), S.peer[1].used ? " 2" : "",
           S.peer[2].used ? " 3" : "", S.peer[3].used ? " 4" : "", (unsigned)st.seed);
  game_net_begin(*g_game, &st);
}

// Frames for console p: from the one it is missing up to where the game has got (at most WIN per
// message, MAX_PKTS messages).
void host_send_frames(Peer& p) {
  uint32_t from = p.want;
  const uint32_t end = S.frame;              // excluded
  for (int k = 0; k < MAX_PKTS && from < end; ++k) {
    Wr w;
    w.head(T_BUNDLE);
    w.u32(S.session);
    w.u32(p.next);                           // inputs used so far (excluded)
    w.u32(p.echo);
    const double hold = p.echo_rx > 0.0 ? S.now - p.echo_rx : 0.0;
    w.u16(hold < 0.0 ? 0u : (hold > 60000.0 ? 60000u : (uint32_t)hold));
    w.u32(from);
    const int cnt_at = w.n;
    w.u8(0);
    int cnt = 0;
    while (from < end && cnt < WIN && w.n < PKT - 64) {
      const Frame& f = S.hist[from % HIST];
      if (!f.ok || f.no != from) break;
      w.u8(f.consoles);
      w.u8(f.silent);
      w.u8(f.check ? 1u : 0u);
      if (f.check) w.u64(f.hash);
      for (int i = 0; i < MAX_PLAYERS; ++i)
        if (f.consoles & (1u << i)) w.pad(f.pad[i]);
      from++;
      cnt++;
    }
    if (cnt == 0) break;
    w.b[cnt_at] = (uint8_t)cnt;
    send_to(p.addr, w);
  }
}

// The game went back to the title (the other consoles get there too, at the same frame): the host
// already plays alone, but for a while it still sends the frames to those behind.
void host_closing(void) {
  S.role = R_HOST_CLOSING;
  S.close_frame = S.frame;
  S.close_t = S.now;
  plat_log("rete: la partita in rete torna al titolo (fotogramma %u), aspetto che ci arrivino anche le altre console", S.frame);
  game_net_end(*g_game, NE_OVER, true);
}

void host_tick(void) {
  for (int p = 1; p < MAX_PLAYERS; ++p) {
    Peer& q = S.peer[p];
    if (!q.used) continue;
    const double quiet = S.now - q.last_rx;
    if (S.role == R_HOST_LOBBY) {
      if (quiet > LOBBY_MS) host_drop(p, NE_LOST, false);
      continue;
    }
    if (S.role == R_HOST_CLOSING) {
      if (!q.done && (q.want >= S.close_frame || quiet > LOBBY_MS)) {
        send_bye(q.addr, NE_OVER, 3);
        q.done = true;
      }
      continue;
    }
    if (!q.acked) {
      if (S.now - q.start_ms > START_ACK_MS) {
        plat_log("rete: giocatore %d non ha confermato l'inizio della partita", p + 1);
        host_drop(p, NE_LOST, true);
      }
      continue;
    }
    if (quiet > S.drop_ms) {
      plat_log("rete: giocatore %d non risponde da %.0f s", p + 1, quiet / 1000.0);
      host_drop(p, NE_LOST, true);
      continue;
    }
    const bool silent = quiet > SILENT_MS;
    if (silent && !q.silent) q.silences++;
    q.silent = silent;
    const uint32_t oldest = S.frame > (uint32_t)HIST ? S.frame - (uint32_t)HIST : 0u;
    if (q.want < oldest) {
      plat_log("rete: giocatore %d e' rimasto troppo indietro (gli manca il fotogramma %u, qui siamo al %u)", p + 1, q.want, S.frame);
      host_drop(p, NE_SLOW, true);
    }
  }
  if (S.role == R_HOST_CLOSING) {
    bool all = true;
    for (int p = 1; p < MAX_PLAYERS; ++p) all &= !S.peer[p].used || S.peer[p].done;
    if (all || S.now - S.close_t > CLOSE_MS) {
      plat_log("rete: partita in rete chiusa%s", all ? "" : " (qualche console non e' arrivata in fondo)");
      memset(S.peer, 0, sizeof(S.peer));
      S.role = R_OFF;
      close_socket();
    }
  }
}

// --- joining console ----------------------------------------------------------------------

bool client_open(void) {
  close_socket();
  const int fd = open_socket(0, true);
  if (fd < 0) {
    plat_log("rete: non riesco ad aprire una porta per cercare le partite (%s)", strerror(-fd));
    error(NE_SOCKET);
    S.role = R_OFF;
    return false;
  }
  S.fd = fd;
  return true;
}

void search_reset(void) {
  memset(S.found, 0, sizeof(S.found));
  S.discover_t = 0.0;
  S.role = R_SEARCH;
}

void search_start(void) {
  if (!client_open()) return;
  S.my_id = fresh_id();
  search_reset();
  read_ip();
  plat_log("rete: cerco le partite (indirizzo %s)", S.view.ip[0] ? S.view.ip : "nessuno");
}

void discover(void) {
  Wr w;
  w.head(T_DISCOVER);
  w.u32(S.my_id);
  // To the whole network (and to each network of the console), and to this machine
  // (tests with several copies).
  in_addr_t dest[12];
  int nd = 0;
  dest[nd++] = htonl(INADDR_BROADCAST);
  dest[nd++] = htonl(INADDR_LOOPBACK);
  struct ifaddrs* list = 0;
  if (getifaddrs(&list) == 0) {
    for (struct ifaddrs* i = list; i && nd < 12; i = i->ifa_next) {
      if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_BROADCAST) || !i->ifa_broadaddr) continue;
      const in_addr_t b = ((const sockaddr_in*)i->ifa_broadaddr)->sin_addr.s_addr;
      bool dup = false;
      for (int k = 0; k < nd; ++k) dup |= dest[k] == b;
      if (!dup) dest[nd++] = b;
    }
    freeifaddrs(list);
  }
  for (int k = 0; k < nd; ++k)
    for (int p = 0; p < NET_PORTS; ++p) {
      sockaddr_in to;
      memset(&to, 0, sizeof(to));
      to.sin_family = AF_INET;
      to.sin_addr.s_addr = dest[k];
      to.sin_port = htons((uint16_t)(S.base + p));
      send_to(to, w);
    }
}

void client_offer(Rd& r, const sockaddr_in& from) {
  const uint32_t nonce = r.u32(), session = r.u32();
  NetGame gm;
  memset(&gm, 0, sizeof(gm));
  r.str(gm.version, 16);
  const uint64_t content = r.u64();
  gm.state = (int)r.u8();
  gm.players = (int)r.u8();
  gm.slot = (int)r.u8();
  gm.world = (int)r.u8();
  gm.giro = (int)r.u8();
  r.str(gm.name, NET_NAME_LEN);
  if (r.bad || nonce != S.my_id) return;
  if (gm.state == NG_OPEN || gm.state == NG_FULL) {
    if (strcmp(gm.version, S.version)) gm.state = NG_VERSION;
    else if (content != S.content) gm.state = NG_MAPS;
  }
  int slot = -1;
  for (int k = 0; k < NET_MAX_GAMES; ++k)
    if (S.found[k].used && S.found[k].session == session) slot = k;
  if (slot < 0)
    for (int k = 0; k < NET_MAX_GAMES && slot < 0; ++k)
      if (!S.found[k].used) slot = k;
  if (slot < 0) return;
  Found& f = S.found[slot];
  if (!f.used) {
    plat_log("rete: trovata la partita \"%s\" su %s (%d console, versione %s)", gm.name, addr_text(from), gm.players, gm.version);
    f.addr = from;
  }
  f.used = true;
  f.session = session;
  f.game = gm;
  f.seen = S.now;
}

void join_send(void) {
  Wr w;
  w.head(T_JOIN);
  w.u32(S.my_id);
  w.str(S.version, 16);
  w.u64(S.content);
  NetStart st;
  memset(&st, 0, sizeof(st));
  game_net_prepare(*g_game, &st);
  w.u8(st.aim[0]);
  send_to(S.host, w);
}

// k: position in the menu list (the games found, in order, without the empty slots).
void join(int k) {
  if (S.role != R_SEARCH || k < 0) return;
  int idx = -1;
  for (int i = 0, n = 0; i < NET_MAX_GAMES && idx < 0; ++i)
    if (S.found[i].used && n++ == k) idx = i;
  if (idx < 0) return;
  const Found& f = S.found[idx];
  S.view.game = f.game;
  if (f.game.state != NG_OPEN && !S.force) {
    static const int ERR[] = { NE_NONE, NE_FULL, NE_STARTED, NE_VERSION, NE_MAPS };
    error(ERR[f.game.state >= 0 && f.game.state <= NG_MAPS ? f.game.state : 0]);
    return;
  }
  S.host = f.addr;
  S.session = f.session;
  S.role = R_JOINING;
  S.join_t = 0.0;
  S.join_first = S.now;
  plat_log("rete: mi unisco alla partita \"%s\" su %s", f.game.name, addr_text(f.addr));
}

void client_lobby(Rd& r, const sockaddr_in& from) {
  const uint32_t session = r.u32(), id = r.u32();
  const int player = (int)r.u8();
  const uint8_t present = (uint8_t)r.u8();
  NetGame gm = S.view.game;
  gm.slot = (int)r.u8();
  gm.world = (int)r.u8();
  gm.giro = (int)r.u8();
  r.str(gm.name, NET_NAME_LEN);
  if (r.bad || id != S.my_id || !same_addr(from, S.host) || player < 1 || player >= MAX_PLAYERS) return;
  if (S.role == R_JOINING) {                   // the WELCOME got lost
    S.role = R_JOINED;
    S.join_t = 0.0;
    plat_log("rete: nella sala d'attesa come giocatore %d", player + 1);
  }
  if (S.role != R_JOINED) return;
  S.session = session;
  S.player = player;
  S.view.present = present;
  S.view.local = player;
  gm.players = 0;
  for (int p = 0; p < MAX_PLAYERS; ++p) gm.players += (present >> p) & 1;
  S.view.game = gm;
  S.host_rx = S.now;
}

void ping(void) {
  Wr w;
  w.head(T_PING);
  w.u32(S.session);
  w.u32(S.my_id);
  send_to(S.host, w);
}

void client_begin(Rd& r) {
  const uint32_t session = r.u32(), id = r.u32();
  const int player = (int)r.u8();
  NetStart st;
  memset(&st, 0, sizeof(st));
  st.seed = r.u32();
  st.consoles = (uint8_t)r.u8();
  for (int i = 0; i < MAX_PLAYERS; ++i) st.aim[i] = (uint8_t)r.u8();
  st.map_sel = (int)r.u8();
  const int nw = (int)r.u8();
  uint8_t open[SAVE_WORLDS], giro[SAVE_WORLDS];
  for (int k = 0; k < SAVE_WORLDS; ++k) open[k] = 0, giro[k] = 1;
  for (int k = 0; k < nw; ++k) { const uint8_t v = (uint8_t)r.u8(); if (k < SAVE_WORLDS) open[k] = v; }
  for (int k = 0; k < nw; ++k) { const uint8_t v = (uint8_t)r.u8(); if (k < SAVE_WORLDS) giro[k] = v; }
  const int nm = (int)r.u8();
  uint8_t stars[256];
  for (int i = 0; i < nm; ++i) stars[i] = (uint8_t)r.u8();
  char name[NET_NAME_LEN];
  r.str(name, NET_NAME_LEN);
  if (r.bad || id != S.my_id || player < 1 || player >= MAX_PLAYERS || !((st.consoles >> player) & 1)) return;
  if (S.role != R_JOINED && S.role != R_JOINING) return;     // repeated START: already started
  if (nm != maps_count()) {
    plat_log("rete: la partita ha %d mappe, qui %d: non si puo' giocare", nm, maps_count());
    return;
  }
  S.session = session;
  S.player = player;
  st.local = player;
  // The host's progress, only in memory (save_guest_begin).
  save_guest_begin();
  SaveData* sd = save_data();
  for (int k = 0; k < SAVE_WORLDS; ++k) {
    sd->maps_open[k] = open[k];
    sd->giro[k] = giro[k] < 1 ? 1 : (giro[k] > SAVE_GIRO_MAX ? SAVE_GIRO_MAX : giro[k]);
  }
  for (int i = 0; i < nm; ++i) save_set_stars(maps_id(i), stars[i]);
  memcpy(sd->name, name, NET_NAME_LEN);
  S.role = R_CLIENT_PLAY;
  S.host_over = false;
  S.contig = S.next = S.cseq = S.acked = 0;
  S.last_done = 0;
  S.target = 2;
  S.budget = 0;
  S.stall_run = 0;
  S.min_avail = 1000;
  S.calm_t = S.stat_t = S.host_rx = S.now;
  S.rtt = 0.0f;
  S.rtt_max = 0;
  S.stalls = S.catchup = 0;
  memset(S.hist, 0, sizeof(S.hist));
  plat_log("rete: la partita comincia, sono il giocatore %d (numero %08x)", player + 1, (unsigned)st.seed);
  game_net_begin(*g_game, &st);
}

void client_bundle(Rd& r) {
  const uint32_t session = r.u32();
  if (r.bad || session != S.session) return;
  const uint32_t acked = r.u32(), echo = r.u32();
  const uint32_t hold = r.u16();
  const uint32_t first = r.u32();
  const int cnt = (int)r.u8();
  if (r.bad) return;
  S.host_rx = S.now;
  if (acked > S.acked && acked <= S.cseq) S.acked = acked;
  if (echo != 0u) {
    const double rtt = (double)((uint32_t)S.now - echo) - (double)hold;
    if (rtt >= 0.0 && rtt < 10000.0) {
      S.rtt = S.rtt <= 0.0f ? (float)rtt : S.rtt + ((float)rtt - S.rtt) * 0.1f;
      if ((int)rtt > S.rtt_max) S.rtt_max = (int)rtt;
    }
  }
  for (int k = 0; k < cnt; ++k) {
    Frame f;
    memset(&f, 0, sizeof(f));
    f.no = first + (uint32_t)k;
    f.consoles = (uint8_t)r.u8();
    f.silent = (uint8_t)r.u8();
    f.check = (r.u8() & 1u) != 0;
    if (f.check) f.hash = r.u64();
    for (int i = 0; i < MAX_PLAYERS; ++i) f.pad[i] = (f.consoles >> i) & 1 ? r.pad() : neutral();
    if (r.bad) return;
    f.ok = true;
    if (f.no < S.contig || f.no >= S.next + (uint32_t)HIST) continue;     // already arrived, or too far ahead
    Frame& slot = S.hist[f.no % HIST];
    if (slot.ok && slot.no == f.no) continue;
    slot = f;
  }
  while (S.contig < S.next + (uint32_t)HIST) {
    const Frame& f = S.hist[S.contig % HIST];
    if (!f.ok || f.no != S.contig) break;
    S.contig++;
  }
}

void client_send_input(void) {
  Wr w;
  w.head(T_INPUT);
  w.u32(S.session);
  w.u32(S.my_id);
  w.u32(S.contig);                               // the first missing frame
  const uint32_t t = (uint32_t)S.now;
  w.u32(t ? t : 1u);
  w.u16(S.rtt > 0.0f ? (uint32_t)(S.rtt + 0.5f) : 0u);
  uint32_t from = S.acked;
  if (S.cseq - from > (uint32_t)WIN) from = S.cseq - (uint32_t)WIN;
  w.u32(from);
  w.u8(S.cseq - from);
  for (uint32_t no = from; no < S.cseq; ++no) w.pad(S.mine[no % IN_RING]);
  send_to(S.host, w);
}

// How many game frames to run now: one, two if the buffer is more than the target, up to
// MAX_STEPS if far behind; none if the next one is not there. Running out makes the target
// buffer grow; 6 seconds with always at least 2 buffered frames make it shrink.
void client_budget(void) {
  const int avail = (int)(S.contig - S.next);
  S.avail = avail;
  if (avail <= 0) {
    S.budget = 0;
    if (S.next > 0 && !S.host_over) {
      if (S.stall_run == 0) S.stalls++;
      S.stall_run++;
      S.min_avail = 0;
    }
    return;
  }
  if (S.stall_run > 0) {
    if (S.target < MAX_TARGET) S.target++;
    S.calm_t = S.now;
    S.stall_run = 0;
  }
  if (avail < S.min_avail) S.min_avail = avail;
  S.budget = 1;
  if (avail > S.target + 1) S.budget = 2;
  if (avail > S.target + 8) {
    S.budget = avail - S.target < MAX_STEPS ? avail - S.target : MAX_STEPS;
    S.catchup++;
  }
  if (S.now - S.calm_t > CALM_MS) {
    if (S.min_avail >= 2 && S.target > 1) S.target--;
    S.calm_t = S.now;
    S.min_avail = 1000;
  }
}

void end_session(int reason);

void client_tick(void) {
  switch (S.role) {
    case R_SEARCH: {
      if (S.now - S.discover_t >= DISCOVER_MS) {
        S.discover_t = S.now;
        discover();
      }
      for (int k = 0; k < NET_MAX_GAMES; ++k)
        if (S.found[k].used && S.now - S.found[k].seen > GAME_EXPIRE_MS) S.found[k].used = false;
      if (S.auto_mode == 2 && S.now - S.join_first > JOIN_GIVEUP_MS) {   // after a refusal, retry in a while
        for (int k = 0, n = 0; k < NET_MAX_GAMES && S.role == R_SEARCH; ++k) {
          if (!S.found[k].used) continue;
          if (S.found[k].game.state == NG_OPEN || S.force) join(n);
          n++;
        }
      }
      break;
    }
    case R_JOINING:
      if (S.now - S.join_first > JOIN_GIVEUP_MS) {
        plat_log("rete: nessuna risposta dalla partita");
        error(NE_NO_ANSWER);
        search_reset();
        break;
      }
      if (S.now - S.join_t >= JOIN_RETRY_MS) {
        S.join_t = S.now;
        join_send();
      }
      break;
    case R_JOINED:
      if (S.now - S.host_rx > LOBBY_MS) {
        plat_log("rete: la partita non risponde piu'");
        error(NE_NO_ANSWER);
        search_reset();
        break;
      }
      if (S.now - S.join_t >= LOBBY_SEND_MS) {
        S.join_t = S.now;
        ping();
      }
      break;
    case R_CLIENT_PLAY:
      if (S.host_over && S.contig == S.next && S.now - S.host_rx > 2000.0) {
        end_session(NE_OVER);                    // over for the host, and nothing else arrives here
      } else if (S.now - S.host_rx > S.drop_ms) {
        plat_log("rete: nessun messaggio dalla console che ha creato la partita da %.0f s", (S.now - S.host_rx) / 1000.0);
        end_session(NE_LOST);
      }
      break;
    default: break;
  }
}

// --- incoming -----------------------------------------------------------------------------

void handle(const uint8_t* b, int n, const sockaddr_in& from) {
  if (n < 6 || b[0] != 'P' || b[1] != 'U' || b[2] != 'P' || b[3] != 'Z' || b[4] != PROTO) return;
  Rd r = { b, n, 6, false };
  switch (b[5]) {
    case T_DISCOVER:
      if (is_host()) {
        const uint32_t nonce = r.u32();
        if (!r.bad) offer(from, nonce);
      }
      break;
    case T_OFFER:
      if (S.role == R_SEARCH) client_offer(r, from);
      break;
    case T_JOIN:
      if (S.role == R_HOST_LOBBY || S.role == R_HOST_PLAY) host_join(r, from);
      break;
    case T_WELCOME: {
      const uint32_t id = r.u32(), session = r.u32();
      const int player = (int)r.u8();
      if (r.bad || S.role != R_JOINING || id != S.my_id || !same_addr(from, S.host) || player < 1 || player >= MAX_PLAYERS) break;
      S.role = R_JOINED;
      S.session = session;
      S.player = player;
      S.host_rx = S.now;
      S.join_t = 0.0;
      plat_log("rete: nella sala d'attesa come giocatore %d", player + 1);
      break;
    }
    case T_REFUSE: {
      const uint32_t id = r.u32();
      const int reason = (int)r.u8();
      char ver[16];
      r.str(ver, 16);
      if (r.bad || S.role != R_JOINING || id != S.my_id || !same_addr(from, S.host)) break;
      memcpy(S.view.game.version, ver, sizeof(ver));
      plat_log("rete: la partita non mi fa entrare (%d)", reason);
      error(reason);
      search_reset();
      break;
    }
    case T_LOBBY:
      if (S.role == R_JOINING || S.role == R_JOINED) client_lobby(r, from);
      break;
    case T_PING: {
      const uint32_t session = r.u32(), id = r.u32();
      if (r.bad || S.role != R_HOST_LOBBY || session != S.session) break;
      for (int p = 1; p < MAX_PLAYERS; ++p)
        if (S.peer[p].used && S.peer[p].id == id) {
          S.peer[p].last_rx = S.now;
          S.peer[p].addr = from;
        }
      break;
    }
    case T_BYE: {
      const uint32_t session = r.u32(), id = r.u32();
      const int reason = (int)r.u8();
      if (r.bad || session != S.session) break;
      if (is_host()) {
        for (int p = 1; p < MAX_PLAYERS; ++p) {
          Peer& q = S.peer[p];
          if (!q.used || q.id != id) continue;
          if (S.role == R_HOST_CLOSING) q.done = true;
          else {
            if (reason == NE_DESYNC) plat_log("rete: ERRORE: il giocatore %d non fa piu' la stessa partita (impronta diversa)", p + 1);
            host_drop(p, reason, false);
          }
        }
      } else if (same_addr(from, S.host)) {
        if (S.role == R_JOINING || S.role == R_JOINED) {
          plat_log("rete: la partita e' stata chiusa da chi l'ha creata");
          error(NE_HOST_LEFT);
          search_reset();
        } else if (S.role == R_CLIENT_PLAY) {
          if (reason == NE_OVER) S.host_over = true;     // this console reaches the end by itself (game_net_over)
          else end_session(reason == NE_SLOW || reason == NE_DESYNC ? reason : (reason == NE_LOST ? NE_KICKED : NE_HOST_LEFT));
        }
      }
      break;
    }
    case T_START:
      if ((S.role == R_JOINED || S.role == R_JOINING) && same_addr(from, S.host)) client_begin(r);
      break;
    case T_INPUT:
      if (S.role == R_HOST_PLAY || S.role == R_HOST_CLOSING) host_input(r, from);
      break;
    case T_BUNDLE:
      if (S.role == R_CLIENT_PLAY && same_addr(from, S.host)) client_bundle(r);
      break;
    default: break;
  }
}

void receive(void) {
  if (S.fd < 0) return;
  for (int k = 0; k < 512; ++k) {
    uint8_t b[2048];
    sockaddr_in from;
    socklen_t fl = sizeof(from);
    memset(&from, 0, sizeof(from));
    const ssize_t got = recvfrom(S.fd, b, sizeof(b), MSG_DONTWAIT, (sockaddr*)&from, &fl);
    if (got < 0) {
      if (errno == EINTR) continue;
      break;
    }
    handle(b, (int)got, from);
    if (S.fd < 0) break;                       // the game ended while reading
  }
}

// --- end -----------------------------------------------------------------------------------

void log_client_stats(const char* why) {
  plat_log("rete: %s al fotogramma %u: andata e ritorno %.0f ms (al massimo %d), scorta %d, rimasta senza fotogrammi %u volte, recuperi %u",
           why, S.next, (double)S.rtt, S.rtt_max, S.target, S.stalls, S.catchup);
}

// End of the LAN game on this console (not the host going back to the title alone: host_closing).
void end_session(int reason) {
  const bool host = S.role == R_HOST_PLAY || S.role == R_HOST_CLOSING;
  const bool playing = S.role == R_CLIENT_PLAY || S.role == R_HOST_PLAY;
  if (S.role == R_CLIENT_PLAY) {
    log_client_stats(reason == NE_OVER ? "partita finita" : "esco dalla partita");
    if (reason != NE_HOST_LEFT && reason != NE_KICKED) send_bye(S.host, reason == NE_LOST ? NE_CLOSED : reason, 3);
  } else if (host) {
    for (int p = 1; p < MAX_PLAYERS; ++p)
      if (S.peer[p].used) host_drop(p, reason == NE_ALONE ? NE_CLOSED : NE_HOST_LEFT, true);
  }
  S.role = R_OFF;
  close_socket();
  if (playing) game_net_end(*g_game, reason, host);
}

void parse_env(void) {
  const char* v = getenv("PUPAZZI_RETE_PORTA");
  S.base = v && *v ? atoi(v) : NET_PORT;
  if (S.base < 1024 || S.base > 65535 - NET_PORTS) S.base = NET_PORT;
  v = getenv("PUPAZZI_RETE_VERSIONE");
  snprintf(S.version, sizeof(S.version), "%s", v && *v ? v : PUPAZZI_VERSION);
  v = getenv("PUPAZZI_RETE_ATTESA");
  S.drop_ms = v && *v ? atof(v) : 10000.0;
  if (S.drop_ms < 500.0) S.drop_ms = 500.0;
  v = getenv("PUPAZZI_RETE_PERDITA");
  S.loss = v && *v ? atoi(v) : 0;
  if (S.loss < 0) S.loss = 0;
  if (S.loss > 90) S.loss = 90;
  v = getenv("PUPAZZI_RETE_RITARDO");
  S.delay = S.jitter = 0;
  if (v && *v) {
    S.delay = atoi(v);
    const char* plus = strchr(v, '+');
    if (plus) S.jitter = atoi(plus + 1);
    if (S.delay < 0) S.delay = 0;
    if (S.jitter < 0) S.jitter = 0;
  }
  v = getenv("PUPAZZI_RETE");
  S.auto_mode = 0;
  S.auto_wait = 1;
  if (v && !strncmp(v, "crea", 4)) {
    S.auto_mode = 1;
    if (v[4] == ':') S.auto_wait = atoi(v + 5);
    if (S.auto_wait < 1) S.auto_wait = 1;
    if (S.auto_wait > MAX_PLAYERS - 1) S.auto_wait = MAX_PLAYERS - 1;
  } else if (v && !strncmp(v, "unisciti", 8)) {
    S.auto_mode = 2;
    S.force = v[8] == '!';
  }
  v = getenv("PUPAZZI_RETE_DOPO");
  S.auto_delay = S.auto_mode && v && *v ? atoi(v) : 0;
  if (S.auto_delay < 0) S.auto_delay = 0;
  if (S.loss || S.delay || S.jitter || S.base != NET_PORT || strcmp(S.version, PUPAZZI_VERSION) || S.drop_ms != 10000.0)
    plat_log("rete (prove): porta %d, versione %s, messaggi persi %d%%, ritardo %d+%d ms, attesa %.0f ms", S.base, S.version, S.loss, S.delay,
             S.jitter, S.drop_ms);
}

}  // namespace

// --- interface -----------------------------------------------------------------------------------

// Tests (PUPAZZI_RETE): create the game or search, right away or after PUPAZZI_RETE_DOPO frames.
static void auto_start(void) {
  if (S.auto_mode == 1) {
    if (save_slot() < 0 && !save_slot_load(0)) save_slot_new(0);
    if (host_open()) game_net_lobby(*g_game);
  } else if (S.auto_mode == 2) {
    search_start();
    if (S.role == R_SEARCH) game_net_lobby(*g_game);
  }
}

void net_init(Game& game) {
  net_shutdown();
  memset(&S, 0, sizeof(S));
  g_game = &game;
  S.fd = -1;
  g_inited = true;
  S.now = plat_now_ms();
  S.rnd = (uint32_t)getpid() * 2654435761u ^ (uint32_t)time(NULL);
  S.rnd = fresh_id();
  parse_env();
  S.content = maps_content_hash();
  if (S.auto_delay == 0) auto_start();
}

void net_shutdown(void) {
  if (!g_inited) return;
  S.now = plat_now_ms();
  switch (S.role) {
    case R_CLIENT_PLAY:
    case R_HOST_PLAY:
      end_session(NE_CLOSED);
      break;
    case R_HOST_LOBBY:
    case R_HOST_CLOSING:
      for (int p = 1; p < MAX_PLAYERS; ++p)
        if (S.peer[p].used && !S.peer[p].done) send_bye(S.peer[p].addr, S.role == R_HOST_LOBBY ? NE_HOST_LEFT : NE_OVER, 3);
      break;
    case R_JOINING:
    case R_JOINED:
      send_bye(S.host, NE_CLOSED, 3);
      break;
    default: break;
  }
  memset(S.peer, 0, sizeof(S.peer));
  S.role = R_OFF;
  close_socket();
  g_ndelay = 0;
}

void net_frame_begin(void) {
  if (!g_inited) return;
  S.now = plat_now_ms();
  if (S.auto_delay > 0 && --S.auto_delay == 0) auto_start();
  flush_delayed(false);
  receive();
  if (is_host()) host_tick();
  else client_tick();
  if (S.auto_mode == 1 && S.role == R_HOST_LOBBY && count_peers() >= S.auto_wait) host_start();
  S.budget = 0;
  if (S.role == R_HOST_PLAY) S.budget = 1;
  else if (S.role == R_CLIENT_PLAY) client_budget();
  // What the menus see.
  if (!net_session() && S.now - S.ip_t > IP_MS) read_ip();   // for the network pages (outside the game)
  NetView& v = S.view;
  v.mode = S.role == R_HOST_LOBBY ? NV_HOST
         : (S.role == R_SEARCH ? NV_SEARCH : (S.role == R_JOINING ? NV_JOINING : (S.role == R_JOINED ? NV_JOINED : NV_OFF)));
  if (S.role == R_HOST_LOBBY) {
    v.present = present_mask();
    v.local = 0;
    v.game = S.summary;
    v.game.players = 1 + count_peers();
  }
  v.ngames = 0;
  if (S.role == R_SEARCH)
    for (int k = 0; k < NET_MAX_GAMES; ++k)
      if (S.found[k].used) v.games[v.ngames++] = S.found[k].game;
  game_net_view(*g_game, &v);
}

bool net_session(void) { return g_inited && (S.role == R_HOST_PLAY || S.role == R_CLIENT_PLAY); }

void net_local_pad(const Pad& pad, bool tap) {
  NetPad n;
  n.btn = pad.btn;
  n.ax[0] = quant(pad.lx);
  n.ax[1] = quant(pad.ly);
  n.ax[2] = quant(pad.rx);
  n.ax[3] = quant(pad.ry);
  n.flags = tap ? (uint8_t)NP_TAP : (uint8_t)0;
  if (S.role == R_HOST_PLAY) {
    S.host_pad = n;
  } else if (S.role == R_CLIENT_PLAY) {
    S.mine[S.cseq % IN_RING] = n;
    S.cseq++;
  }
}

bool net_step(Pad pads[MAX_PLAYERS], bool taps[MAX_PLAYERS], uint8_t* consoles, uint8_t* silent) {
  if (S.budget <= 0) return false;
  const Frame* f = 0;
  if (S.role == R_HOST_PLAY) {
    Frame& h = S.hist[S.frame % HIST];
    memset(&h, 0, sizeof(h));
    h.no = S.frame;
    h.ok = true;
    h.consoles = S.consoles;
    h.pad[0] = S.host_pad;
    S.host_pad.flags = 0;                      // a short START counts once
    for (int p = 1; p < MAX_PLAYERS; ++p) {
      if (!((h.consoles >> p) & 1)) continue;
      Peer& q = S.peer[p];
      h.pad[p] = q.used ? host_take(q) : neutral();
      if (q.used && (q.silent || !q.acked)) h.silent |= (uint8_t)(1u << p);
    }
    f = &h;
  } else if (S.role == R_CLIENT_PLAY) {
    if (S.next >= S.contig) return false;
    f = &S.hist[S.next % HIST];
    S.consoles = f->consoles;
  } else {
    return false;
  }
  S.budget--;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    pads[i] = unpack(f->pad[i]);
    taps[i] = (f->pad[i].flags & NP_TAP) != 0;
  }
  *consoles = f->consoles;
  *silent = f->silent;
  return true;
}

void net_step_done(uint64_t hash) {
  if (S.role == R_HOST_PLAY) {
    Frame& h = S.hist[S.frame % HIST];
    if (S.frame % CHECK_EVERY == 0) {
      h.check = true;
      h.hash = hash;
    }
    S.last_done = S.frame;
    S.frame++;
    if (game_net_over(*g_game)) {
      host_closing();
    } else if (count_peers() == 0) {
      plat_log("rete: le altre console sono uscite, si continua da soli");
      end_session(NE_ALONE);
    }
    return;
  }
  if (S.role != R_CLIENT_PLAY) return;
  const Frame& f = S.hist[S.next % HIST];
  S.last_done = S.next;
  S.next++;
  if (f.check && f.hash != hash) {
    plat_log("rete: ERRORE: impronta diversa al fotogramma %u: qui %016llx, sulla console che ha creato la partita %016llx", f.no,
             (unsigned long long)hash, (unsigned long long)f.hash);
    end_session(NE_DESYNC);
    return;
  }
  if (game_net_over(*g_game)) end_session(NE_OVER);
}

void net_frame_end(void) {
  if (!g_inited) return;
  S.now = plat_now_ms();
  switch (S.role) {
    case R_HOST_LOBBY:
      if (S.now - S.lobby_t >= LOBBY_SEND_MS) {
        S.lobby_t = S.now;
        for (int p = 1; p < MAX_PLAYERS; ++p)
          if (S.peer[p].used) lobby_msg(S.peer[p]);
      }
      break;
    case R_HOST_PLAY:
    case R_HOST_CLOSING:
      for (int p = 1; p < MAX_PLAYERS; ++p) {
        Peer& q = S.peer[p];
        if (!q.used || q.done) continue;
        if (!q.acked) {
          if (S.role == R_HOST_PLAY) start_msg(q);
        } else {
          host_send_frames(q);
        }
      }
      break;
    case R_CLIENT_PLAY:
      client_send_input();
      break;
    default: break;
  }
  flush_delayed(false);
  game_net_wait(*g_game, S.role == R_CLIENT_PLAY ? S.stall_run : 0);
  if ((S.role == R_HOST_PLAY || S.role == R_CLIENT_PLAY) && S.now - S.stat_t >= STAT_MS) {
    S.stat_t = S.now;
    if (S.role == R_CLIENT_PLAY) {
      log_client_stats("in partita");
    } else {
      for (int p = 1; p < MAX_PLAYERS; ++p) {
        const Peer& q = S.peer[p];
        if (q.used)
          plat_log("rete: giocatore %d al fotogramma %u (qui %u): andata e ritorno %d ms (al massimo %d), comandi ripetuti %u, uniti %u, saltati %u, silenzi %u",
                   p + 1, q.want, S.frame, q.rtt, q.rtt_max, q.repeats, q.merges, q.skips, q.silences);
      }
    }
  }
}

void net_request(int req, int arg) {
  if (!g_inited) return;
  S.now = plat_now_ms();
  switch (req) {
    case NR_HOST:
      net_shutdown();
      host_open();
      break;
    case NR_SEARCH:
      net_shutdown();
      search_start();
      break;
    case NR_JOIN:
      join(arg);
      break;
    case NR_LEAVE:
      if (S.role == R_JOINING || S.role == R_JOINED) {
        send_bye(S.host, NE_CLOSED, 3);
        plat_log("rete: esco dalla sala d'attesa");
        search_reset();
      } else if (S.role != R_HOST_PLAY && S.role != R_CLIENT_PLAY) {
        if (S.role == R_HOST_LOBBY) plat_log("rete: partita annullata");
        net_shutdown();
      }
      break;
    case NR_START:
      host_start();
      break;
    default: break;
  }
}

uint32_t net_frame_no(void) { return S.last_done; }

void net_stats(NetStats* s) {
  memset(s, 0, sizeof(*s));
  s->on = net_session();
  s->host = S.role == R_HOST_PLAY;
  if (S.role == R_HOST_PLAY) {
    for (int p = 1; p < MAX_PLAYERS; ++p)
      if (S.peer[p].used) {
        s->players++;
        if (S.peer[p].rtt > s->rtt_ms) s->rtt_ms = S.peer[p].rtt;
      }
    s->players++;
  } else if (S.role == R_CLIENT_PLAY) {
    s->rtt_ms = (int)(S.rtt + 0.5f);
    s->buffer = S.avail;
    s->target = S.target;
    s->stalls = S.stalls;
    for (int p = 0; p < MAX_PLAYERS; ++p) s->players += (S.consoles >> p) & 1;
  }
}
