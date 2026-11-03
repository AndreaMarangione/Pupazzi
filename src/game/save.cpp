// Saves: tolerant reading and atomic writing of pupazzi.sav (settings) and partitaN.sav (games);
// format and usage in save.h. No allocations: everything in static memory, the biggest possible
// file fits in OUT_LEN.
#include "game/save.h"
#include "engine/lang.h"
#include "platform/plat.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static_assert(NLANGS == SAVE_LANGS, "save.h e lang.h devono avere le stesse lingue");

namespace {

const char* const SETTINGS_NAME = "pupazzi.sav";
const char* const SETTINGS_HEADER =
    "# Pupazzi: impostazioni di questa console. Le partite sono in partita1.sav ... partita4.sav\n"
    "# (si duplicano e si eliminano dal gioco: prima schermata, Gestisci salvataggi).\n";
const char* const GAME_HEADER =
    "# Pupazzi: una partita. Cancella il file (o eliminala dal gioco) per liberare il posto.\n"
    "# giro.PIANETA: giro del pianeta; mappe.PIANETA: sue mappe aperte, in ordine; tempo: secondi di gioco\n"
    "# stelle.MAPPA: stelle del giro in corso, somma di 1 (finita), 2 (esploratore salvato), 4 (senza ricominciare la mappa)\n";
const int LINE_LEN = 256;           // longer lines (newline included): invalid
const int MAX_EXTRA = 64;           // unknown keys kept
const int MAX_READ = 256 * 1024;    // of a bigger file (not a save) only the beginning is read
const int OUT_LEN = 24 * 1024;      // header, entries, planets, 64 maps and 64 unknown lines fit in 19 KB
const uint32_t MAX_SECONDS = 999999999u;
// Planets in travel order (WORLD_* in level.h) and languages in the order of LANG_* (lang.h).
const char* const WORLD_ID[SAVE_WORLDS] = { "terra", "luna", "marte", "venere", "mercurio", "giove", "saturno", "urano", "nettuno" };
const char* const LANG_ID[SAVE_LANGS] = { "it", "en", "es", "pt", "zh", "ja", "hi" };

enum Kind { K_SETTINGS, K_GAME };

// A file with its unknown lines and what is already on disk.
struct File {
  Kind kind;
  char path[300], tmp[310], bad[320];
  char extra[MAX_EXTRA][LINE_LEN];  // "key=value" that this version does not know, in file order
  int nextra;
  bool backup;                      // unreadable file: on the first write it becomes NAME.illeggibile
  bool failed;                      // the last write failed (already written to the log)
  char last[OUT_LEN];               // content already on disk (written or read): not rewritten if unchanged
  int last_n;
  bool progress;                    // (settings) there were game keys: a 0.5.2 file
};

bool g_init;                        // g_data has the default values even before save_open
SaveData g_data;
char g_dir[256];
char g_status[200], g_slot_status[200];
File g_set, g_game;                 // settings and active game
File g_scratch;                     // trial reads (save_slot_peek) and copies
int g_active = -1;
bool g_marker;                      // salvataggi=1: nothing (more) to take from the 0.5.2 file; 0 if that failed
bool g_full_logged;                 // too many maps: logged only once
// LAN game saved on a console that joins (0.5.6): the slot as it was, when it held the same game
// (what it has more is kept at every write), and the planet of a map.
SaveData g_base;
bool g_guest_slot, g_base_on;
int (*g_world_of)(const char*);
char g_out[OUT_LEN];
char g_slot_paths[SAVE_SLOTS][300];

void defaults_settings(SaveData* d) {
  for (int i = 0; i < SAVE_PLAYERS; ++i) d->aim[i] = 1;
  d->antialias = 1;
  d->music_vol = 6;   // 0.5: new, lighter music, below the sound effects
  d->sfx_vol = 8;
  d->lang = 0;
  d->cam_dist = d->cam_pitch = 0;
  d->res = 640;
  d->detail = 1;
  d->margin = 0;
}

// Width of the picture: the nearest of 320, 640, 960, 1280.
int snap_res(long w) { return w < 480 ? 320 : (w < 800 ? 640 : (w < 1120 ? 960 : 1280)); }

void defaults_game(SaveData* d) {
  for (int w = 0; w < SAVE_WORLDS; ++w) {
    d->maps_open[w] = 0;
    d->giro[w] = 1;
  }
  d->name[0] = 0;
  d->id = 0;
  d->maps_open[0] = 1;                       // the first map of Earth
  d->nmaps = 0;
  memset(d->maps, 0, sizeof(d->maps));
  d->seconds = 0;
}

void defaults(SaveData* d) {
  memset(d, 0, sizeof(*d));
  defaults_settings(d);
  defaults_game(d);
}

SaveData& data(void) {
  if (!g_init) {
    defaults(&g_data);
    g_init = true;
  }
  return g_data;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Integer with an optional sign and nothing else around it; huge values stop at +-1000000000.
bool parse_int(const char* s, long* out) {
  long sign = 1, v = 0;
  int n = 0;
  if (*s == '-') sign = -1;
  if (*s == '-' || *s == '+') s++;
  for (; *s >= '0' && *s <= '9'; ++s, ++n)
    if (v < 1000000000L) v = v * 10 + (*s - '0');
  if (n == 0 || *s) return false;
  *out = sign * (v > 1000000000L ? 1000000000L : v);
  return true;
}

bool id_char(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }

bool valid_id(const char* s) {
  if (!s || !*s) return false;
  for (int n = 0; s[n]; ++n)
    if (n >= SAVE_ID_LEN - 1 || !id_char(s[n])) return false;
  return true;
}

// Planet of the key "prefix.planet" (-1 if it is not one of the planets).
int world_key(const char* k, const char* prefix) {
  const size_t n = strlen(prefix);
  if (strncmp(k, prefix, n)) return -1;
  for (int w = 0; w < SAVE_WORLDS; ++w)
    if (!strcmp(k + n, WORLD_ID[w])) return w;
  return -1;
}

bool valid_key(const char* s) {
  if (!*s) return false;
  for (; *s; ++s)
    if (!id_char(*s) && *s != '.') return false;
  return true;
}

// Name of a game into dst (SAVE_NAME_LEN bytes): surrounding spaces removed, lowercase to uppercase.
// false (dst empty) if it is too long or has characters that are not allowed.
bool clean_name(char* dst, const char* src) {
  char out[SAVE_NAME_LEN];
  while (*src == ' ') src++;
  int len = (int)strlen(src);
  while (len > 0 && src[len - 1] == ' ') len--;
  if (len > SAVE_NAME_LEN - 1) {
    dst[0] = 0;
    return false;
  }
  for (int i = 0; i < len; ++i) {
    const char c = src[i] >= 'a' && src[i] <= 'z' ? (char)(src[i] - 'a' + 'A') : src[i];
    if (!save_name_char(c)) {
      dst[0] = 0;
      return false;
    }
    out[i] = c;
  }
  memcpy(dst, out, (size_t)len);
  dst[len] = 0;
  return true;
}

// Settings keys (ignored in a game file).
bool settings_key(const char* k) {
  return !strcmp(k, "antialias") || !strcmp(k, "volume_musica") || !strcmp(k, "volume_effetti") || !strcmp(k, "lingua") ||
         !strcmp(k, "salvataggi") || (!strncmp(k, "mira.", 5) && k[5] >= '1' && k[5] < '1' + SAVE_PLAYERS && !k[6]) ||
         !strcmp(k, "telecamera_distanza") || !strcmp(k, "telecamera_inclinazione") || !strcmp(k, "risoluzione") ||
         !strcmp(k, "dettagli") || !strcmp(k, "margine_tv");
}

// Keys of a game (in the settings file: the 0.5.2 one).
bool game_key(const char* k) {
  return !strcmp(k, "storia_pianeti") || !strncmp(k, "mappe.", 6) || !strncmp(k, "giro.", 5) || !strncmp(k, "stelle.", 7);
}

SaveMap* find_map_in(SaveData& d, const char* id) {
  d.nmaps = clampi(d.nmaps, 0, SAVE_MAX_MAPS);    // the game can change the data by hand
  for (int i = 0; i < d.nmaps; ++i)
    if (!strncmp(d.maps[i].id, id, SAVE_ID_LEN)) return &d.maps[i];
  return 0;
}

// The map (id already checked), added if missing; NULL if there is no room left.
SaveMap* add_map_in(SaveData& d, const char* id) {
  SaveMap* m = find_map_in(d, id);
  if (m || d.nmaps >= SAVE_MAX_MAPS) return m;
  m = &d.maps[d.nmaps++];
  snprintf(m->id, sizeof(m->id), "%s", id);
  m->stars = 0;
  return m;
}

const char* why(int e) {
  switch (e) {
    case 0: return "motivo sconosciuto";
    case ENOENT: return "la cartella non c'e'";
    case EACCES: case EPERM: return "permesso negato";
    case EROFS: return "scheda o cartella in sola lettura";
    case ENOSPC: return "spazio finito";
    case EISDIR: return "c'e' una cartella con quel nome";
    case ENOTDIR: return "il percorso non e' una cartella";
    case EIO: return "errore della scheda";
    case ENAMETOOLONG: return "percorso troppo lungo";
    case EMFILE: case ENFILE: return "troppi file aperti";
    default: {
      static char buf[24];
      snprintf(buf, sizeof(buf), "errore %d", e);
      return buf;
    }
  }
}

void set_paths(File* f, const char* name) {
  snprintf(f->path, sizeof(f->path), "%s%s%s", g_dir, strcmp(g_dir, "/") ? "/" : "", name);
  snprintf(f->tmp, sizeof(f->tmp), "%s.tmp", f->path);
  snprintf(f->bad, sizeof(f->bad), "%s.illeggibile", f->path);
}

void reset_file(File* f, Kind kind) {
  f->kind = kind;
  f->nextra = 0;
  f->backup = f->failed = f->progress = false;
  f->last_n = -1;
}

// --- reading ---------------------------------------------------------------------------

struct Reader {
  File* f;
  SaveData* d;
  int ok, bad, dropped;      // lines used, invalid lines, unknown keys with no room
  bool binary;               // control characters: maybe not a text file
  bool first;                // the first line can start with the BOM of Windows programs
};

void keep_extra(Reader* r, const char* k, const char* v) {
  File* f = r->f;
  if (f->nextra >= MAX_EXTRA) {
    r->dropped++;
    return;
  }
  snprintf(f->extra[f->nextra++], LINE_LEN, "%s=%s", k, v);
  r->ok++;
}

// One line of the file, without the newline. broken: too long or with control characters.
void read_line(Reader* r, char* s, bool broken) {
  if (r->first && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
  r->first = false;
  char* e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
  while (*s == ' ' || *s == '\t') s++;
  if (broken) {
    r->bad++;
    return;
  }
  if (!*s || *s == '#') return;
  char* eq = strchr(s, '=');
  if (!eq) {
    r->bad++;
    return;
  }
  char *k = s, *ke = eq, *v = eq + 1;
  while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
  *ke = 0;
  while (*v == ' ' || *v == '\t') v++;
  if (!valid_key(k)) {
    r->bad++;
    return;
  }
  SaveData& d = *r->d;
  long n = 0;
  const bool num = parse_int(v, &n);
  int* iv = 0;
  uint8_t* bv = 0;
  bool* fv = 0;
  uint32_t* uv = 0;
  long lo = 0, hi = 1;
  if (!strcmp(k, "versione")) {
    iv = &d.version;
    hi = 1000000;
  } else if (r->f->kind == K_SETTINGS) {
    if (!strcmp(k, "antialias")) {
      if (!num) {
        r->bad++;
        return;
      }
      d.antialias = (uint8_t)(n <= 0 ? 0 : (n == 1 ? 1 : (n < 6 ? 4 : 8)));
      r->ok++;
      return;
    }
    else if (!strcmp(k, "risoluzione")) {
      if (!num) {
        r->bad++;
        return;
      }
      d.res = snap_res(n);
      r->ok++;
      return;
    }
    else if (!strcmp(k, "telecamera_distanza")) { iv = &d.cam_dist; lo = -SAVE_CAM_STEPS; hi = SAVE_CAM_STEPS; }
    else if (!strcmp(k, "telecamera_inclinazione")) { iv = &d.cam_pitch; lo = -SAVE_CAM_STEPS; hi = SAVE_CAM_STEPS; }
    else if (!strcmp(k, "dettagli")) { bv = &d.detail; hi = 1; }
    else if (!strcmp(k, "margine_tv")) { bv = &d.margin; hi = SAVE_MARGIN_MAX; }
    else if (!strcmp(k, "volume_musica")) { bv = &d.music_vol; hi = 10; }
    else if (!strcmp(k, "volume_effetti")) { bv = &d.sfx_vol; hi = 10; }
    else if (!strncmp(k, "mira.", 5) && k[5] >= '1' && k[5] < '1' + SAVE_PLAYERS && !k[6]) { bv = &d.aim[k[5] - '1']; hi = 3; }
    else if (!strcmp(k, "salvataggi")) fv = &g_marker;
    else if (!strcmp(k, "lingua")) {
      for (int l = 0; l < SAVE_LANGS; ++l)
        if (!strcmp(v, LANG_ID[l])) {
          d.lang = (uint8_t)l;
          r->ok++;
          return;
        }
      r->bad++;
      return;
    } else {
      // Key of another version (including the 0.5.2 progress): written back unchanged.
      if (game_key(k)) r->f->progress = true;
      keep_extra(r, k, v);
      return;
    }
  } else {
    const int wm = world_key(k, "mappe."), wg = world_key(k, "giro.");
    if (!strcmp(k, "nome")) {
      if (clean_name(d.name, v)) r->ok++;
      else r->bad++;
      return;
    }
    if (!strcmp(k, "id")) {
      uint64_t x = 0;
      int n16 = 0;
      for (; v[n16]; ++n16) {
        const char c = v[n16];
        const int h = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
        if (h < 0 || n16 >= 16) break;
        x = (x << 4) | (uint64_t)h;
      }
      if (n16 == 16 && !v[16] && x) {
        d.id = x;
        r->ok++;
      } else {
        r->bad++;
      }
      return;
    }
    if (!strcmp(k, "tempo")) { uv = &d.seconds; hi = (long)MAX_SECONDS; }
    else if (wm >= 0) { bv = &d.maps_open[wm]; lo = wm == 0 ? 1 : 0; hi = 99; }
    else if (wg >= 0) { bv = &d.giro[wg]; lo = 1; hi = SAVE_GIRO_MAX; }
    else if (!strncmp(k, "stelle.", 7) && valid_id(k + 7) && (find_map_in(d, k + 7) || d.nmaps < SAVE_MAX_MAPS)) {
      if (!num) {
        r->bad++;
        return;
      }
      add_map_in(d, k + 7)->stars |= (uint8_t)clampi((int)n, 0, 7);   // the same map twice: they add up
      r->ok++;
      return;
    } else if (settings_key(k)) {
      r->ok++;               // settings in a game (taken from the 0.5.2 file): they do not count here
      return;
    } else {
      keep_extra(r, k, v);   // key of another version (or stars with no room)
      return;
    }
  }
  if (!num) {
    r->bad++;
    return;
  }
  const long c = n < lo ? lo : (n > hi ? hi : n);
  if (iv) *iv = (int)c;
  if (bv) *bv = (uint8_t)c;
  if (fv) *fv = n != 0;
  if (uv) *uv = (uint32_t)c;
  r->ok++;
}

// --- writing -----------------------------------------------------------------------------

struct Out { char* p; int n, cap; };

void put(Out* o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void put(Out* o, const char* fmt, ...) {
  if (o->n >= o->cap) return;
  va_list ap;
  va_start(ap, fmt);
  const int k = vsnprintf(o->p + o->n, (size_t)(o->cap - o->n), fmt, ap);
  va_end(ap);
  if (k > 0) o->n = k < o->cap - o->n ? o->n + k : o->cap;
}

// Brings the data into range (the game edits it by hand) and prepares the file content.
// -1 if it does not fit in cap (does not happen: OUT_LEN fits all that can be kept).
int serialize(const File* f, SaveData& d, char* buf, int cap) {
  Out o = { buf, 0, cap };
  if (f->kind == K_SETTINGS) {
    for (int i = 0; i < SAVE_PLAYERS; ++i) d.aim[i] = (uint8_t)clampi(d.aim[i], 0, 3);
    d.music_vol = (uint8_t)clampi(d.music_vol, 0, 10);
    d.sfx_vol = (uint8_t)clampi(d.sfx_vol, 0, 10);
    if (d.lang >= SAVE_LANGS) d.lang = 0;
    d.cam_dist = clampi(d.cam_dist, -SAVE_CAM_STEPS, SAVE_CAM_STEPS);
    d.cam_pitch = clampi(d.cam_pitch, -SAVE_CAM_STEPS, SAVE_CAM_STEPS);
    d.res = snap_res(d.res);
    d.detail = (uint8_t)clampi(d.detail, 0, 1);
    d.margin = (uint8_t)clampi(d.margin, 0, SAVE_MARGIN_MAX);
    put(&o, "%s", SETTINGS_HEADER);
    put(&o, "versione=%d\n", SAVE_VERSION);
    for (int i = 0; i < SAVE_PLAYERS; ++i) put(&o, "mira.%d=%d\n", i + 1, d.aim[i]);
    if (d.antialias != 0 && d.antialias != 1 && d.antialias != 4 && d.antialias != 8) d.antialias = 1;
    put(&o, "antialias=%d\n", d.antialias);
    put(&o, "volume_musica=%d\n", d.music_vol);
    put(&o, "volume_effetti=%d\n", d.sfx_vol);
    put(&o, "lingua=%s\n", LANG_ID[d.lang]);
    put(&o, "telecamera_distanza=%d\n", d.cam_dist);
    put(&o, "telecamera_inclinazione=%d\n", d.cam_pitch);
    put(&o, "risoluzione=%d\n", d.res);
    put(&o, "dettagli=%d\n", d.detail);
    put(&o, "margine_tv=%d\n", d.margin);
    put(&o, "salvataggi=%d\n", g_marker ? 1 : 0);
  } else {
    for (int w = 0; w < SAVE_WORLDS; ++w) {
      d.maps_open[w] = (uint8_t)clampi(d.maps_open[w], w == 0 ? 1 : 0, 99);
      d.giro[w] = (uint8_t)clampi(d.giro[w], 1, SAVE_GIRO_MAX);
    }
    d.nmaps = clampi(d.nmaps, 0, SAVE_MAX_MAPS);
    if (d.seconds > MAX_SECONDS) d.seconds = MAX_SECONDS;
    put(&o, "%s", GAME_HEADER);
    put(&o, "versione=%d\n", SAVE_VERSION);
    d.name[SAVE_NAME_LEN - 1] = 0;
    if (!clean_name(d.name, d.name)) d.name[0] = 0;      // the game writes it by hand: only a valid name
    if (d.name[0]) put(&o, "nome=%s\n", d.name);
    if (d.id) put(&o, "id=%016llx\n", (unsigned long long)d.id);
    // Earth always, the other planets once they have been reached.
    for (int w = 0; w < SAVE_WORLDS; ++w) {
      if (w > 0 && d.maps_open[w] == 0 && d.giro[w] <= 1) continue;
      put(&o, "mappe.%s=%d\n", WORLD_ID[w], d.maps_open[w]);
      put(&o, "giro.%s=%d\n", WORLD_ID[w], d.giro[w]);
    }
    for (int i = 0; i < d.nmaps; ++i) {
      SaveMap& m = d.maps[i];
      m.id[SAVE_ID_LEN - 1] = 0;
      if (m.stars > 7) m.stars = 7;
      if (valid_id(m.id)) put(&o, "stelle.%s=%d\n", m.id, m.stars);
    }
    put(&o, "tempo=%u\n", d.seconds);
  }
  for (int i = 0; i < f->nextra; ++i) put(&o, "%s\n", f->extra[i]);
  return o.n < cap - 1 ? o.n : -1;
}

bool failed(File* f, const char* what, const char* path, int e) {
  if (!f->failed) plat_log("salvataggio non riuscito: %s %s (%s); il gioco va avanti, i progressi restano in memoria", what, path, why(e));
  f->failed = true;
  return false;
}

// The directory on disk too, for the new name (on FAT it is the file's entry in the directory).
// If the file system does not allow it, never mind: the file is already whole.
void sync_dir(void) {
  const int fd = open(g_dir, O_RDONLY | O_DIRECTORY);
  if (fd < 0) return;
  fsync(fd);
  close(fd);
}

// Writes buf (n bytes) as the content of the file, atomically.
bool write_bytes(File* f, const char* buf, int n) {
  if (n == f->last_n && !memcmp(buf, f->last, (size_t)n)) return true;   // this is already on disk
  errno = 0;
  FILE* fp = fopen(f->tmp, "wb");
  int e = errno;
  // RetroArch can give a core directory not created yet: it is created (only the last level).
  if (!fp && e == ENOENT && mkdir(g_dir, 0777) == 0) {
    fp = fopen(f->tmp, "wb");
    e = errno;
  }
  if (!fp) return failed(f, "non riesco a creare", f->tmp, e);
  bool ok = true;
  if (fwrite(buf, 1, (size_t)n, fp) != (size_t)n || fflush(fp) != 0) {
    ok = false;
    e = errno;
  } else if (fsync(fileno(fp)) != 0 && errno != EINVAL) {
    // The data must be on the card before renaming: after a power-off the new file could be
    // there but empty. EINVAL: file system without fsync, carry on anyway.
    ok = false;
    e = errno;
  }
  if (fclose(fp) != 0 && ok) {
    ok = false;
    e = errno;
  }
  if (!ok) {
    remove(f->tmp);
    return failed(f, "non riesco a scrivere", f->tmp, e);
  }
  if (f->backup) {
    // The unreadable file is not lost: if it cannot be set aside it is not replaced.
    if (rename(f->path, f->bad) == 0) {
      plat_log("salvataggio: il file illeggibile resta come %s", f->bad);
    } else if (errno != ENOENT) {
      e = errno;
      remove(f->tmp);
      return failed(f, "non riesco a mettere da parte il file illeggibile", f->path, e);
    }
    f->backup = false;
  }
  if (rename(f->tmp, f->path) != 0) {
    e = errno;
    remove(f->tmp);
    return failed(f, "non riesco a sostituire", f->path, e);
  }
  sync_dir();
  if (n <= OUT_LEN) {
    memcpy(f->last, buf, (size_t)n);
    f->last_n = n;
  } else {
    f->last_n = -1;
  }
  if (f->failed) plat_log("salvataggio di nuovo riuscito: %s", f->path);
  f->failed = false;
  return true;
}

bool write_file(File* f, SaveData& d) {
  if (!f->path[0]) return false;
  const int n = serialize(f, d, g_out, OUT_LEN);
  if (n < 0) return failed(f, "troppi dati per", f->path, 0);
  return write_bytes(f, g_out, n);
}

// The file exists but could not be read entirely (card error, permissions): start from scratch,
// with nothing of what was read up to there, and if it is an actual file, on the first write it is
// kept as NAME.illeggibile instead of disappearing (a directory with that name is not touched).
int unreadable(File* f, int e, char* status, size_t slen) {
  struct stat st;
  f->nextra = 0;
  f->progress = false;
  f->backup = stat(f->path, &st) == 0 && S_ISREG(st.st_mode);
  const char* name = strrchr(f->path, '/');
  name = name ? name + 1 : f->path;
  if (f->backup)
    snprintf(status, slen, "illeggibile: %s, si riparte da capo; alla prima scrittura il file diventa %.60s.illeggibile", why(e), name);
  else
    snprintf(status, slen, "illeggibile: %s, si riparte da capo", why(e));
  return SAVE_ILLEGGIBILE;
}

// Reads file f into d (only the keys of its kind). status: how it went. Returns SAVE_*.
int read_file(File* f, SaveData* d, char* status, size_t slen, bool remember) {
  errno = 0;
  FILE* fp = fopen(f->path, "rb");
  if (!fp) {
    const int e = errno;
    if (e == ENOENT) {
      snprintf(status, slen, "nuovo");
      return SAVE_NUOVO;
    }
    return unreadable(f, e, status, slen);
  }
  // In chunks, one line at a time: a truncated, binary or huge file breaks nothing.
  static char chunk[4096];
  char line[LINE_LEN];
  int len_l = 0, total = 0;
  bool broken = false;
  Reader r = { f, d, 0, 0, 0, false, true };
  size_t got;
  while (total < MAX_READ && (got = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
    total += (int)got;
    for (size_t i = 0; i < got; ++i) {
      const unsigned char c = (unsigned char)chunk[i];
      if (c == '\n') {
        line[len_l] = 0;
        read_line(&r, line, broken);
        len_l = 0;
        broken = false;
        continue;
      }
      if ((c < 0x20 && c != '\t' && c != '\r') || c == 0x7F) {
        broken = true;
        r.binary = true;
      }
      if (len_l < LINE_LEN - 1) line[len_l++] = (char)c;
      else broken = true;
    }
  }
  const int read_errno = ferror(fp) ? (errno ? errno : EIO) : 0;
  const bool more = !read_errno && total >= MAX_READ && fgetc(fp) != EOF;
  fclose(fp);
  if (read_errno) return unreadable(f, read_errno, status, slen);   // even halfway through the file
  if (len_l > 0 || broken) {               // last line without a newline (or cut off above)
    line[len_l] = 0;
    read_line(&r, line, broken || more);
  }
  const char* name = strrchr(f->path, '/');
  name = name ? name + 1 : f->path;
  if (total == 0) {
    snprintf(status, slen, "illeggibile: file vuoto, si riparte da capo");
    return SAVE_ILLEGGIBILE;
  }
  if (r.ok == 0 && r.bad > 0) {
    f->nextra = 0;
    f->backup = true;
    snprintf(status, slen, "illeggibile: nessuna riga valida%s, si riparte da capo; alla prima scrittura il file diventa %.60s.illeggibile",
             r.binary ? " (non e' un file di testo)" : "", name);
    return SAVE_ILLEGGIBILE;
  }
  int n = snprintf(status, slen, "letto");
  if (r.bad > 0) n += snprintf(status + n, slen - (size_t)n, ", righe non valide ignorate: %d%s", r.bad, r.binary ? " (caratteri non di testo)" : "");
  if (r.dropped > 0 && n < (int)slen) n += snprintf(status + n, slen - (size_t)n, ", voci sconosciute di troppo perse: %d", r.dropped);
  if (more) {
    f->backup = true;                      // the rest is not lost: on the first write the whole file is set aside
    if (n < (int)slen) n += snprintf(status + n, slen - (size_t)n, ", file troppo grande: letto solo l'inizio (resta come %.60s.illeggibile)", name);
  }
  if (d->version > SAVE_VERSION && n < (int)slen)
    snprintf(status + n, slen - (size_t)n, ", scritto da una versione piu' nuova del gioco (formato %d)", d->version);
  if (remember) f->last_n = serialize(f, *d, f->last, OUT_LEN);   // the data read are already on disk
  return SAVE_LETTO;
}

bool slot_ok(int i) { return i >= 0 && i < SAVE_SLOTS && g_dir[0]; }

bool file_exists(const char* path) {
  struct stat st;
  return stat(path, &st) == 0;
}

// From 0.5.2: the progress left in pupazzi.sav becomes game 1, only once and only if there is no
// game yet. g_marker stays false (salvataggi=0) only if that failed: it is retried at the next
// startup.
void migrate(void) {
  if (g_marker) return;
  g_marker = true;
  if (!g_set.progress) return;                       // nothing to take
  for (int i = 0; i < SAVE_SLOTS; ++i)
    if (file_exists(g_slot_paths[i])) return;        // there are games already: the old progress stays where it is
  g_marker = false;
  static SaveData tmp;
  defaults(&tmp);
  reset_file(&g_scratch, K_GAME);
  snprintf(g_scratch.path, sizeof(g_scratch.path), "%s", g_set.path);   // pupazzi.sav is read as a game
  char st[200];
  if (read_file(&g_scratch, &tmp, st, sizeof(st), false) != SAVE_LETTO) {
    plat_log("salvataggio: non riesco a rileggere i progressi della versione di prima (%s), riprovo al prossimo avvio", st);
    return;
  }
  // The unknown keys read (mappe_sbloccate from 0.4...) go into the game with the rest.
  snprintf(g_scratch.path, sizeof(g_scratch.path), "%s", g_slot_paths[0]);
  snprintf(g_scratch.tmp, sizeof(g_scratch.tmp), "%s.tmp", g_slot_paths[0]);
  snprintf(g_scratch.bad, sizeof(g_scratch.bad), "%s.illeggibile", g_slot_paths[0]);
  g_scratch.backup = false;                          // it was about pupazzi.sav, which stays as it is
  g_scratch.last_n = -1;
  if (!write_file(&g_scratch, tmp)) return;          // already in the log; salvataggi=0, it is retried
  plat_log("salvataggio: i progressi della versione di prima sono la partita 1 (%s)", g_slot_paths[0]);
  g_marker = true;
  // Rewritten even if nothing changed in memory: salvataggi=1 is still missing on disk.
  g_set.last_n = -1;
  write_file(&g_set, g_data);
}

}  // namespace

int save_open(const char* dir) {
  defaults(&g_data);
  g_init = true;
  g_guest_slot = g_base_on = false;
  reset_file(&g_set, K_SETTINGS);
  reset_file(&g_game, K_GAME);
  g_active = -1;
  g_marker = g_full_logged = false;
  g_dir[0] = g_set.path[0] = g_set.tmp[0] = g_set.bad[0] = g_game.path[0] = 0;
  for (int i = 0; i < SAVE_SLOTS; ++i) g_slot_paths[i][0] = 0;
  g_slot_status[0] = 0;
  if (!dir || !*dir) {
    snprintf(g_status, sizeof(g_status), "nessuna cartella: si gioca senza salvare");
    return SAVE_NUOVO;
  }
  int len = (int)strlen(dir);
  while (len > 1 && dir[len - 1] == '/') len--;
  if (len > (int)sizeof(g_dir) - 1) {
    snprintf(g_status, sizeof(g_status), "illeggibile: percorso della cartella troppo lungo, si gioca senza salvare");
    return SAVE_ILLEGGIBILE;
  }
  memcpy(g_dir, dir, (size_t)len);
  g_dir[len] = 0;
  set_paths(&g_set, SETTINGS_NAME);
  for (int i = 0; i < SAVE_SLOTS; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "partita%d.sav", i + 1);
    snprintf(g_slot_paths[i], sizeof(g_slot_paths[i]), "%s%s%s", g_dir, strcmp(g_dir, "/") ? "/" : "", name);
  }
  const int st = read_file(&g_set, &g_data, g_status, sizeof(g_status), true);
  if (st == SAVE_LETTO) {
    migrate();
  } else {
    defaults_settings(&g_data);
    g_data.version = 0;
    g_marker = true;                       // in the new file there is no old progress to take
  }
  return st;
}

const char* save_status(void) { return g_status; }
const char* save_path(void) { return g_set.path; }
SaveData* save_data(void) { return &data(); }

uint8_t save_stars(const char* map_id) {
  if (!valid_id(map_id)) return 0;
  const SaveMap* m = find_map_in(data(), map_id);
  return m ? m->stars : 0;
}

uint8_t save_stars_in(const SaveData* d, const char* map_id) {
  if (!d || !valid_id(map_id)) return 0;
  const int n = clampi(d->nmaps, 0, SAVE_MAX_MAPS);
  for (int i = 0; i < n; ++i)
    if (!strncmp(d->maps[i].id, map_id, SAVE_ID_LEN)) return d->maps[i].stars;
  return 0;
}

void save_add_stars(const char* map_id, uint8_t bits) {
  if (!valid_id(map_id)) {
    plat_log("salvataggio: nome di mappa \"%.40s\" non valido (solo lettere, cifre, _ e -, al massimo %d): stelle non salvate",
             map_id ? map_id : "", SAVE_ID_LEN - 1);
    return;
  }
  SaveMap* m = add_map_in(data(), map_id);
  if (!m) {
    if (!g_full_logged) plat_log("salvataggio: troppe mappe (al massimo %d), le stelle di %s non si salvano", SAVE_MAX_MAPS, map_id);
    g_full_logged = true;
    return;
  }
  m->stars |= (uint8_t)(bits & 7u);
}

void save_set_stars(const char* map_id, uint8_t bits) {
  if (!valid_id(map_id)) return;
  SaveMap* m = find_map_in(data(), map_id);
  if (!m && bits == 0) return;            // 0 stars: as if the map were not there
  if (!m) m = add_map_in(data(), map_id);
  if (m) m->stars = (uint8_t)(bits & 7u);
}

const char* save_world_id(int w) { return w >= 0 && w < SAVE_WORLDS ? WORLD_ID[w] : ""; }
const char* save_lang_id(int l) { return l >= 0 && l < SAVE_LANGS ? LANG_ID[l] : ""; }

// LAN game saved on a console that joins (0.5.6): the slot as it was, when it held the same game
// (what it has more is kept at every write), and the planet of a map.

// The game in memory (the host's progress) with, planet by planet, what g_base has more (see
// save_guest_slot_begin).
const SaveData& merged(const SaveData& d) {
  static SaveData m;
  m = d;
  if (!g_base_on) return m;
  for (int w = 0; w < SAVE_WORLDS; ++w) {
    if (g_base.giro[w] < d.giro[w]) continue;
    const bool later = g_base.giro[w] > d.giro[w];
    m.giro[w] = g_base.giro[w];
    if (later || g_base.maps_open[w] > m.maps_open[w]) m.maps_open[w] = g_base.maps_open[w];
    // The stars of this planet's maps: those of the file if its round is later, otherwise both.
    for (int i = 0; i < m.nmaps; ++i)
      if (g_world_of && g_world_of(m.maps[i].id) == w && later) m.maps[i].stars = 0;
    for (int i = 0; i < g_base.nmaps; ++i) {
      if (!g_world_of || g_world_of(g_base.maps[i].id) != w) continue;
      SaveMap* s = add_map_in(m, g_base.maps[i].id);
      if (s) s->stars |= g_base.maps[i].stars;
    }
  }
  // Maps that are not on this console now (custom maps taken away): their stars stay.
  for (int i = 0; i < g_base.nmaps; ++i) {
    if (g_world_of && g_world_of(g_base.maps[i].id) >= 0) continue;
    SaveMap* s = add_map_in(m, g_base.maps[i].id);
    if (s) s->stars |= g_base.maps[i].stars;
  }
  return m;
}

// The active game into its file (merged, for a LAN game saved also here).
bool write_active(void) { return write_file(&g_game, g_base_on ? const_cast<SaveData&>(merged(data())) : data()); }

// Another game becomes the active one (or none): the LAN game saved also here is over.
void guest_slot_off(void) { g_guest_slot = g_base_on = false; }

uint64_t new_game_id(void) {
  uint64_t x = 0;
  FILE* f = fopen("/dev/urandom", "rb");
  if (f) {
    if (fread(&x, sizeof(x), 1, f) != 1) x = 0;
    fclose(f);
  }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  x ^= (uint64_t)ts.tv_sec * 1000000007ull ^ (uint64_t)ts.tv_nsec * 0x9E3779B97F4A7C15ull ^ ((uint64_t)getpid() << 40);
  x += 0x9E3779B97F4A7C15ull;                 // splitmix64
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  x ^= x >> 31;
  return x ? x : 1;
}

bool save_write(void) {
  if (!g_set.path[0]) return false;
  bool ok = write_file(&g_set, data());
  if (g_active >= 0) ok &= write_active();
  return ok;
}

int save_slot(void) { return g_active; }
const char* save_slot_path(int i) { return i >= 0 && i < SAVE_SLOTS ? g_slot_paths[i] : ""; }
const char* save_slot_status(void) { return g_slot_status; }

int save_slot_peek(int i, SaveData* out) {
  defaults(out);
  if (!slot_ok(i)) return SAVE_NUOVO;
  reset_file(&g_scratch, K_GAME);
  snprintf(g_scratch.path, sizeof(g_scratch.path), "%s", g_slot_paths[i]);
  char st[200];
  const int r = read_file(&g_scratch, out, st, sizeof(st), false);
  if (r != SAVE_LETTO) defaults_game(out);
  return r;
}

bool save_slot_load(int i) {
  if (!slot_ok(i)) return false;
  if (g_active >= 0) write_active();   // the previous one loses nothing
  guest_slot_off();
  SaveData& d = data();
  defaults_game(&d);
  reset_file(&g_game, K_GAME);
  const char* name = strrchr(g_slot_paths[i], '/');
  set_paths(&g_game, name ? name + 1 : g_slot_paths[i]);
  const int r = read_file(&g_game, &d, g_slot_status, sizeof(g_slot_status), true);
  if (r == SAVE_NUOVO) {
    g_active = -1;
    g_game.path[0] = 0;
    return false;
  }
  if (r != SAVE_LETTO) defaults_game(&d);
  g_active = i;
  plat_log("salvataggio: partita %d (%s)", i + 1, g_slot_status);
  return true;
}

bool save_slot_new(int i, const char* name) {
  if (!slot_ok(i)) return false;
  if (g_active >= 0 && g_active != i) write_active();
  guest_slot_off();
  SaveData& d = data();
  defaults_game(&d);
  if (name) clean_name(d.name, name);
  reset_file(&g_game, K_GAME);
  const char* file = strrchr(g_slot_paths[i], '/');
  set_paths(&g_game, file ? file + 1 : g_slot_paths[i]);
  g_active = i;
  snprintf(g_slot_status, sizeof(g_slot_status), "nuova");
  plat_log("salvataggio: partita %d nuova%s%s%s", i + 1, d.name[0] ? " (\"" : "", d.name, d.name[0] ? "\")" : "");
  return write_file(&g_game, d);
}

bool save_slot_rename(int i, const char* name) {
  char nm[SAVE_NAME_LEN];
  if (!slot_ok(i) || !clean_name(nm, name ? name : "")) return false;
  bool ok;
  if (i == g_active) {
    memcpy(data().name, nm, sizeof(nm));
    ok = write_active();
  } else {
    // Another game: it is read again (unknown keys included) and rewritten with the new name.
    static SaveData tmp;
    defaults(&tmp);
    reset_file(&g_scratch, K_GAME);
    snprintf(g_scratch.path, sizeof(g_scratch.path), "%s", g_slot_paths[i]);
    snprintf(g_scratch.tmp, sizeof(g_scratch.tmp), "%s.tmp", g_slot_paths[i]);
    snprintf(g_scratch.bad, sizeof(g_scratch.bad), "%s.illeggibile", g_slot_paths[i]);
    char st[200];
    if (read_file(&g_scratch, &tmp, st, sizeof(st), false) != SAVE_LETTO) return false;
    memcpy(tmp.name, nm, sizeof(nm));
    ok = write_file(&g_scratch, tmp);
  }
  if (ok) plat_log("salvataggio: partita %d rinominata \"%s\"", i + 1, nm);
  return ok;
}

bool save_name_char(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '\'' || c == '!' || c == '?' ||
         c == '.';
}

bool save_slot_copy(int from, int to) {
  if (!slot_ok(from) || !slot_ok(to) || from == to) return false;
  if (from == g_active) write_active();
  FILE* fp = fopen(g_slot_paths[from], "rb");
  if (!fp) return false;
  static char buf[OUT_LEN * 2];
  const size_t n = fread(buf, 1, sizeof(buf), fp);
  const bool bad = ferror(fp) != 0, big = !bad && fgetc(fp) != EOF;
  fclose(fp);
  if (n == 0 || bad || big) return false;         // never a half-done copy
  reset_file(&g_scratch, K_GAME);
  snprintf(g_scratch.path, sizeof(g_scratch.path), "%s", g_slot_paths[to]);
  snprintf(g_scratch.tmp, sizeof(g_scratch.tmp), "%s.tmp", g_slot_paths[to]);
  snprintf(g_scratch.bad, sizeof(g_scratch.bad), "%s.illeggibile", g_slot_paths[to]);
  bool ok = write_bytes(&g_scratch, buf, (int)n);
  if (ok) {
    // 0.5.6: the copy is a game of its own from now on: with an identity it gets a new one (otherwise
    // whoever joins it on the network would save both in the same slot).
    static SaveData tmp;
    defaults(&tmp);
    char st[200];
    if (read_file(&g_scratch, &tmp, st, sizeof(st), true) == SAVE_LETTO && tmp.id) {
      tmp.id = new_game_id();
      ok = write_file(&g_scratch, tmp);
      if (!ok) remove(g_slot_paths[to]);        // never two games with the same identity
    }
  }
  if (ok) {
    plat_log("salvataggio: partita %d copiata nella %d", from + 1, to + 1);
    if (to == g_active) {                      // does not happen from the menu (copies go to a free slot)
      g_active = -1;                           // the copy wins over the data in memory
      save_slot_load(to);
    }
  }
  return ok;
}

bool save_slot_delete(int i) {
  if (!slot_ok(i)) return false;
  if (i == g_active) {
    g_active = -1;
    g_game.path[0] = 0;
    defaults_game(&data());
    guest_slot_off();
  }
  char tmp[310];
  snprintf(tmp, sizeof(tmp), "%s.tmp", g_slot_paths[i]);
  remove(tmp);
  if (remove(g_slot_paths[i]) != 0 && errno != ENOENT) {
    plat_log("salvataggio: non riesco a eliminare %s (%s)", g_slot_paths[i], why(errno));
    return false;
  }
  sync_dir();
  plat_log("salvataggio: partita %d eliminata", i + 1);
  return true;
}

void save_guest_begin(void) {
  if (g_active >= 0) {
    write_active();                    // the active one loses nothing
    plat_log("salvataggio: partita %d messa da parte (si gioca con i progressi di chi ha creato la partita in rete)", g_active + 1);
  }
  guest_slot_off();
  g_active = -1;
  g_game.path[0] = 0;
  defaults_game(&data());
}

void save_guest_end(void) {
  if (g_guest_slot && g_active >= 0) {
    save_write();
    plat_log("salvataggio: partita in rete salvata anche qui, nel posto %d", g_active + 1);
  }
  g_guest_slot = g_base_on = false;
  g_active = -1;
  g_game.path[0] = 0;
  defaults_game(&data());
}

void save_guest_slot_begin(int i, uint64_t id, int (*world_of)(const char* map_id)) {
  if (!slot_ok(i) || !id) {
    save_guest_begin();
    return;
  }
  if (g_active >= 0) write_active();     // the active one loses nothing
  guest_slot_off();
  g_world_of = world_of;
  // The slot as it is: the same game (with its unknown keys) or another one, overwritten.
  reset_file(&g_game, K_GAME);
  const char* file = strrchr(g_slot_paths[i], '/');
  set_paths(&g_game, file ? file + 1 : g_slot_paths[i]);
  defaults(&g_base);
  char st[200];
  const int r = read_file(&g_game, &g_base, st, sizeof(st), true);
  g_base_on = r == SAVE_LETTO && g_base.id == id;
  if (!g_base_on) {
    reset_file(&g_game, K_GAME);
    set_paths(&g_game, file ? file + 1 : g_slot_paths[i]);
  }
  SaveData& d = data();
  defaults_game(&d);
  d.id = id;
  if (g_base_on) d.seconds = g_base.seconds;
  g_active = i;
  g_guest_slot = true;
  plat_log("salvataggio: la partita in rete si salva anche qui, nel posto %d (%s)", i + 1,
           g_base_on ? "la stessa partita: si aggiorna" : (r == SAVE_NUOVO ? "posto libero" : "sostituisce quella che c'era"));
}

uint64_t save_game_id(void) {
  if (g_active < 0) return 0;
  SaveData& d = data();
  if (!d.id) {
    d.id = new_game_id();
    plat_log("salvataggio: la partita %d ha ora un'identita' (%016llx)", g_active + 1, (unsigned long long)d.id);
    save_write();
  }
  return d.id;
}

int save_slot_with_id(uint64_t id) {
  if (!id) return -1;
  static SaveData d;
  for (int i = 0; i < SAVE_SLOTS; ++i) {
    if (i == g_active && data().id == id) return i;
    if (save_slot_peek(i, &d) == SAVE_LETTO && d.id == id) return i;
  }
  return -1;
}
