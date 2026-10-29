// Gioco in rete nella stessa Wi-Fi (0.5.4): vedi net.h. Tutto nel thread di retro_run, con prese
// UDP non bloccanti: a ogni fotogramma si legge tutto cio' che e' arrivato e si manda cio' che serve.
//
// Messaggi (interi little-endian; ognuno comincia con "PUPZ", la versione del protocollo e il tipo):
//   DISCOVER  chi cerca -> tutta la rete      "ci sono partite?"
//   OFFER     chi crea -> chi cerca           nome, pianeta e giro, console nella sala, versione,
//                                             impronta delle mappe
//   JOIN      chi cerca -> chi crea           versione, impronta delle mappe, mira
//   WELCOME / REFUSE                          posto in partita / perche' no
//   LOBBY / PING                              sala d'attesa, 5 volte al secondo nei due sensi
//   START     chi crea -> le altre            la partita comincia: numero per mescolare, posti,
//                                             mire, progressi di chi crea (finche' non risponde)
//   INPUT     le altre -> chi crea            i comandi non ancora usati (fino a 16 fotogrammi) e il
//                                             primo fotogramma della partita che le manca
//   BUNDLE    chi crea -> le altre            i fotogrammi della partita (comandi di tutti) che
//                                             mancano a quella console (fino a 16 per messaggio),
//                                             i suoi comandi gia' usati, ogni 30 l'impronta
//   BYE       nei due sensi                   esco (e perche')
#include "net.h"
#include "maps.h"
#include "plat.h"
#include "save.h"
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
const int HIST = 1024;              // fotogrammi tenuti (chi crea) e ricevuti (le altre): 17 secondi
const int IN_RING = 64;             // comandi in arrivo da una console in attesa di essere usati
const int WIN = 16;                 // fotogrammi o comandi al massimo in un messaggio
const int MAX_PKTS = 4;             // messaggi al massimo per console in un fotogramma (quando e' indietro)
const int MAX_STEPS = 4;            // fotogrammi della partita al massimo in un fotogramma (per recuperare)
const int MAX_TARGET = 8;           // scorta al massimo
const int CHECK_EVERY = 30;         // fotogrammi tra un controllo dell'impronta e l'altro
const int PKT = 1400;               // byte al massimo in un messaggio (sotto i 1472 di un pacchetto Ethernet)
const double SILENT_MS = 250.0;     // console che non manda nulla da tanto: comandi a riposo
const double LOBBY_MS = 3000.0;     // sala d'attesa: chi non si sente da tanto non c'e' piu'
const double LOBBY_SEND_MS = 200.0;
const double JOIN_RETRY_MS = 250.0, JOIN_GIVEUP_MS = 3000.0;
const double DISCOVER_MS = 500.0, GAME_EXPIRE_MS = 2500.0;
const double START_ACK_MS = 5000.0; // una console che non conferma l'inizio entro tanto esce
const double CLOSE_MS = 5000.0;     // dopo la fine (titolo) chi crea aspetta tanto chi e' indietro
const double CALM_MS = 6000.0;      // tanto senza restare senza fotogrammi (e sempre 2 di scorta): la scorta cala di 1
const double STAT_MS = 10000.0;     // una riga nel registro ogni tanto
const double IP_MS = 2000.0;        // indirizzo di questa console riletto ogni tanto

// Comandi di un giocatore in un fotogramma, come viaggiano: tasti, levette in 1/32767, START breve.
struct NetPad {
  uint16_t btn;
  int16_t ax[4];
  uint8_t flags;
};
enum { NP_TAP = 1 };

struct Frame {
  uint32_t no;                      // numero del fotogramma (il posto nell'anello e' no % HIST)
  bool ok;
  uint8_t consoles, silent;         // posti con una console; console che non rispondono (solo HUD)
  bool check;
  uint64_t hash;                    // impronta del gioco dopo questo fotogramma (se check)
  NetPad pad[MAX_PLAYERS];
};

