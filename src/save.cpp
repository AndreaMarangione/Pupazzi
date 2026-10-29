// Salvataggi: lettura tollerante e scrittura atomica di pupazzi.sav (impostazioni) e partitaN.sav
// (partite); formato e uso in save.h. Niente allocazioni: tutto in memoria statica, il file piu'
// grande possibile sta in OUT_LEN.
#include "save.h"
#include "plat.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char* const SETTINGS_NAME = "pupazzi.sav";
const char* const SETTINGS_HEADER =
    "# Pupazzi: impostazioni di questa console. Le partite sono in partita1.sav ... partita4.sav\n"
    "# (si duplicano e si eliminano dal gioco: prima schermata, Gestisci salvataggi).\n";
const char* const GAME_HEADER =
    "# Pupazzi: una partita. Cancella il file (o eliminala dal gioco) per liberare il posto.\n"
    "# giro.PIANETA: giro del pianeta; mappe.PIANETA: sue mappe aperte, in ordine; tempo: secondi di gioco\n"
    "# stelle.MAPPA: stelle del giro in corso, somma di 1 (finita), 2 (esploratore salvato), 4 (senza ricominciare la mappa)\n";
const int LINE_LEN = 256;           // righe piu' lunghe (a capo compreso): non valide
const int MAX_EXTRA = 64;           // chiavi sconosciute conservate
const int MAX_READ = 256 * 1024;    // di un file piu' grande (non e' un salvataggio) si legge solo l'inizio
const int OUT_LEN = 24 * 1024;      // intestazione, voci, pianeti, 64 mappe e 64 righe sconosciute stanno in 19 KB
const uint32_t MAX_SECONDS = 999999999u;
// Pianeti nell'ordine del viaggio (WORLD_* in level.h) e lingue nell'ordine di LANG_* (lang.h).
const char* const WORLD_ID[SAVE_WORLDS] = { "terra", "luna", "marte", "venere", "mercurio", "giove", "saturno", "urano", "nettuno" };
const char* const LANG_ID[SAVE_LANGS] = { "it", "en", "es", "pt", "zh", "ja", "hi" };

enum Kind { K_SETTINGS, K_GAME };

// Un file con le sue righe sconosciute e cio' che c'e' gia' sul disco.
struct File {
  Kind kind;
  char path[300], tmp[310], bad[320];
  char extra[MAX_EXTRA][LINE_LEN];  // "chiave=valore" che questa versione non conosce, nell'ordine del file
  int nextra;
  bool backup;                      // file illeggibile: alla prima scrittura diventa NOME.illeggibile
  bool failed;                      // l'ultima scrittura non e' riuscita (gia' scritto nel registro)
  char last[OUT_LEN];               // contenuto gia' sul disco (scritto o letto): se non cambia non si riscrive
  int last_n;
  bool progress;                    // (impostazioni) c'erano chiavi della partita: file della 0.5.2
};

bool g_init;                        // g_data ha i valori predefiniti anche prima di save_open
SaveData g_data;
char g_dir[256];
char g_status[200], g_slot_status[200];
File g_set, g_game;                 // impostazioni e partita attiva
File g_scratch;                     // letture di prova (save_slot_peek) e copie
int g_active = -1;
bool g_marker;                      // salvataggi=1: niente (piu') da prendere dal file della 0.5.2; 0 se non ci si e' riusciti
bool g_full_logged;                 // troppe mappe: scritto una volta sola
char g_out[OUT_LEN];
char g_slot_paths[SAVE_SLOTS][300];

void defaults_settings(SaveData* d) {
  for (int i = 0; i < SAVE_PLAYERS; ++i) d->aim[i] = 1;
  d->antialias = true;
  d->music_vol = 6;   // 0.5: musiche nuove piu' leggere, sotto gli effetti
  d->sfx_vol = 8;
  d->lang = 0;
}

