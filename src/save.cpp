// Salvataggio: lettura tollerante e scrittura atomica di pupazzi.sav (formato e uso in save.h).
// Niente allocazioni: tutto in memoria statica, il file piu' grande possibile sta in OUT_LEN.
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

const char* const FILE_NAME = "pupazzi.sav";
const char* const HEADER =
    "# Pupazzi: salvataggio. Cancellalo per ricominciare da capo.\n"
    "# giro.PIANETA: giro del pianeta; mappe.PIANETA: sue mappe aperte, in ordine\n"
    "# stelle.MAPPA: stelle del giro in corso, somma di 1 (finita), 2 (esploratore salvato), 4 (senza ricominciare la mappa)\n";
const int LINE_LEN = 256;           // righe piu' lunghe (a capo compreso): non valide
const int MAX_EXTRA = 64;           // chiavi sconosciute conservate
const int MAX_READ = 256 * 1024;    // di un file piu' grande (non e' un salvataggio) si legge solo l'inizio
const int OUT_LEN = 24 * 1024;      // intestazione, voci, pianeti, 64 mappe e 64 righe sconosciute stanno in 19 KB
// Pianeti nell'ordine del viaggio (WORLD_* in level.h).
const char* const WORLD_ID[SAVE_WORLDS] = { "terra", "luna", "marte", "venere", "mercurio", "giove", "saturno", "urano", "nettuno" };

bool g_init;                        // g_data ha i valori predefiniti anche prima di save_open
SaveData g_data;
char g_dir[256], g_path[300], g_tmp[310], g_bad[320];
char g_status[200];
char g_extra[MAX_EXTRA][LINE_LEN];  // "chiave=valore" che questa versione non conosce, nell'ordine del file
int g_nextra;
bool g_backup;                      // file illeggibile: al primo salvataggio diventa pupazzi.sav.illeggibile
bool g_failed;                      // l'ultima scrittura non e' riuscita (gia' scritto nel registro)
bool g_full_logged;                 // troppe mappe: scritto una volta sola
char g_out[OUT_LEN];
char g_last[OUT_LEN];               // contenuto gia' sul disco (scritto o letto): se non cambia non si riscrive
int g_last_n = -1;

void defaults(SaveData* d) {
  memset(d, 0, sizeof(*d));
  for (int i = 0; i < SAVE_PLAYERS; ++i) d->aim[i] = 1;
  d->maps_open[0] = 1;                       // la prima mappa della Terra
  for (int w = 0; w < SAVE_WORLDS; ++w) d->giro[w] = 1;
  d->antialias = true;
  d->music_vol = 6;   // 0.5: musiche nuove piu' leggere, sotto gli effetti
  d->sfx_vol = 8;
}