// Una console che si e' unita (vista da chi crea la partita).
struct Peer {
  bool used;
  uint32_t id;                      // numero a caso della console
  sockaddr_in addr;
  int player;
  int aim;
  double last_rx, joined_ms;
  // In partita.
  bool acked;                       // ha confermato START
  double start_ms;
  uint32_t want;                    // primo fotogramma che le manca
  NetPad rec[IN_RING];              // comandi arrivati (posto: numero % IN_RING)
  uint32_t rec_no[IN_RING];
  bool rec_ok[IN_RING];
  uint32_t next, top;               // prossimo comando da usare, piu' alto arrivato
  bool any;                         // e' arrivato almeno un comando
  NetPad last;                      // gli ultimi usati
  int gap_t;                        // fotogrammi con un buco (comando perso) prima di saltarlo
  uint32_t echo;                    // ora della console nell'ultimo messaggio (tempo di andata e ritorno)
  double echo_rx;
  bool silent;
  bool done;                        // dopo la fine: e' arrivata in fondo
  // Statistiche.
  int rtt, rtt_max;                 // come li misura la console
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
  int port;                         // chi crea: porta aperta
  int base;                         // prima porta (55410)
  uint32_t session;
  uint32_t my_id;
  uint64_t content;
  char version[16];
  NetView view;
  double now, ip_t, stat_t;
  uint32_t last_done;               // ultimo fotogramma della partita fatto (tracce)
  int budget;                       // fotogrammi della partita che si possono fare in questo fotogramma
  // Chi crea.
  Peer peer[MAX_PLAYERS];           // posto = giocatore (0 = chi crea: non usato)
  double lobby_t;
  NetGame summary;
  NetStart start;
  uint8_t st_open[SAVE_WORLDS], st_giro[SAVE_WORLDS], st_stars[256];   // progressi all'inizio (START)
  int st_nm;
  Frame hist[HIST];                 // chi crea: fotogrammi fatti; le altre: fotogrammi ricevuti
  uint32_t frame;                   // chi crea: prossimo fotogramma da fare
  NetPad host_pad;
  uint8_t consoles;
  uint32_t close_frame;
  double close_t;
  // Le altre.
  double discover_t;
  Found found[NET_MAX_GAMES];
  sockaddr_in host;
  double join_t, join_first;
  double host_rx;                   // ultimo messaggio di chi crea
  bool host_over;                   // chi crea ha detto che la partita e' finita
  int player;
  uint32_t contig;                  // fotogrammi ricevuti di fila (il primo che manca)
  uint32_t next;                    // prossimo da fare
  uint32_t cseq;                    // comandi di questa console fatti finora
  NetPad mine[IN_RING];             // comandi di questa console non ancora usati da chi crea
  uint32_t acked;                   // chi crea ha usato i comandi fino a questo (escluso)
  int target, stall_run, min_avail;
  int avail;                        // fotogrammi di scorta all'inizio di questo fotogramma (riquadro delle prestazioni)
  double calm_t;
  float rtt;
  int rtt_max;
  unsigned stalls, catchup;
  // Prove.
  int auto_mode;                    // 0 niente, 1 crea, 2 unisciti
  int auto_wait;                    // crea: quante altre console aspettare
  bool force;                       // unisciti: anche a una partita che non va (prova dei rifiuti)
  int auto_delay;                   // fotogrammi prima di crea/unisciti (prima si gioca da soli)
  int loss, delay, jitter;          // per cento, ms, ms
  double drop_ms;                   // una console che non risponde da tanto esce
  uint32_t rnd;
};
State S;
bool g_inited;                      // net_init fatta (prima S.fd non vale)

// Messaggi in uscita trattenuti (prove: PUPAZZI_RETE_RITARDO).
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

// --- scrittura e lettura dei messaggi -------------------------------------------------------