void defaults_game(SaveData* d) {
  for (int w = 0; w < SAVE_WORLDS; ++w) {
    d->maps_open[w] = 0;
    d->giro[w] = 1;
  }
  d->name[0] = 0;
  d->maps_open[0] = 1;                       // la prima mappa della Terra
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

// Intero con il segno facoltativo e nient'altro attorno; i valori enormi si fermano a +-1000000000.
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

// Pianeta della chiave "prefisso.pianeta" (-1 se non e' uno dei pianeti).
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

// Nome di una partita in dst (SAVE_NAME_LEN byte): spazi attorno tolti, minuscole in maiuscole. false
// (dst vuoto) se e' troppo lungo o ha caratteri che non vanno.
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

// Chiavi delle impostazioni (in un file di partita si ignorano).
bool settings_key(const char* k) {
  return !strcmp(k, "antialias") || !strcmp(k, "volume_musica") || !strcmp(k, "volume_effetti") || !strcmp(k, "lingua") ||
         !strcmp(k, "salvataggi") || (!strncmp(k, "mira.", 5) && k[5] >= '1' && k[5] < '1' + SAVE_PLAYERS && !k[6]);
}

// Chiavi di una partita (nel file delle impostazioni: quello della 0.5.2).
bool game_key(const char* k) {
  return !strcmp(k, "storia_pianeti") || !strncmp(k, "mappe.", 6) || !strncmp(k, "giro.", 5) || !strncmp(k, "stelle.", 7);
}

SaveMap* find_map_in(SaveData& d, const char* id) {
  d.nmaps = clampi(d.nmaps, 0, SAVE_MAX_MAPS);    // il gioco puo' cambiare i dati a mano
  for (int i = 0; i < d.nmaps; ++i)
    if (!strncmp(d.maps[i].id, id, SAVE_ID_LEN)) return &d.maps[i];
  return 0;
}

// La mappa (id gia' controllato), aggiunta se manca; NULL se non c'e' piu' posto.
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

// --- lettura ---------------------------------------------------------------------------

struct Reader {
  File* f;
  SaveData* d;
  int ok, bad, dropped;      // righe usate, righe non valide, chiavi sconosciute senza posto
  bool binary;               // caratteri di controllo: forse non e' un file di testo
  bool first;                // la prima riga puo' cominciare con il BOM dei programmi di Windows
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

// Una riga del file, senza il ritorno a capo. broken: troppo lunga o con caratteri di controllo.
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
    if (!strcmp(k, "antialias")) fv = &d.antialias;
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
      // Chiave di un'altra versione (anche i progressi della 0.5.2): si riscrive uguale.
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
    if (!strcmp(k, "tempo")) { uv = &d.seconds; hi = (long)MAX_SECONDS; }
    else if (wm >= 0) { bv = &d.maps_open[wm]; lo = wm == 0 ? 1 : 0; hi = 99; }
    else if (wg >= 0) { bv = &d.giro[wg]; lo = 1; hi = SAVE_GIRO_MAX; }
    else if (!strncmp(k, "stelle.", 7) && valid_id(k + 7) && (find_map_in(d, k + 7) || d.nmaps < SAVE_MAX_MAPS)) {
      if (!num) {
        r->bad++;
        return;
      }
      add_map_in(d, k + 7)->stars |= (uint8_t)clampi((int)n, 0, 7);   // la stessa mappa due volte: si sommano
      r->ok++;
      return;
    } else if (settings_key(k)) {
      r->ok++;               // impostazioni in una partita (presa dal file della 0.5.2): non contano qui
      return;
    } else {
      keep_extra(r, k, v);   // chiave di un'altra versione (o stelle senza posto)
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

// --- scrittura ---------------------------------------------------------------------------

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

// Riporta nei limiti i dati (il gioco li cambia a mano) e prepara il contenuto del file.
// -1 se non sta in cap (non succede: OUT_LEN basta per tutto quello che si puo' tenere).
int serialize(const File* f, SaveData& d, char* buf, int cap) {
  Out o = { buf, 0, cap };
  if (f->kind == K_SETTINGS) {
    for (int i = 0; i < SAVE_PLAYERS; ++i) d.aim[i] = (uint8_t)clampi(d.aim[i], 0, 3);
    d.music_vol = (uint8_t)clampi(d.music_vol, 0, 10);
    d.sfx_vol = (uint8_t)clampi(d.sfx_vol, 0, 10);
    if (d.lang >= SAVE_LANGS) d.lang = 0;
    put(&o, "%s", SETTINGS_HEADER);
    put(&o, "versione=%d\n", SAVE_VERSION);
    for (int i = 0; i < SAVE_PLAYERS; ++i) put(&o, "mira.%d=%d\n", i + 1, d.aim[i]);
    put(&o, "antialias=%d\n", d.antialias ? 1 : 0);
    put(&o, "volume_musica=%d\n", d.music_vol);
    put(&o, "volume_effetti=%d\n", d.sfx_vol);
    put(&o, "lingua=%s\n", LANG_ID[d.lang]);
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
    if (!clean_name(d.name, d.name)) d.name[0] = 0;      // il gioco lo scrive a mano: solo un nome valido
    if (d.name[0]) put(&o, "nome=%s\n", d.name);
    // La Terra sempre, gli altri pianeti quando sono stati raggiunti.
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

// Anche la cartella sul disco, per il nome nuovo (sulla FAT e' la voce del file nella cartella).
// Se il file system non lo permette pazienza: il file e' gia' intero.
void sync_dir(void) {
  const int fd = open(g_dir, O_RDONLY | O_DIRECTORY);
  if (fd < 0) return;
  fsync(fd);
  close(fd);
}

// Scrive buf (n byte) come contenuto del file, in modo atomico.
bool write_bytes(File* f, const char* buf, int n) {
  if (n == f->last_n && !memcmp(buf, f->last, (size_t)n)) return true;   // sul disco c'e' gia' questo
  errno = 0;
  FILE* fp = fopen(f->tmp, "wb");
  int e = errno;
  // RetroArch puo' dare la cartella del core non ancora creata: si crea (solo l'ultimo livello).
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
    // I dati devono essere sulla scheda prima di rinominare: dopo uno spegnimento il file nuovo
    // potrebbe esserci ma vuoto. EINVAL: file system senza fsync, si va avanti lo stesso.
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
    // Il file illeggibile non si perde: se non lo si riesce a mettere da parte non lo si sostituisce.
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

// Il file c'e' ma non si e' potuto leggere tutto (errore della scheda, permessi): si riparte da capo,
// senza nulla di quello che si e' letto fin li', e se e' un file vero e proprio alla prima scrittura
// resta come NOME.illeggibile invece di sparire (una cartella con quel nome non si tocca).
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

// Legge il file f in d (solo le chiavi del suo tipo). status: com'e' andata. Restituisce SAVE_*.
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
  // A pezzi, una riga alla volta: un file troncato, binario o enorme non rompe nulla.
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
  if (read_errno) return unreadable(f, read_errno, status, slen);   // anche a meta' del file
  if (len_l > 0 || broken) {               // ultima riga senza a capo (o tagliata qui sopra)
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
    f->backup = true;                      // il resto non si perde: alla prima scrittura il file intero resta da parte
    if (n < (int)slen) n += snprintf(status + n, slen - (size_t)n, ", file troppo grande: letto solo l'inizio (resta come %.60s.illeggibile)", name);
  }
  if (d->version > SAVE_VERSION && n < (int)slen)
    snprintf(status + n, slen - (size_t)n, ", scritto da una versione piu' nuova del gioco (formato %d)", d->version);
  if (remember) f->last_n = serialize(f, *d, f->last, OUT_LEN);   // i dati letti sono gia' sul disco
  return SAVE_LETTO;
}

bool slot_ok(int i) { return i >= 0 && i < SAVE_SLOTS && g_dir[0]; }

bool file_exists(const char* path) {
  struct stat st;
  return stat(path, &st) == 0;
}

// Dalla 0.5.2: i progressi rimasti in pupazzi.sav diventano la partita 1, una volta sola e solo se
// non c'e' ancora nessuna partita. g_marker resta false (salvataggi=0) solo se non ci si e' riusciti:
// si riprova al prossimo avvio.
void migrate(void) {
  if (g_marker) return;
  g_marker = true;
  if (!g_set.progress) return;                       // niente da prendere
  for (int i = 0; i < SAVE_SLOTS; ++i)
    if (file_exists(g_slot_paths[i])) return;        // ci sono gia' partite: i progressi vecchi restano dove sono
  g_marker = false;
  static SaveData tmp;
  defaults(&tmp);
  reset_file(&g_scratch, K_GAME);
  snprintf(g_scratch.path, sizeof(g_scratch.path), "%s", g_set.path);   // si legge pupazzi.sav come partita
  char st[200];
  if (read_file(&g_scratch, &tmp, st, sizeof(st), false) != SAVE_LETTO) {
    plat_log("salvataggio: non riesco a rileggere i progressi della versione di prima (%s), riprovo al prossimo avvio", st);
    return;
  }
  // Le chiavi sconosciute lette (mappe_sbloccate della 0.4...) vanno nella partita con il resto.
  snprintf(g_scratch.path, sizeof(g_scratch.path), "%s", g_slot_paths[0]);
  snprintf(g_scratch.tmp, sizeof(g_scratch.tmp), "%s.tmp", g_slot_paths[0]);
  snprintf(g_scratch.bad, sizeof(g_scratch.bad), "%s.illeggibile", g_slot_paths[0]);
  g_scratch.backup = false;                          // riguardava pupazzi.sav, che resta com'e'
  g_scratch.last_n = -1;
  if (!write_file(&g_scratch, tmp)) return;          // gia' nel registro; salvataggi=0, si riprova
  plat_log("salvataggio: i progressi della versione di prima sono la partita 1 (%s)", g_slot_paths[0]);
  g_marker = true;
  // Si riscrive anche se in memoria nulla e' cambiato: sul disco manca ancora salvataggi=1.
  g_set.last_n = -1;
  write_file(&g_set, g_data);
}

}  // namespace

int save_open(const char* dir) {
  defaults(&g_data);
  g_init = true;
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
    g_marker = true;                       // nel file nuovo non ci sono progressi vecchi da prendere
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
  if (!m && bits == 0) return;            // 0 stelle: come se la mappa non ci fosse
  if (!m) m = add_map_in(data(), map_id);
  if (m) m->stars = (uint8_t)(bits & 7u);
}

const char* save_world_id(int w) { return w >= 0 && w < SAVE_WORLDS ? WORLD_ID[w] : ""; }
const char* save_lang_id(int l) { return l >= 0 && l < SAVE_LANGS ? LANG_ID[l] : ""; }

bool save_write(void) {
  if (!g_set.path[0]) return false;
  bool ok = write_file(&g_set, data());
  if (g_active >= 0) ok &= write_file(&g_game, data());
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
  if (g_active >= 0) write_file(&g_game, data());   // quella di prima non perde nulla
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
  if (g_active >= 0 && g_active != i) write_file(&g_game, data());
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
    ok = write_file(&g_game, data());
  } else {
    // Un'altra partita: la si rilegge (chiavi sconosciute comprese) e la si riscrive col nome nuovo.
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
  if (from == g_active) write_file(&g_game, data());
  FILE* fp = fopen(g_slot_paths[from], "rb");
  if (!fp) return false;
  static char buf[OUT_LEN * 2];
  const size_t n = fread(buf, 1, sizeof(buf), fp);
  const bool bad = ferror(fp) != 0, big = !bad && fgetc(fp) != EOF;
  fclose(fp);
  if (n == 0 || bad || big) return false;         // mai una copia a meta'
  reset_file(&g_scratch, K_GAME);
  snprintf(g_scratch.path, sizeof(g_scratch.path), "%s", g_slot_paths[to]);
  snprintf(g_scratch.tmp, sizeof(g_scratch.tmp), "%s.tmp", g_slot_paths[to]);
  snprintf(g_scratch.bad, sizeof(g_scratch.bad), "%s.illeggibile", g_slot_paths[to]);
  const bool ok = write_bytes(&g_scratch, buf, (int)n);
  if (ok) {
    plat_log("salvataggio: partita %d copiata nella %d", from + 1, to + 1);
    if (to == g_active) {                      // non succede dal menu (si copia in un posto libero)
      g_active = -1;                           // la copia vale piu' dei dati in memoria
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