SaveData& data(void) {
  if (!g_init) {
    defaults(&g_data);
    g_init = true;
  }
  return g_data;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Intero con il segno facoltativo e nient'altro attorno; i valori enormi si fermano a +-1000000.
bool parse_int(const char* s, int* out) {
  int sign = 1, v = 0, n = 0;
  if (*s == '-') sign = -1;
  if (*s == '-' || *s == '+') s++;
  for (; *s >= '0' && *s <= '9'; ++s, ++n)
    if (v < 1000000) v = v * 10 + (*s - '0');
  if (n == 0 || *s) return false;
  *out = sign * (v > 1000000 ? 1000000 : v);
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

SaveMap* find_map(const char* id) {
  SaveData& d = data();
  d.nmaps = clampi(d.nmaps, 0, SAVE_MAX_MAPS);    // il gioco puo' cambiare i dati a mano
  for (int i = 0; i < d.nmaps; ++i)
    if (!strncmp(d.maps[i].id, id, SAVE_ID_LEN)) return &d.maps[i];
  return 0;
}

// La mappa (id gia' controllato), aggiunta se manca; NULL se non c'e' piu' posto.
SaveMap* add_map(const char* id) {
  SaveMap* m = find_map(id);
  SaveData& d = data();
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

// --- lettura ---------------------------------------------------------------------------

struct Reader {
  int ok, bad, dropped;      // righe usate, righe non valide, chiavi sconosciute senza posto
  bool binary;               // caratteri di controllo: forse non e' un file di testo
  bool first;                // la prima riga puo' cominciare con il BOM dei programmi di Windows
};

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
  SaveData& d = data();
  int n = 0;
  const bool num = parse_int(v, &n);
  int* iv = 0;
  uint8_t* bv = 0;
  bool* fv = 0;
  int lo = 0, hi = 1;
  const int wm = world_key(k, "mappe."), wg = world_key(k, "giro.");
  if (!strcmp(k, "versione")) { iv = &d.version; hi = 1000000; }
  else if (!strcmp(k, "storia_pianeti")) fv = &d.story_seen;
  else if (!strcmp(k, "antialias")) fv = &d.antialias;
  else if (!strcmp(k, "volume_musica")) { bv = &d.music_vol; hi = 10; }
  else if (!strcmp(k, "volume_effetti")) { bv = &d.sfx_vol; hi = 10; }
  else if (!strncmp(k, "mira.", 5) && k[5] >= '1' && k[5] < '1' + SAVE_PLAYERS && !k[6]) { bv = &d.aim[k[5] - '1']; hi = 3; }
  else if (wm >= 0) { bv = &d.maps_open[wm]; lo = wm == 0 ? 1 : 0; hi = 99; }
  else if (wg >= 0) { bv = &d.giro[wg]; lo = 1; hi = SAVE_GIRO_MAX; }
  else if (!strncmp(k, "stelle.", 7) && valid_id(k + 7) && (find_map(k + 7) || d.nmaps < SAVE_MAX_MAPS)) {
    if (!num) {
      r->bad++;
      return;
    }
    add_map(k + 7)->stars |= (uint8_t)clampi(n, 0, 7);   // la stessa mappa due volte: si sommano
    r->ok++;
    return;
  }
  if (iv || bv || fv) {
    if (!num) {
      r->bad++;
      return;
    }
    if (iv) *iv = clampi(n, lo, hi);
    if (bv) *bv = (uint8_t)clampi(n, lo, hi);
    if (fv) *fv = n != 0;
    r->ok++;
    return;
  }
  // Chiave di un'altra versione (o stelle senza posto): si riscrive uguale.
  if (g_nextra >= MAX_EXTRA) {
    r->dropped++;
    return;
  }
  snprintf(g_extra[g_nextra++], LINE_LEN, "%s=%s", k, v);
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
int serialize(char* buf, int cap) {
  SaveData& d = data();
  for (int i = 0; i < SAVE_PLAYERS; ++i) d.aim[i] = (uint8_t)clampi(d.aim[i], 0, 3);
  for (int w = 0; w < SAVE_WORLDS; ++w) {
    d.maps_open[w] = (uint8_t)clampi(d.maps_open[w], w == 0 ? 1 : 0, 99);
    d.giro[w] = (uint8_t)clampi(d.giro[w], 1, SAVE_GIRO_MAX);
  }
  d.music_vol = (uint8_t)clampi(d.music_vol, 0, 10);
  d.sfx_vol = (uint8_t)clampi(d.sfx_vol, 0, 10);
  d.nmaps = clampi(d.nmaps, 0, SAVE_MAX_MAPS);
  Out o = { buf, 0, cap };
  put(&o, "%s", HEADER);
  put(&o, "versione=%d\n", SAVE_VERSION);
  put(&o, "storia_pianeti=%d\n", d.story_seen ? 1 : 0);
  for (int i = 0; i < SAVE_PLAYERS; ++i) put(&o, "mira.%d=%d\n", i + 1, d.aim[i]);
  put(&o, "antialias=%d\n", d.antialias ? 1 : 0);
  put(&o, "volume_musica=%d\n", d.music_vol);
  put(&o, "volume_effetti=%d\n", d.sfx_vol);
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
  for (int i = 0; i < g_nextra; ++i) put(&o, "%s\n", g_extra[i]);
  return o.n < cap - 1 ? o.n : -1;
}

bool failed(const char* what, const char* path, int e) {
  if (!g_failed) plat_log("salvataggio non riuscito: %s %s (%s); il gioco va avanti, i progressi restano in memoria", what, path, why(e));
  g_failed = true;
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

}  // namespace

int save_open(const char* dir) {
  defaults(&g_data);
  g_init = true;
  g_nextra = 0;
  g_backup = g_failed = g_full_logged = false;
  g_last_n = -1;
  g_dir[0] = g_path[0] = g_tmp[0] = g_bad[0] = 0;
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
  snprintf(g_path, sizeof(g_path), "%s%s%s", g_dir, strcmp(g_dir, "/") ? "/" : "", FILE_NAME);
  snprintf(g_tmp, sizeof(g_tmp), "%s.tmp", g_path);
  snprintf(g_bad, sizeof(g_bad), "%s.illeggibile", g_path);

  errno = 0;
  FILE* f = fopen(g_path, "rb");
  if (!f) {
    const int e = errno;
    if (e == ENOENT) {
      snprintf(g_status, sizeof(g_status), "nuovo");
      return SAVE_NUOVO;
    }
    snprintf(g_status, sizeof(g_status), "illeggibile: %s, si riparte da capo", why(e));
    return SAVE_ILLEGGIBILE;
  }
  // A pezzi, una riga alla volta: un file troncato, binario o enorme non rompe nulla.
  static char chunk[4096];
  char line[LINE_LEN];
  int len_l = 0, total = 0;
  bool broken = false;
  Reader r = { 0, 0, 0, false, true };
  size_t got;
  while (total < MAX_READ && (got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
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
  const int read_errno = ferror(f) ? (errno ? errno : EIO) : 0;
  const bool more = total >= MAX_READ && fgetc(f) != EOF;
  fclose(f);
  if (len_l > 0 || broken) {               // ultima riga senza a capo (o tagliata qui sopra)
    line[len_l] = 0;
    read_line(&r, line, broken || more);
  }
  if (total == 0) {
    defaults(&g_data);
    if (read_errno) snprintf(g_status, sizeof(g_status), "illeggibile: %s, si riparte da capo", why(read_errno));
    else snprintf(g_status, sizeof(g_status), "illeggibile: file vuoto, si riparte da capo");
    return SAVE_ILLEGGIBILE;
  }
  if (r.ok == 0 && r.bad > 0) {
    defaults(&g_data);
    g_nextra = 0;
    g_backup = true;
    snprintf(g_status, sizeof(g_status), "illeggibile: nessuna riga valida%s, si riparte da capo; al primo salvataggio il file diventa %s.illeggibile",
             r.binary ? " (non e' un file di testo)" : "", FILE_NAME);
    return SAVE_ILLEGGIBILE;
  }
  int n = snprintf(g_status, sizeof(g_status), "letto");
  if (r.bad > 0) n += snprintf(g_status + n, sizeof(g_status) - (size_t)n, ", righe non valide ignorate: %d%s", r.bad, r.binary ? " (caratteri non di testo)" : "");
  if (r.dropped > 0 && n < (int)sizeof(g_status))
    n += snprintf(g_status + n, sizeof(g_status) - (size_t)n, ", voci sconosciute di troppo perse: %d", r.dropped);
  if (more && n < (int)sizeof(g_status)) n += snprintf(g_status + n, sizeof(g_status) - (size_t)n, ", file troppo grande: letto solo l'inizio");
  if (read_errno && n < (int)sizeof(g_status)) n += snprintf(g_status + n, sizeof(g_status) - (size_t)n, ", lettura interrotta (%s)", why(read_errno));
  if (g_data.version > SAVE_VERSION && n < (int)sizeof(g_status))
    snprintf(g_status + n, sizeof(g_status) - (size_t)n, ", scritto da una versione piu' nuova del gioco (formato %d)", g_data.version);
  g_last_n = serialize(g_last, OUT_LEN);   // i dati letti sono gia' sul disco
  return SAVE_LETTO;
}

const char* save_status(void) { return g_status; }
const char* save_path(void) { return g_path; }
SaveData* save_data(void) { return &data(); }

uint8_t save_stars(const char* map_id) {
  if (!valid_id(map_id)) return 0;
  const SaveMap* m = find_map(map_id);
  return m ? m->stars : 0;
}

void save_add_stars(const char* map_id, uint8_t bits) {
  if (!valid_id(map_id)) {
    plat_log("salvataggio: nome di mappa \"%.40s\" non valido (solo lettere, cifre, _ e -, al massimo %d): stelle non salvate",
             map_id ? map_id : "", SAVE_ID_LEN - 1);
    return;
  }
  SaveMap* m = add_map(map_id);
  if (!m) {
    if (!g_full_logged) plat_log("salvataggio: troppe mappe (al massimo %d), le stelle di %s non si salvano", SAVE_MAX_MAPS, map_id);
    g_full_logged = true;
    return;
  }
  m->stars |= (uint8_t)(bits & 7u);
}

void save_set_stars(const char* map_id, uint8_t bits) {
  if (!valid_id(map_id)) return;
  SaveMap* m = find_map(map_id);
  if (!m && bits == 0) return;            // 0 stelle: come se la mappa non ci fosse
  if (!m) m = add_map(map_id);
  if (m) m->stars = (uint8_t)(bits & 7u);
}

const char* save_world_id(int w) { return w >= 0 && w < SAVE_WORLDS ? WORLD_ID[w] : ""; }

bool save_write(void) {
  if (!g_path[0]) return false;
  const int n = serialize(g_out, OUT_LEN);
  if (n < 0) return failed("troppi dati per", g_path, 0);
  if (n == g_last_n && !memcmp(g_out, g_last, (size_t)n)) return true;   // sul disco c'e' gia' questo
  errno = 0;
  FILE* f = fopen(g_tmp, "wb");
  int e = errno;
  // RetroArch puo' dare la cartella del core non ancora creata: si crea (solo l'ultimo livello).
  if (!f && e == ENOENT && mkdir(g_dir, 0777) == 0) {
    f = fopen(g_tmp, "wb");
    e = errno;
  }
  if (!f) return failed("non riesco a creare", g_tmp, e);
  bool ok = true;
  if (fwrite(g_out, 1, (size_t)n, f) != (size_t)n || fflush(f) != 0) {
    ok = false;
    e = errno;
  } else if (fsync(fileno(f)) != 0 && errno != EINVAL) {
    // I dati devono essere sulla scheda prima di rinominare: dopo uno spegnimento il file nuovo
    // potrebbe esserci ma vuoto. EINVAL: file system senza fsync, si va avanti lo stesso.
    ok = false;
    e = errno;
  }
  if (fclose(f) != 0 && ok) {
    ok = false;
    e = errno;
  }
  if (!ok) {
    remove(g_tmp);
    return failed("non riesco a scrivere", g_tmp, e);
  }
  if (g_backup && rename(g_path, g_bad) == 0) plat_log("salvataggio: il file illeggibile resta come %s", g_bad);
  g_backup = false;
  if (rename(g_tmp, g_path) != 0) {
    e = errno;
    remove(g_tmp);
    return failed("non riesco a sostituire", g_path, e);
  }
  sync_dir();
  memcpy(g_last, g_out, (size_t)n);
  g_last_n = n;
  if (g_failed) plat_log("salvataggio di nuovo riuscito: %s", g_path);
  g_failed = false;
  return true;
}