struct Wr {
  uint8_t b[PKT];
  int n;
  bool bad;
  void u8(uint32_t v) { if (n + 1 > PKT) { bad = true; return; } b[n++] = (uint8_t)v; }
  void u16(uint32_t v) { u8(v & 255u); u8((v >> 8) & 255u); }
  void u32(uint32_t v) { u16(v & 0xFFFFu); u16(v >> 16); }
  void u64(uint64_t v) { u32((uint32_t)v); u32((uint32_t)(v >> 32)); }
  void str(const char* s, int len) {          // lunghezza fissa, riempita con 0
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

// Messaggi trattenuti il cui momento e' arrivato (all: tutti, prima di chiudere), in ordine di
// partenza: con il ritardo a caso possono superarsi, come nell'aria.
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

// --- prese -------------------------------------------------------------------------------

int open_socket(int port, bool broadcast) {
  const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) return -errno;
  int one = 1;
  if (broadcast) setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
  int sz = 256 * 1024;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
  // Priorita' "voce" nella Wi-Fi (WMM): meno attesa nell'aria per i messaggi piccoli e frequenti.
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

// Indirizzo di questa console (il primo IPv4 acceso che non sia 127.x; la Wi-Fi prima).
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

// --- comandi --------------------------------------------------------------------------------

int16_t quant(float v) {
  v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
  return (int16_t)(v * 32767.0f + (v >= 0.0f ? 0.5f : -0.5f));
}

// Da qui in poi tutte le console usano gli stessi valori (divisione esatta, uguale su ogni processore).
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

// --- chi crea la partita --------------------------------------------------------------------

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
    if (S.peer[p].used && S.peer[p].id == id) {      // JOIN ripetuto (il WELCOME si e' perso)
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
    S.consoles &= (uint8_t)~(1u << pl);          // dal prossimo fotogramma: il suo eroe esce
  }
  p.used = false;
}

// Il prossimo comando della console p per il fotogramma che si sta facendo: i comandi si usano
// nell'ordine in cui li ha letti, uno a fotogramma.
NetPad host_take(Peer& p) {
  if (!p.acked || !p.any) return neutral();
  if (p.silent) {                              // non risponde: a riposo (l'eroe si ferma)
    p.last = neutral();
    return p.last;
  }
  // Troppi in attesa (la console si era fermata, o i messaggi sono arrivati tutti insieme): si
  // tengono solo gli ultimi.
  if (p.top >= p.next && p.top - p.next >= 8u) {
    p.skips += p.top - 2u - p.next;
    p.next = p.top - 2u;
  }
  const int slot = (int)(p.next % IN_RING);
  if (p.rec_ok[slot] && p.rec_no[slot] == p.next) {
    NetPad out = p.rec[slot];
    p.rec_ok[slot] = false;
    p.next++;
    p.gap_t = 0;
    // Ancora piu' di uno in attesa: se ne usano due in questo fotogramma (i tasti premuti nel primo
    // restano premuti, cosi' una pressione breve non si perde).
    const int s2 = (int)(p.next % IN_RING);
    if (p.top >= p.next && p.top - p.next >= 1u && p.rec_ok[s2] && p.rec_no[s2] == p.next) {
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
  // Nessun comando nuovo: si ripete l'ultimo (senza lo START breve). Un buco (comando perso) che
  // dura si salta.
  if (p.top >= p.next && ++p.gap_t > 3) {
    while (p.next <= p.top) {
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
      p.any = true;
      p.next = p.top = no;
    }
    if (no < p.next) continue;                     // gia' usato
    const int s = (int)(no % IN_RING);
    p.rec[s] = pad;
    p.rec_no[s] = no;
    p.rec_ok[s] = true;
    if (no > p.top) p.top = no;
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
  game_net_prepare(&st);
  st.local = 0;
  st.consoles = present_mask();
  for (int p = 1; p < MAX_PLAYERS; ++p) st.aim[p] = S.peer[p].used ? (uint8_t)S.peer[p].aim : (uint8_t)0;
  // I progressi come sono adesso: le altre console giocano con questi (anche se START arriva tardi).
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
  game_net_begin(&st);
}

// Fotogrammi per la console p: da quello che le manca fin dove si e' arrivati (al massimo WIN per
// messaggio, MAX_PKTS messaggi).
void host_send_frames(Peer& p) {
  uint32_t from = p.want;
  const uint32_t end = S.frame;              // escluso
  for (int k = 0; k < MAX_PKTS && from < end; ++k) {
    Wr w;
    w.head(T_BUNDLE);
    w.u32(S.session);
    w.u32(p.next);                           // comandi usati fin qui (esclusi)
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

// La partita e' tornata al titolo (anche le altre console ci arrivano, allo stesso fotogramma): chi
// crea gioca gia' da solo, ma per un po' manda ancora i fotogrammi a chi e' indietro.
void host_closing(void) {
  S.role = R_HOST_CLOSING;
  S.close_frame = S.frame;
  S.close_t = S.now;
  plat_log("rete: la partita in rete torna al titolo (fotogramma %u), aspetto che ci arrivino anche le altre console", S.frame);
  game_net_end(NE_OVER, true);
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

// --- chi si unisce ------------------------------------------------------------------------

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
  // A tutta la rete (e a ogni rete della console), e a questa macchina (prove con piu' copie).
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
  game_net_prepare(&st);
  w.u8(st.aim[0]);
  send_to(S.host, w);
}

// k: posto nella lista dei menu (le partite trovate, in ordine, senza i posti vuoti).
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
  if (S.role == R_JOINING) {                   // il WELCOME si e' perso
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
  if (S.role != R_JOINED && S.role != R_JOINING) return;     // START ripetuto: gia' cominciata
  if (nm != maps_count()) {
    plat_log("rete: la partita ha %d mappe, qui %d: non si puo' giocare", nm, maps_count());
    return;
  }
  S.session = session;
  S.player = player;
  st.local = player;
  // I progressi di chi ha creato la partita, solo in memoria (save_guest_begin).
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
  game_net_begin(&st);
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
    if (f.no < S.contig || f.no >= S.next + (uint32_t)HIST) continue;     // gia' arrivato, o troppo avanti
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
  w.u32(S.contig);                               // il primo fotogramma che manca
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

// Quanti fotogrammi della partita fare adesso: uno, due se la scorta e' piu' di quella voluta,
// fino a MAX_STEPS se si e' molto indietro; nessuno se non c'e' il prossimo. Restare senza fa
// crescere la scorta voluta; 6 secondi con sempre almeno 2 fotogrammi di scorta la fanno calare.
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
      if (S.auto_mode == 2 && S.now - S.join_first > JOIN_GIVEUP_MS) {   // dopo un rifiuto si riprova fra un po'
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
        end_session(NE_OVER);                    // finita per chi l'ha creata, e qui non arriva altro
      } else if (S.now - S.host_rx > S.drop_ms) {
        plat_log("rete: nessun messaggio dalla console che ha creato la partita da %.0f s", (S.now - S.host_rx) / 1000.0);
        end_session(NE_LOST);
      }
      break;
    default: break;
  }
}

// --- arrivi -------------------------------------------------------------------------------

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
          if (reason == NE_OVER) S.host_over = true;     // si arriva in fondo da soli (game_net_over)
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
    if (S.fd < 0) break;                       // la partita e' finita mentre si leggeva
  }
}

// --- fine ----------------------------------------------------------------------------------

void log_client_stats(const char* why) {
  plat_log("rete: %s al fotogramma %u: andata e ritorno %.0f ms (al massimo %d), scorta %d, rimasta senza fotogrammi %u volte, recuperi %u",
           why, S.next, (double)S.rtt, S.rtt_max, S.target, S.stalls, S.catchup);
}

// Fine della partita in rete su questa console (non quella da sola al titolo di chi crea: host_closing).
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
  if (playing) game_net_end(reason, host);
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

// --- interfaccia ---------------------------------------------------------------------------------

// Prove (PUPAZZI_RETE): crea la partita o cerca, subito o dopo PUPAZZI_RETE_DOPO fotogrammi.
static void auto_start(void) {
  if (S.auto_mode == 1) {
    if (save_slot() < 0 && !save_slot_load(0)) save_slot_new(0);
    if (host_open()) game_net_lobby();
  } else if (S.auto_mode == 2) {
    search_start();
    if (S.role == R_SEARCH) game_net_lobby();
  }
}

void net_init(void) {
  net_shutdown();
  memset(&S, 0, sizeof(S));
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
  // Cosa vedono i menu.
  if (!net_session() && S.now - S.ip_t > IP_MS) read_ip();   // per le pagine della rete (fuori dalla partita)
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
  game_net_view(&v);
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
    S.host_pad.flags = 0;                      // uno START breve vale una volta
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
    if (game_net_over()) {
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
  if (game_net_over()) end_session(NE_OVER);
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
  game_net_wait(S.role == R_CLIENT_PLAY ? S.stall_run : 0);
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
