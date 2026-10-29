// Mappa: lettura del file di testo, zone e cancelli, collisioni, linea di vista.
// La geometria da disegnare e' in level_mesh.cpp.
#include "level.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

void fail(char* err, int errlen, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
void fail(char* err, int errlen, const char* fmt, ...) {
  if (!err || errlen <= 0) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, (size_t)errlen, fmt, ap);
  va_end(ap);
}

void trim(char* s) {
  int n = (int)strlen(s);
  while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
  int k = 0;
  while (s[k] && isspace((unsigned char)s[k])) k++;
  if (k) memmove(s, s + k, (size_t)(n - k + 1));
}

void lower(char* s) {
  for (; *s; ++s) *s = (char)tolower((unsigned char)*s);
}

// Dalla 0.5.1 il corridore e' lo scorpione e il tiratore l'arciere di terracotta; i nomi di prima
// (sciacallo, lama) valgono ancora per le mappe fatte a mano.
int monster_by_name(const char* s) {
  static const struct { const char* name; int type; } N[] = {
    { "scorpione", MO_SCORPIONE }, { "scorpioni", MO_SCORPIONE }, { "arciere", MO_ARCIERE }, { "arcieri", MO_ARCIERE },
    { "scarabeo", MO_SCARABEO }, { "scarabei", MO_SCARABEO }, { "guerriero", MO_GUERRIERO }, { "guerrieri", MO_GUERRIERO },
    { "sciacallo", MO_SCORPIONE }, { "sciacalli", MO_SCORPIONE }, { "lama", MO_ARCIERE } };
  for (const auto& n : N)
    if (!strcmp(s, n.name)) return n.type;
  return -1;
}

// "sciacallo 3, scarabei 4 | guerriero 1": ondate separate da |, gruppi separati da virgole.
bool parse_waves(ZoneDef* z, char* s, int line, char* err, int errlen) {
  z->nwaves = 0;
  for (char* w = s; w;) {
    char* bar = strchr(w, '|');
    if (bar) *bar = 0;
    if (z->nwaves >= LV_MAX_WAVES) {
      fail(err, errlen, "riga %d: al massimo %d ondate per zona", line, LV_MAX_WAVES);
      return false;
    }
    WaveDef& wd = z->wave[z->nwaves];
    memset(&wd, 0, sizeof(wd));
    for (char* g = w; g;) {
      char* comma = strchr(g, ',');
      if (comma) *comma = 0;
      char name[32];
      int count = 0;
      char tmp[64];
      snprintf(tmp, sizeof(tmp), "%.63s", g);
      trim(tmp);
      if (tmp[0]) {
        if (sscanf(tmp, "%31s %d", name, &count) != 2 || count < 1 || count > 30) {
          fail(err, errlen, "riga %d: gruppo non valido \"%s\" (scrivere per esempio: scorpioni 3)", line, tmp);
          return false;
        }
        lower(name);
        const int type = monster_by_name(name);
        if (type < 0) {
          fail(err, errlen, "riga %d: mostro sconosciuto \"%s\" (scorpione, arciere, scarabeo, guerriero)", line, name);
          return false;
        }
        if (wd.nkinds >= LV_MAX_WAVE_KINDS) {
          fail(err, errlen, "riga %d: al massimo %d gruppi per ondata", line, LV_MAX_WAVE_KINDS);
          return false;
        }
        wd.kind[wd.nkinds] = (uint8_t)type;
        wd.count[wd.nkinds] = (uint8_t)count;
        wd.nkinds++;
      }
      g = comma ? comma + 1 : 0;
    }
    if (wd.nkinds > 0) z->nwaves++;
    w = bar ? bar + 1 : 0;
  }
  return true;
}

bool passable_for_zones(uint8_t c) { return c != C_VOID && c != C_WALL && c != C_TREE && c != C_ROCK && c != C_GATE; }
bool walkable_floor(uint8_t c) { return c == C_FLOOR || c == C_PATH; }
// Caselle su cui si sta e da cui si va avanti a piedi (saltando sopra blocchi, piattaforme e
// muretti): tutte quelle delle zone tranne l'acqua e il cenote.
bool standable(uint8_t c) { return passable_for_zones(c) && c != C_WATER && c != C_PIT; }
// Sempre libere a terra (niente da scavalcare, niente da rompere).
bool free_ground(uint8_t c) { return c == C_FLOOR || c == C_PATH || c == C_BUSH || c == C_GRASS || c == C_TRAMP; }

const int D4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
const int D8[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };

inline int cell_at(const Level* L, int cx, int cz) {
  return cx < 0 || cz < 0 || cx >= L->W || cz >= L->H ? (int)C_VOID : (int)L->cell[cz][cx];
}
inline int zone_of(const Level* L, int cx, int cz) { return cx < 0 || cz < 0 || cx >= L->W || cz >= L->H ? -1 : L->zone[cz][cx]; }

// Il cenote arriva a meno di d caselle (in tutte le direzioni, diagonali comprese)?
bool pit_within(const Level* L, int cx, int cz, int d) {
  for (int z = cz - d; z <= cz + d; ++z)
    for (int x = cx - d; x <= cx + d; ++x)
      if (cell_at(L, x, z) == C_PIT) return true;
  return false;
}

// Controlli delle fughe (0.5.1), dopo zone e cancelli: tutto quello che e' disegnato dentro la
// griglia sta nelle zone; il bordo e il fuori (spazi) sono chiusi dai muri; due zone si toccano
// solo attraverso un cancello, anche in diagonale; in ogni zona si arriva a piedi dappertutto
// (l'acqua e il cenote non si attraversano) e si arriva al cancello di uscita; i muretti hanno
// un lato libero (chi ci atterra sopra ne esce); il cenote sta lontano da cancelli e partenze e
// non tocca la capsula; alla capsula ci si avvicina a terra.
bool check_leaks(const Level* L, const char (*grid)[LV_MAX_W + 1], const int* grid_line, const int* start1, char* err, int errlen) {
  const int W = L->W, H = L->H;
  // Alberi e massi vanno bene anche a boschetti (3x3 e piu'): basta che il boschetto tocchi una
  // zona. Si allarga dalle caselle accanto a una zona attraverso alberi e massi vicini.
  static uint8_t grove[LV_MAX_H][LV_MAX_W];
  static int16_t qx[LV_MAX_W * LV_MAX_H], qz[LV_MAX_W * LV_MAX_H];
  memset(grove, 0, sizeof(grove));
  int qn = 0;
  for (int cz = 0; cz < H; ++cz)
    for (int cx = 0; cx < W; ++cx) {
      if (L->cell[cz][cx] != C_TREE && L->cell[cz][cx] != C_ROCK) continue;
      bool near = false;
      for (int k = 0; k < 8 && !near; ++k) near = zone_of(L, cx + D8[k][0], cz + D8[k][1]) >= 0;
      if (!near) continue;
      grove[cz][cx] = 1;
      qx[qn] = (int16_t)cx;
      qz[qn] = (int16_t)cz;
      qn++;
    }
  for (int q = 0; q < qn; ++q)
    for (int k = 0; k < 8; ++k) {
      const int x = qx[q] + D8[k][0], z = qz[q] + D8[k][1];
      if (x < 0 || z < 0 || x >= W || z >= H || grove[z][x]) continue;
      if (L->cell[z][x] != C_TREE && L->cell[z][x] != C_ROCK) continue;
      grove[z][x] = 1;
      qx[qn] = (int16_t)x;
      qz[qn] = (int16_t)z;
      qn++;
    }
  for (int cz = 0; cz < H; ++cz)
    for (int cx = 0; cx < W; ++cx) {
      const int c = L->cell[cz][cx];
      const char ch = cx < (int)strlen(grid[cz]) ? grid[cz][cx] : ' ';
      if ((cx == 0 || cz == 0 || cx == W - 1 || cz == H - 1) && c != C_WALL && c != C_VOID) {
        fail(err, errlen, "riga %d, colonna %d: sul bordo serve il muro #", grid_line[cz], cx + 1);
        return false;
      }
      if (c == C_WALL || c == C_VOID) continue;
      for (int k = 0; k < 8; ++k)
        if (cx + D8[k][0] >= 0 && cz + D8[k][1] >= 0 && cx + D8[k][0] < W && cz + D8[k][1] < H &&
            L->cell[cz + D8[k][1]][cx + D8[k][0]] == C_VOID) {
          fail(err, errlen, "riga %d, colonna %d: manca il muro verso il fuori", grid_line[cz], cx + 1);
          return false;
        }
      const int z = L->zone[cz][cx];
      if (c == C_TREE || c == C_ROCK) {
        if (!grove[cz][cx]) {
          fail(err, errlen, "riga %d, colonna %d: '%c' fuori dalle zone", grid_line[cz], cx + 1, ch);
          return false;
        }
        continue;
      }
      if (c == C_GATE) {
        const Gate& gt = L->gates[L->gate_at[cz][cx]];
        int to = -1;
        for (int k = 0; k < L->nzones; ++k)
          if (L->zones[k].entry_gate == L->gate_at[cz][cx]) to = k;
        for (int k = 0; k < 8; ++k) {
          const int o = zone_of(L, cx + D8[k][0], cz + D8[k][1]);
          if (o >= 0 && o != gt.from_zone && o != to) {
            fail(err, errlen, "riga %d, colonna %d: il cancello tocca la zona %d", grid_line[cz], cx + 1, o + 1);
            return false;
          }
        }
        continue;
      }
      if (z < 0) {
        fail(err, errlen, "riga %d, colonna %d: '%c' fuori dalle zone", grid_line[cz], cx + 1, ch);
        return false;
      }
      for (int k = 0; k < 8; ++k) {
        const int o = zone_of(L, cx + D8[k][0], cz + D8[k][1]);
        if (o >= 0 && o != z) {
          fail(err, errlen, "riga %d, colonna %d: la zona %d tocca la %d (serve G)", grid_line[cz], cx + 1, z + 1, o + 1);
          return false;
        }
      }
      if (c == C_LOW) {
        bool side = false;
        for (int k = 0; k < 4; ++k) side |= free_ground((uint8_t)cell_at(L, cx + D4[k][0], cz + D4[k][1]));
        if (!side) {
          fail(err, errlen, "riga %d, colonna %d: muretto = senza un lato libero", grid_line[cz], cx + 1);
          return false;
        }
      }
      if (c == C_PIT) {
        bool gate = false, start = false;
        for (int y = cz - 2; y <= cz + 2; ++y)
          for (int x = cx - 2; x <= cx + 2; ++x) {
            if (cell_at(L, x, y) == C_GATE) gate = true;
            if (x >= 0 && y >= 0 && x < W && y < H && x < (int)strlen(grid[y]) && grid[y][x] >= '1' && grid[y][x] <= '4') start = true;
          }
        if (gate || start) {
          fail(err, errlen, gate ? "riga %d, colonna %d: cenote O troppo vicino a un G" : "riga %d, colonna %d: cenote O vicino alla partenza", grid_line[cz],
               cx + 1);
          return false;
        }
        for (int y = cz - 1; y <= cz + 1; ++y)
          for (int x = cx - 1; x <= cx + 1; ++x)
            if (x >= 0 && y >= 0 && x < W && y < H && L->dyn_at[y][x] >= 0 && L->dyn_at[y][x] == L->capsule_dyn) {
              fail(err, errlen, "riga %d, colonna %d: cenote O accanto alla capsula p", grid_line[cz], cx + 1);
              return false;
            }
      }
    }
  // A piedi: da dove si entra nella zona (partenza 1 o cancello d'ingresso) si arriva a tutte le
  // caselle su cui si sta e al cancello di uscita.
  static uint8_t seen[LV_MAX_H][LV_MAX_W];
  static int16_t q[LV_MAX_W * LV_MAX_H][2];
  for (int k = 0; k < L->nzones; ++k) {
    memset(seen, 0, sizeof(seen));
    int qn = 0, qh = 0;
    auto seed = [&](int x, int y) {
      if (zone_of(L, x, y) != k || !standable(L->cell[y][x]) || seen[y][x]) return;
      seen[y][x] = 1;
      q[qn][0] = (int16_t)x;
      q[qn][1] = (int16_t)y;
      qn++;
    };
    const ZoneDef& zd = L->zones[k];
    if (k == 0) {
      seed(start1[0], start1[1]);
    } else {
      const Gate& gt = L->gates[zd.entry_gate];
      for (int y = gt.z0; y <= gt.z1; ++y)
        for (int x = gt.x0; x <= gt.x1; ++x)
          for (int d = 0; d < 4; ++d) seed(x + D4[d][0], y + D4[d][1]);
    }
    while (qh < qn) {
      const int x = q[qh][0], y = q[qh][1];
      qh++;
      for (int d = 0; d < 4; ++d) seed(x + D4[d][0], y + D4[d][1]);
    }
    for (int cz = zd.z0; cz <= zd.z1; ++cz)
      for (int cx = zd.x0; cx <= zd.x1; ++cx)
        if (L->zone[cz][cx] == k && standable(L->cell[cz][cx]) && !seen[cz][cx]) {
          fail(err, errlen, "riga %d, colonna %d: non ci si arriva a piedi", grid_line[cz], cx + 1);
          return false;
        }
    if (zd.exit_gate >= 0) {
      const Gate& gt = L->gates[zd.exit_gate];
      bool reach = false;
      for (int y = gt.z0; y <= gt.z1; ++y)
        for (int x = gt.x0; x <= gt.x1; ++x)
          for (int d = 0; d < 4; ++d) {
            const int nx = x + D4[d][0], ny = y + D4[d][1];
            if (zone_of(L, nx, ny) == k && seen[ny][nx] && free_ground(L->cell[ny][nx])) reach = true;
          }
      if (!reach) {
        fail(err, errlen, "riga %d, colonna %d: al cancello non si arriva a piedi", grid_line[gt.z0], gt.x0 + 1);
        return false;
      }
    }
  }
  if (L->capsule_dyn >= 0) {
    const DynCell& c = L->dyn[L->capsule_dyn];
    bool ok = false;
    for (int d = 0; d < 4; ++d) {
      const int x = c.cx + D4[d][0], y = c.cz + D4[d][1];
      if (zone_of(L, x, y) == L->zone[c.cz][c.cx] && free_ground((uint8_t)cell_at(L, x, y))) ok = true;
    }
    if (!ok) {
      fail(err, errlen, "riga %d, colonna %d: capsula p senza un lato libero", grid_line[c.cz], c.cx + 1);
      return false;
    }
  }
  return true;
}

// Riempie la zona z partendo dalle caselle in coda (4 vicini, non attraversa muri e cancelli).
int flood_zone(Level* L, int z, int16_t (*queue)[2], int qn) {
  int head = 0, n = 0;
  while (head < qn) {
    const int cx = queue[head][0], cz = queue[head][1];
    head++;
    n++;
    static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int k = 0; k < 4; ++k) {
      const int x = cx + D[k][0], y = cz + D[k][1];
      if (x < 0 || y < 0 || x >= L->W || y >= L->H) continue;
      if (L->zone[y][x] >= 0 || !passable_for_zones(L->cell[y][x])) continue;
      L->zone[y][x] = (int8_t)z;
      queue[qn][0] = (int16_t)x;
      queue[qn][1] = (int16_t)y;
      qn++;
    }
  }
  return n;
}

}  // namespace

bool level_parse(Level* L, const char* text, char* err, int errlen) {
  memset(L, 0, sizeof(*L));
  memset(L->gate_at, -1, sizeof(L->gate_at));
  L->capsule_dyn = -1;
  L->boss = BOSS_NONE;
  L->world = WORLD_TERRA;
  L->style = -1;
  snprintf(L->name, sizeof(L->name), "senza nome");
  if (err && errlen > 0) err[0] = 0;

  // --- intestazione e griglia ---
  static char grid[LV_MAX_H][LV_MAX_W + 1];
  int grid_line[LV_MAX_H];
  int H = 0, W = 0, line = 0;
  bool in_grid = false;
  bool zone_given[LV_MAX_ZONES] = { false, false, false, false, false, false };
  const char* p = text;
  while (*p) {
    const char* e = p;
    while (*e && *e != '\n') e++;
    line++;
    char buf[256];
    int n = (int)(e - p);
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    memcpy(buf, p, (size_t)n);
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == ' ' || buf[n - 1] == '\t')) buf[--n] = 0;
    p = *e ? e + 1 : e;
    if (!in_grid) {
      char t[256];
      snprintf(t, sizeof(t), "%s", buf);
      trim(t);
      if (!t[0] || t[0] == '#') continue;
      if (!strcmp(t, "---")) { in_grid = true; continue; }
      char* colon = strchr(t, ':');
      if (!colon) {
        fail(err, errlen, "riga %d: manca ':' (oppure la riga --- prima della griglia)", line);
        return false;
      }
      *colon = 0;
      char key[64], val[192];
      snprintf(key, sizeof(key), "%.63s", t);
      snprintf(val, sizeof(val), "%.191s", colon + 1);
      trim(key);
      trim(val);
      lower(key);
      int zn = 0;
      if (!strcmp(key, "nome")) {
        snprintf(L->name, sizeof(L->name), "%.39s", val);
      } else if (!strcmp(key, "mondo")) {
        lower(val);
        static const char* const WN[NWORLDS] = { "terra", "luna", "marte", "venere", "mercurio", "giove", "saturno", "urano", "nettuno" };
        L->world = -1;
        for (int k = 0; k < NWORLDS; ++k)
          if (!strcmp(val, WN[k])) L->world = k;
        if (L->world < 0) {
          fail(err, errlen, "riga %d: mondo sconosciuto \"%s\" (terra; gli altri pianeti arrivano con le prossime versioni)", line, val);
          return false;
        }
      } else if (!strcmp(key, "stile")) {
        lower(val);
        static const char* const SN[NSTYLES] = { "piramidi", "chichenitza", "muraglia", "colosseo" };
        L->style = -1;
        for (int k = 0; k < NSTYLES; ++k)
          if (!strcmp(val, SN[k])) L->style = k;
        if (L->style < 0) {
          fail(err, errlen, "riga %d: stile sconosciuto \"%s\" (piramidi, chichenitza, muraglia, colosseo)", line, val);
          return false;
        }
      } else if (!strcmp(key, "boss")) {
        lower(val);
        if (!strcmp(val, "gladiatore")) L->boss = BOSS_GLADIATORE;
        else {
          fail(err, errlen, "riga %d: boss sconosciuto \"%s\" (gladiatore)", line, val);
          return false;
        }
      } else if (sscanf(key, "zona %d", &zn) == 1) {
        if (zn < 1 || zn > LV_MAX_ZONES) {
          fail(err, errlen, "riga %d: le zone vanno da 1 a %d", line, LV_MAX_ZONES);
          return false;
        }
        if (!parse_waves(&L->zones[zn - 1], val, line, err, errlen)) return false;
        zone_given[zn - 1] = true;
      } else {
        fail(err, errlen, "riga %d: voce sconosciuta \"%s\" (nome, mondo, stile, boss, zona N)", line, key);
        return false;
      }
      continue;
    }
    if (H >= LV_MAX_H) {
      fail(err, errlen, "la griglia ha più di %d righe", LV_MAX_H);
      return false;
    }
    if (n > LV_MAX_W) {
      fail(err, errlen, "riga %d: la griglia è larga più di %d caselle", line, LV_MAX_W);
      return false;
    }
    memcpy(grid[H], buf, (size_t)n + 1);
    grid_line[H] = line;
    H++;
    if (n > W) W = n;
  }
  while (H > 0 && grid[H - 1][0] == 0) H--;    // righe vuote in fondo
  if (!in_grid) {
    fail(err, errlen, "manca la riga --- che separa l'intestazione dalla griglia");
    return false;
  }
  if (W < 8 || H < 8) {
    fail(err, errlen, "griglia troppo piccola (%dx%d, servono almeno 8x8)", W, H);
    return false;
  }
  L->W = W;
  L->H = H;

  // --- caselle ---
  bool have_start[4] = { false, false, false, false };
  int start_c[4][2];
  int nboss = 0;
  memset(L->zone, -1, sizeof(L->zone));
  for (int z = 0; z < LV_MAX_H; ++z)
    for (int x = 0; x < LV_MAX_W; ++x) L->dyn_at[z][x] = -1;
  struct Pending { char ch; int16_t cx, cz; };
  static Pending pend[LV_MAX_W * LV_MAX_H];
  int npend = 0;
  for (int cz = 0; cz < H; ++cz) {
    const int len = (int)strlen(grid[cz]);
    for (int cx = 0; cx < W; ++cx) {
      const char ch = cx < len ? grid[cz][cx] : ' ';
      uint8_t t;
      switch (ch) {
        case ' ': t = C_VOID; break;
        case '.': t = C_FLOOR; break;
        case ',': t = C_PATH; break;
        case '#': t = C_WALL; break;
        case 't': t = C_TREE; break;
        case 'o': t = C_ROCK; break;
        case '=': t = C_LOW; break;
        case 'c': t = C_CRATE; break;
        case 'x': case 'X': case 'p': t = C_BREAK; break;
        case '*': t = C_BUSH; break;
        case '"': t = C_GRASS; break;
        case '~': t = C_WATER; break;
        case 'O': t = C_PIT; break;
        case 'T': t = C_TRAMP; break;
        case 'P': case 'Q': t = C_PLATFORM; break;
        case 'G': t = C_GATE; break;
        case '+': case 'b': case 'm': case 'B': t = C_FLOOR; break;
        case '1': case '2': case '3': case '4': t = C_PATH; break;     // partenze: piazzola di terra
        default:
          fail(err, errlen, "riga %d, colonna %d: carattere sconosciuto '%c'", grid_line[cz], cx + 1, ch);
          return false;
      }
      L->cell[cz][cx] = t;
      if (ch >= '1' && ch <= '4') {
        const int i = ch - '1';
        if (have_start[i]) {
          fail(err, errlen, "riga %d: la partenza del giocatore %c compare due volte", grid_line[cz], ch);
          return false;
        }
        have_start[i] = true;
        start_c[i][0] = cx;
        start_c[i][1] = cz;
      }
      if (ch == 'B') {
        nboss++;
        L->boss_pos = cell_center(L, cx, cz);
      }
      if (ch == '+' || ch == 'b' || ch == 'm' || ch == 'X' || ch == 'Q' || ch == 'x') {
        pend[npend].ch = ch;
        pend[npend].cx = (int16_t)cx;
        pend[npend].cz = (int16_t)cz;
        npend++;
      }
      if (ch == 'p' && L->capsule_dyn >= 0) {
        const DynCell& c = L->dyn[L->capsule_dyn];
        fail(err, errlen, "riga %d, colonna %d: seconda capsula p (la prima è a riga %d): una sola per mappa", grid_line[cz], cx + 1,
             grid_line[c.cz]);
        return false;
      }
      if (t == C_BUSH || t == C_GRASS || t == C_BREAK || t == C_TRAMP) {
        if (L->ndyn >= LV_MAX_DYN) {
          fail(err, errlen, "troppi cespugli, ciuffi d'erba, casse e trampolini (al massimo %d)", LV_MAX_DYN);
          return false;
        }
        DynCell& d = L->dyn[L->ndyn];
        d.type = t;
        d.capsule = ch == 'p';                    // la capsula e' una cassa 'x' piu' robusta
        d.cx = (int16_t)cx;
        d.cz = (int16_t)cz;
        d.hp = dyn_hp_max(d);
        if (d.capsule) L->capsule_dyn = (int16_t)L->ndyn;
        L->dyn_at[cz][cx] = (int16_t)L->ndyn;
        L->ndyn++;
      }
    }
  }
  if (!have_start[0]) {
    fail(err, errlen, "manca la partenza del giocatore 1 (carattere 1)");
    return false;
  }
  if (L->boss != BOSS_NONE && nboss != 1) {
    fail(err, errlen, "un'arena con il boss deve avere una casella B (ne ha %d)", nboss);
    return false;
  }
  if (L->boss == BOSS_NONE && nboss > 0) {
    fail(err, errlen, "c'è una casella B ma manca la riga \"boss: gladiatore\"");
    return false;
  }

  // --- zone e cancelli ---
  static int16_t queue[LV_MAX_W * LV_MAX_H][2];
  int qn = 0;
  L->zone[start_c[0][1]][start_c[0][0]] = 0;
  queue[0][0] = (int16_t)start_c[0][0];
  queue[0][1] = (int16_t)start_c[0][1];
  flood_zone(L, 0, queue, 1);
  for (int i = 1; i < 4; ++i)
    if (have_start[i] && L->zone[start_c[i][1]][start_c[i][0]] != 0) {
      fail(err, errlen, "la partenza %d non è nella stessa zona della partenza 1", i + 1);
      return false;
    }
  L->nzones = 1;
  // Componenti dei cancelli.
  for (int cz = 0; cz < H; ++cz)
    for (int cx = 0; cx < W; ++cx) {
      if (L->cell[cz][cx] != C_GATE || L->gate_at[cz][cx] >= 0) continue;
      if (L->ngates >= LV_MAX_GATES) {
        fail(err, errlen, "troppi cancelli (al massimo %d)", LV_MAX_GATES);
        return false;
      }
      const int gi = L->ngates++;
      Gate& gt = L->gates[gi];
      gt.x0 = gt.x1 = cx;
      gt.z0 = gt.z1 = cz;
      gt.from_zone = -1;
      int gq = 0, gh = 0;
      queue[gq][0] = (int16_t)cx;
      queue[gq][1] = (int16_t)cz;
      gq++;
      L->gate_at[cz][cx] = (int8_t)gi;
      while (gh < gq) {
        const int x = queue[gh][0], y = queue[gh][1];
        gh++;
        if (x < gt.x0) gt.x0 = x;
        if (x > gt.x1) gt.x1 = x;
        if (y < gt.z0) gt.z0 = y;
        if (y > gt.z1) gt.z1 = y;
        static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int k = 0; k < 4; ++k) {
          const int nx = x + D[k][0], ny = y + D[k][1];
          if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
          if (L->cell[ny][nx] != C_GATE || L->gate_at[ny][nx] >= 0) continue;
          L->gate_at[ny][nx] = (int8_t)gi;
          queue[gq][0] = (int16_t)nx;
          queue[gq][1] = (int16_t)ny;
          gq++;
        }
      }
    }
  for (int z = 0; z < LV_MAX_ZONES; ++z) L->zones[z].entry_gate = L->zones[z].exit_gate = -1;
  // Dalla zona k, il cancello che la tocca e non e' ancora assegnato porta alla zona k+1.
  for (int k = 0; k < L->nzones; ++k) {
    for (int gi = 0; gi < L->ngates; ++gi) {
      Gate& gt = L->gates[gi];
      if (gt.from_zone >= 0 || L->zones[k].entry_gate == gi) continue;
      bool touches = false;
      qn = 0;
      for (int y = gt.z0 - 1; y <= gt.z1 + 1; ++y)
        for (int x = gt.x0 - 1; x <= gt.x1 + 1; ++x) {
          if (x < 0 || y < 0 || x >= W || y >= H) continue;
          if ((x < gt.x0 || x > gt.x1) && (y < gt.z0 || y > gt.z1)) continue;   // niente diagonali
          if (L->zone[y][x] == k) touches = true;
        }
      if (!touches) continue;
      if (L->zones[k].exit_gate >= 0) {
        fail(err, errlen, "la zona %d ha più di un cancello di uscita: le zone vanno in fila", k + 1);
        return false;
      }
      if (L->nzones >= LV_MAX_ZONES) {
        fail(err, errlen, "troppe zone (al massimo %d)", LV_MAX_ZONES);
        return false;
      }
      const int nz = L->nzones;
      // La zona nuova si riempie da una sola casella oltre il cancello; tutte le altre caselle
      // oltre il cancello devono essere raggiunte, altrimenti la zona e' divisa in pezzi separati
      // (i mostri potrebbero comparire dove i giocatori non arrivano).
      int seeds[2 * (LV_MAX_W + LV_MAX_H)][2], ns = 0;
      for (int y = gt.z0 - 1; y <= gt.z1 + 1; ++y)
        for (int x = gt.x0 - 1; x <= gt.x1 + 1; ++x) {
          if (x < 0 || y < 0 || x >= W || y >= H) continue;
          if ((x < gt.x0 || x > gt.x1) && (y < gt.z0 || y > gt.z1)) continue;
          if (L->zone[y][x] >= 0 || !passable_for_zones(L->cell[y][x])) continue;
          if (ns < (int)(sizeof(seeds) / sizeof(seeds[0]))) {
            seeds[ns][0] = x;
            seeds[ns][1] = y;
            ns++;
          }
        }
      if (ns == 0) {
        fail(err, errlen, "il cancello vicino a riga %d, colonna %d non porta da nessuna parte", grid_line[gt.z0], gt.x0 + 1);
        return false;
      }
      L->zone[seeds[0][1]][seeds[0][0]] = (int8_t)nz;
      queue[0][0] = (int16_t)seeds[0][0];
      queue[0][1] = (int16_t)seeds[0][1];
      flood_zone(L, nz, queue, 1);
      for (int q = 1; q < ns; ++q)
        if (L->zone[seeds[q][1]][seeds[q][0]] != nz) {
          fail(err, errlen, "oltre il cancello vicino a riga %d, colonna %d la zona %d è divisa in parti separate",
               grid_line[gt.z0], gt.x0 + 1, nz + 1);
          return false;
        }
      gt.from_zone = k;
      L->zones[k].exit_gate = gi;
      L->zones[nz].entry_gate = gi;
      L->nzones++;
    }
  }
  for (int gi = 0; gi < L->ngates; ++gi)
    if (L->gates[gi].from_zone < 0) {
      fail(err, errlen, "il cancello vicino a riga %d, colonna %d non si raggiunge dalla partenza", grid_line[L->gates[gi].z0],
           L->gates[gi].x0 + 1);
      return false;
    }
  for (int k = 0; k < L->nzones; ++k) {
    if (L->boss == BOSS_NONE && (!zone_given[k] || L->zones[k].nwaves == 0)) {
      fail(err, errlen, "manca la riga \"zona %d: ...\" con i mostri della zona", k + 1);
      return false;
    }
  }
  if (L->boss != BOSS_NONE) {
    int bx, bz;
    if (!level_cell(L, L->boss_pos.x, L->boss_pos.z, &bx, &bz) || L->zone[bz][bx] != L->nzones - 1) {
      fail(err, errlen, "il boss (casella B) deve stare nell'ultima zona della mappa");
      return false;
    }
  }
  for (int k = L->nzones; k < LV_MAX_ZONES; ++k)
    if (zone_given[k]) {
      fail(err, errlen, "c'è la riga \"zona %d\" ma la griglia ha solo %d zone", k + 1, L->nzones);
      return false;
    }

  // Estensione delle zone.
  for (int k = 0; k < L->nzones; ++k) {
    ZoneDef& zd = L->zones[k];
    zd.x0 = W; zd.z0 = H; zd.x1 = -1; zd.z1 = -1;
  }
  for (int cz = 0; cz < H; ++cz)
    for (int cx = 0; cx < W; ++cx) {
      const int k = L->zone[cz][cx];
      if (k < 0) continue;
      ZoneDef& zd = L->zones[k];
      if (cx < zd.x0) zd.x0 = cx;
      if (cx > zd.x1) zd.x1 = cx;
      if (cz < zd.z0) zd.z0 = cz;
      if (cz > zd.z1) zd.z1 = cz;
    }
  for (int k = 0; k < L->nzones; ++k) {
    ZoneDef& zd = L->zones[k];
    zd.wx0 = (float)zd.x0 - (float)W * 0.5f;
    zd.wx1 = (float)zd.x1 + 1.0f - (float)W * 0.5f;
    zd.wz0 = (float)zd.z0 - (float)H;
    zd.wz1 = (float)zd.z1 + 1.0f - (float)H;
  }
  for (int i = 0; i < L->ndyn; ++i) {
    const int k = L->zone[L->dyn[i].cz][L->dyn[i].cx];
    L->dyn[i].zone = (uint8_t)(k < 0 ? 0 : k);
  }
  // La capsula deve stare dove si arriva e non accanto a un cancello (chiuderebbe il passaggio).
  if (L->capsule_dyn >= 0) {
    const DynCell& c = L->dyn[L->capsule_dyn];
    if (L->zone[c.cz][c.cx] < 0) {
      fail(err, errlen, "riga %d, colonna %d: la capsula p è fuori dalle zone raggiungibili", grid_line[c.cz], c.cx + 1);
      return false;
    }
    for (int z = c.cz - 1; z <= c.cz + 1; ++z)
      for (int x = c.cx - 1; x <= c.cx + 1; ++x)
        if (x >= 0 && z >= 0 && x < W && z < H && L->cell[z][x] == C_GATE) {
          fail(err, errlen, "riga %d, colonna %d: la capsula p non può stare accanto a un cancello", grid_line[c.cz], c.cx + 1);
          return false;
        }
  }
  if (!check_leaks(L, grid, grid_line, start_c[0], err, errlen)) return false;

  // --- oggetti, comparse ---
  // Due giri: prima talismani, bombe e comparse dei mostri, poi i bonus nelle casse 'x' (0.5.4), che
  // prendono i posti che restano (una cassa fuori dalle zone, o una di troppo, resta vuota).
  for (int pass = 0; pass < 2; ++pass)
  for (int i = 0; i < npend; ++i) {
    const Pending& pd = pend[i];
    const int k = L->zone[pd.cz][pd.cx];
    if ((pd.ch == 'x') != (pass == 1)) continue;
    if (pd.ch == 'x' && (k < 0 || L->nitems >= LV_MAX_ITEMS)) continue;
    if (k < 0) {
      fail(err, errlen, "riga %d, colonna %d: '%c' fuori dalle zone raggiungibili", grid_line[pd.cz], pd.cx + 1, pd.ch);
      return false;
    }
    if (pd.ch == 'm') {
      ZoneDef& zd = L->zones[k];
      if (zd.nspawn < LV_MAX_SPAWN) {
        zd.spawn[zd.nspawn][0] = pd.cx;
        zd.spawn[zd.nspawn][1] = pd.cz;
        zd.nspawn++;
      }
      continue;
    }
    if (L->nitems >= LV_MAX_ITEMS) {
      fail(err, errlen, "troppi talismani e bombe (al massimo %d)", LV_MAX_ITEMS);
      return false;
    }
    MapItem& it = L->items[L->nitems++];
    it.kind = pd.ch == 'b' ? IT_BOMB : (pd.ch == 'x' ? IT_POWER : IT_TALISMAN);
    it.zone = (uint8_t)k;
    it.cx = pd.cx;
    it.cz = pd.cz;
    it.crate = pd.ch == 'X' || pd.ch == 'x' ? L->dyn_at[pd.cz][pd.cx] : (int16_t)-1;
    it.y = pd.ch == 'Q' ? PLAT_H : 0.0f;
  }

  // Partenze: zona 1 dalle caselle 1-4 (quelle che mancano sulle caselle libere piu' vicine
  // alla partenza 1), le altre zone appena dentro il cancello d'ingresso.
  static int16_t dist[LV_MAX_H][LV_MAX_W];
  {
    ZoneDef& z0 = L->zones[0];
    bool taken[LV_MAX_H][LV_MAX_W];
    memset(taken, 0, sizeof(taken));
    for (int i = 0; i < 4; ++i)
      if (have_start[i]) {
        z0.start[i] = cell_center(L, start_c[i][0], start_c[i][1]);
        taken[start_c[i][1]][start_c[i][0]] = true;
      }
    for (int it = 0; it < L->nitems; ++it) taken[L->items[it].cz][L->items[it].cx] = true;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) dist[y][x] = -1;
    int qh = 0;
    qn = 1;
    queue[0][0] = (int16_t)start_c[0][0];
    queue[0][1] = (int16_t)start_c[0][1];
    dist[start_c[0][1]][start_c[0][0]] = 0;
    for (int i = 1; i < 4; ++i) {
      if (have_start[i]) continue;
      bool found = false;
      while (qh < qn && !found) {
        const int x = queue[qh][0], y = queue[qh][1];
        qh++;
        if (!taken[y][x] && walkable_floor(L->cell[y][x]) && !pit_within(L, x, y, 1)) {   // mai accanto al cenote
          z0.start[i] = cell_center(L, x, y);
          taken[y][x] = true;
          found = true;
        }
        static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int d = 0; d < 4; ++d) {
          const int nx = x + D[d][0], ny = y + D[d][1];
          if (nx < 0 || ny < 0 || nx >= W || ny >= H || dist[ny][nx] >= 0 || L->zone[ny][nx] != 0) continue;
          dist[ny][nx] = (int16_t)(dist[y][x] + 1);
          queue[qn][0] = (int16_t)nx;
          queue[qn][1] = (int16_t)ny;
          qn++;
        }
      }
      if (!found) z0.start[i] = z0.start[0];
    }
  }
  for (int k = 1; k < L->nzones; ++k) {
    ZoneDef& zd = L->zones[k];
    const Gate& gt = L->gates[zd.entry_gate];
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) dist[y][x] = -1;
    int qh = 0;
    qn = 0;
    for (int y = gt.z0; y <= gt.z1; ++y)
      for (int x = gt.x0; x <= gt.x1; ++x) {
        dist[y][x] = 0;
        queue[qn][0] = (int16_t)x;
        queue[qn][1] = (int16_t)y;
        qn++;
      }
    int got = 0;
    for (int layer = 2; layer < 12 && got < 4; ++layer) {
      // Caselle libere a distanza "layer" dal cancello, dalla piu' centrale.
      while (qh < qn) {
        const int x = queue[qh][0], y = queue[qh][1];
        if (dist[y][x] >= layer) break;
        qh++;
        static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int d = 0; d < 4; ++d) {
          const int nx = x + D[d][0], ny = y + D[d][1];
          if (nx < 0 || ny < 0 || nx >= W || ny >= H || dist[ny][nx] >= 0) continue;
          if (L->zone[ny][nx] != k) continue;
          dist[ny][nx] = (int16_t)(dist[y][x] + 1);
          queue[qn][0] = (int16_t)nx;
          queue[qn][1] = (int16_t)ny;
          qn++;
        }
      }
      const float gcx = 0.5f * (float)(gt.x0 + gt.x1), gcz = 0.5f * (float)(gt.z0 + gt.z1);
      for (int pick = got; pick < 4; ++pick) {
        int bx = -1, by = -1;
        float bd = 1e30f;
        for (int q = 0; q < qn; ++q) {
          const int x = queue[q][0], y = queue[q][1];
          if (dist[y][x] != layer || !walkable_floor(L->cell[y][x]) || pit_within(L, x, y, 1)) continue;
          bool used = false;
          for (int u = 0; u < got; ++u) {
            const Vec3 c = cell_center(L, x, y);
            if (fabsf(zd.start[u].x - c.x) < 0.5f && fabsf(zd.start[u].z - c.z) < 0.5f) used = true;
          }
          bool item = false;
          for (int it = 0; it < L->nitems; ++it)
            if (L->items[it].cx == x && L->items[it].cz == y) item = true;
          if (used || item) continue;
          const float d = fabsf((float)x - gcx) + fabsf((float)y - gcz);
          if (d < bd) { bd = d; bx = x; by = y; }
        }
        if (bx < 0) break;
        zd.start[got++] = cell_center(L, bx, by);
      }
    }
    if (got == 0) {
      fail(err, errlen, "la zona %d non ha caselle libere vicino al cancello d'ingresso", k + 1);
      return false;
    }
    for (int i = got; i < 4; ++i) zd.start[i] = zd.start[i % got];
  }

  // Comparse mancanti: caselle libere lontane dall'ingresso della zona.
  for (int k = 0; k < L->nzones; ++k) {
    ZoneDef& zd = L->zones[k];
    if (zd.nspawn > 0) continue;
    const Vec3 ref = zd.start[0];
    float far = 0.0f;
    for (int y = zd.z0; y <= zd.z1; ++y)
      for (int x = zd.x0; x <= zd.x1; ++x)
        if (L->zone[y][x] == k && walkable_floor(L->cell[y][x])) {
          const Vec3 c = cell_center(L, x, y);
          const float d = fabsf(c.x - ref.x) + fabsf(c.z - ref.z);
          if (d > far) far = d;
        }
    int cand = 0;
    for (int pass = 0; pass < 2; ++pass)
      for (int y = zd.z0; y <= zd.z1; ++y)
        for (int x = zd.x0; x <= zd.x1; ++x) {
          if (L->zone[y][x] != k || !walkable_floor(L->cell[y][x])) continue;
          const Vec3 c = cell_center(L, x, y);
          if (fabsf(c.x - ref.x) + fabsf(c.z - ref.z) < 0.55f * far) continue;
          if (pass == 0) { cand++; continue; }
          const int stride = cand / LV_MAX_SPAWN + 1;
          if ((x * 7 + y * 13) % stride != 0 || zd.nspawn >= LV_MAX_SPAWN) continue;
          zd.spawn[zd.nspawn][0] = (int16_t)x;
          zd.spawn[zd.nspawn][1] = (int16_t)y;
          zd.nspawn++;
        }
    if (zd.nspawn == 0) {
      zd.spawn[0][0] = (int16_t)(zd.x0 + zd.x1) / 2;
      zd.spawn[0][1] = (int16_t)zd.z0;
      zd.nspawn = 1;
    }
  }
  for (int gi = 0; gi < L->ngates; ++gi) L->gates[gi].open = 0.0f;
  if (L->style < 0) L->style = L->boss != BOSS_NONE ? STYLE_COLOSSEO : STYLE_PIRAMIDI;   // senza riga "stile:"
  return true;
}

void level_free(Level* L) {
  for (int i = 0; i < L->nchunks; ++i) mesh_free(&L->chunk[i]);
  L->nchunks = 0;
  for (int i = 0; i < L->nscenery; ++i) mesh_free(&L->scenery[i]);
  L->nscenery = 0;
  for (int i = 0; i < DM_COUNT; ++i) mesh_free(&L->dyn_mesh[i]);
  for (int i = 0; i < L->ngates; ++i) mesh_free(&L->gates[i].mesh);
}

void level_reset_dynamic(Level* L, int z) {
  for (int i = 0; i < L->ndyn; ++i)
    if (z < 0 || L->dyn[i].zone == z) L->dyn[i].hp = dyn_hp_max(L->dyn[i]);   // anche la capsula
  for (int i = 0; i < L->ngates; ++i)
    if (z < 0 || L->gates[i].from_zone >= z) L->gates[i].open = 0.0f;
}

// --- collisioni -------------------------------------------------------------

bool level_cell(const Level* L, float x, float z, int* cx, int* cz) {
  const float fx = x + (float)L->W * 0.5f, fz = z + (float)L->H;
  if (fx < 0.0f || fz < 0.0f) return false;
  const int ix = (int)fx, iz = (int)fz;
  if (ix >= L->W || iz >= L->H) return false;
  *cx = ix;
  *cz = iz;
  return true;
}

int level_type_at(const Level* L, float x, float z) {
  int cx, cz;
  return level_cell(L, x, z, &cx, &cz) ? (int)L->cell[cz][cx] : (int)C_VOID;
}

int level_zone_at(const Level* L, float x, float z) {
  int cx, cz;
  return level_cell(L, x, z, &cx, &cz) ? L->zone[cz][cx] : -1;
}

static inline bool gate_closed(const Level* L, int cx, int cz) {
  const int gi = L->gate_at[cz][cx];
  return gi < 0 || L->gates[gi].open < 0.95f;
}
static inline bool crate_whole(const Level* L, int cx, int cz) {
  const int d = L->dyn_at[cz][cx];
  return d >= 0 && L->dyn[d].hp > 0.0f;
}

float level_walk_top(const Level* L, int cx, int cz) {
  if (cx < 0 || cz < 0 || cx >= L->W || cz >= L->H) return BLOCK_ALL;
  switch (L->cell[cz][cx]) {
    // L'acqua non si passa nemmeno saltando (0.5.1: prima arrivava a 0,5 m e chi saltava ci
    // finiva dentro, e da li' le spinte lo facevano uscire dall'altra parte dei muri).
    case C_VOID: case C_WALL: case C_TREE: case C_ROCK: case C_WATER: return BLOCK_ALL;
    case C_GATE: return gate_closed(L, cx, cz) ? BLOCK_ALL : 0.0f;
    case C_LOW: return LOW_H;
    case C_CRATE: return CRATE_H;
    case C_BREAK: return crate_whole(L, cx, cz) ? CRATE_H : 0.0f;
    case C_TRAMP: return TRAMP_H;
    case C_PLATFORM: return PLAT_H;
    default: return 0.0f;       // anche il cenote: ci si cammina sopra... e ci si cade (player.cpp)
  }
}

float level_stand_top(const Level* L, int cx, int cz) {
  if (cx < 0 || cz < 0 || cx >= L->W || cz >= L->H) return BLOCK_ALL;
  const uint8_t c = L->cell[cz][cx];
  if (c == C_WATER || c == C_LOW) return 0.0f;
  return level_walk_top(L, cx, cz);
}

float level_shot_top(const Level* L, int cx, int cz) {
  if (cx < 0 || cz < 0 || cx >= L->W || cz >= L->H) return BLOCK_ALL;
  switch (L->cell[cz][cx]) {
    case C_VOID: return BLOCK_ALL;
    case C_WALL: return 2.2f;
    case C_TREE: return 4.0f;
    case C_ROCK: return 1.1f;
    case C_GATE: return gate_closed(L, cx, cz) ? 2.2f : 0.0f;
    case C_CRATE: return CRATE_H;
    case C_BREAK: return crate_whole(L, cx, cz) ? CRATE_H : 0.0f;
    case C_PLATFORM: return PLAT_H;
    default: return 0.0f;
  }
}

float level_solid_top(const Level* L, int cx, int cz) {
  if (cx < 0 || cz < 0 || cx >= L->W || cz >= L->H) return 0.0f;
  switch (L->cell[cz][cx]) {
    case C_WALL: return WALL_H;
    case C_TREE: return TREE_H;
    case C_ROCK: return 1.1f;
    case C_GATE: return gate_closed(L, cx, cz) ? GATE_H : 0.0f;
    case C_LOW: return LOW_H;
    case C_CRATE: return CRATE_H;
    case C_BREAK: return crate_whole(L, cx, cz) ? CRATE_H : 0.0f;
    case C_PLATFORM: return PLAT_H;
    case C_TRAMP: return TRAMP_H;
    case C_WATER: return WATER_Y;
    default: return 0.0f;
  }
}

float level_ground(const Level* L, float x, float z, float feet, float step) {
  const float fx = x + (float)L->W * 0.5f, fz = z + (float)L->H;
  const float k = 0.12f;
  const int x0 = (int)floorf(fx - k), x1 = (int)floorf(fx + k), z0 = (int)floorf(fz - k), z1 = (int)floorf(fz + k);
  float g = 0.0f;
  for (int cz = z0; cz <= z1; ++cz)
    for (int cx = x0; cx <= x1; ++cx) {
      const float t = level_stand_top(L, cx, cz);
      if (t <= feet + step + 0.001f && t > g) g = t;
    }
  return g;
}

bool level_blocks(const Level* L, int cx, int cz, float feet, float step, int body) {
  const bool in = cx >= 0 && cz >= 0 && cx < L->W && cz < L->H;
  const int c = in ? (int)L->cell[cz][cx] : (int)C_VOID;
  if (body & BODY_FLY) {
    if (c == C_WATER || c == C_PIT) return false;   // si vola sopra l'acqua e sopra il cenote
    return level_walk_top(L, cx, cz) >= FLY_H;
  }
  if (c == C_PIT) return (body & BODY_PIT) != 0;
  return level_walk_top(L, cx, cz) > feet + step;
}

// Caselle toccate dal cerchio (x, z, r): la griglia va da (0, 0) a (W, H) in (fx, fz).
static inline void circle_cells(const Level* L, float x, float z, float r, int* x0, int* z0, int* x1, int* z1) {
  const float fx = x + (float)L->W * 0.5f, fz = z + (float)L->H;
  *x0 = (int)floorf(fx - r);
  *x1 = (int)floorf(fx + r);
  *z0 = (int)floorf(fz - r);
  *z1 = (int)floorf(fz + r);
}

// Distanza al quadrato tra il punto (px, pz) della griglia e la casella (cx, cz).
static inline float cell_d2(float px, float pz, int cx, int cz) {
  const float qx = clampf(px, (float)cx, (float)cx + 1.0f), qz = clampf(pz, (float)cz, (float)cz + 1.0f);
  return (px - qx) * (px - qx) + (pz - qz) * (pz - qz);
}

void level_push_out(const Level* L, float* x, float* z, float feet, float r, float step, int body) {
  const float ox = (float)L->W * 0.5f, oz = (float)L->H;
  for (int pass = 0; pass < 3; ++pass) {
    int x0, z0, x1, z1;
    circle_cells(L, *x, *z, r, &x0, &z0, &x1, &z1);
    bool moved = false;
    for (int cz = z0; cz <= z1; ++cz)
      for (int cx = x0; cx <= x1; ++cx) {
        if (!level_blocks(L, cx, cz, feet, step, body)) continue;
        const float px = *x + ox, pz = *z + oz;
        const float qx = clampf(px, (float)cx, (float)cx + 1.0f), qz = clampf(pz, (float)cz, (float)cz + 1.0f);
        const float dx = px - qx, dz = pz - qz, d2 = dx * dx + dz * dz;
        if (d2 >= r * r) continue;
        if (d2 > 1e-10f) {
          const float d = sqrtf(d2), k = (r - d) / d;
          *x += dx * k;
          *z += dz * k;
        } else {
          // Centro dentro la casella: esce dal lato piu' vicino che da' su una casella libera. Se
          // non ce n'e' nessuno resta dov'e': mai dentro un'altra casella che lo ferma (fino alla
          // 0.5 usciva comunque dal lato piu' vicino, anche dentro un muro, e poi dall'altra parte).
          const float side[4] = { px - (float)cx, (float)cx + 1.0f - px, pz - (float)cz, (float)cz + 1.0f - pz };
          const int nb[4][2] = { { cx - 1, cz }, { cx + 1, cz }, { cx, cz - 1 }, { cx, cz + 1 } };
          int best = -1;
          for (int k = 0; k < 4; ++k) {
            if (level_blocks(L, nb[k][0], nb[k][1], feet, step, body)) continue;
            if (best < 0 || side[k] < side[best]) best = k;
          }
          if (best < 0) continue;
          if (best == 0) *x = (float)cx - r - ox;
          else if (best == 1) *x = (float)cx + 1.0f + r - ox;
          else if (best == 2) *z = (float)cz - r - oz;
          else *z = (float)cz + 1.0f + r - oz;
        }
        moved = true;
      }
    if (!moved) break;
  }
}

bool level_body_ok(const Level* L, float x, float z, float feet, float r, float step, int body) {
  const float px = x + (float)L->W * 0.5f, pz = z + (float)L->H;
  int x0, z0, x1, z1;
  circle_cells(L, x, z, r, &x0, &z0, &x1, &z1);
  // un pelo di tolleranza: chi e' appena stato spinto fuori sta proprio sul bordo
  const float r2 = (r - 0.002f) * (r - 0.002f);
  for (int cz = z0; cz <= z1; ++cz)
    for (int cx = x0; cx <= x1; ++cx)
      if (level_blocks(L, cx, cz, feet, step, body) && cell_d2(px, pz, cx, cz) < r2) return false;
  return true;
}

bool level_body_guard(const Level* L, float ox, float oz, float* x, float* z, float feet, float r, float step, int body) {
  if (level_body_ok(L, *x, *z, feet, r, step, body)) return true;
  if (level_body_ok(L, *x, oz, feet, r, step, body)) { *z = oz; return false; }
  if (level_body_ok(L, ox, *z, feet, r, step, body)) { *x = ox; return false; }
  *x = ox;
  *z = oz;
  return false;
}

bool level_pit_at(const Level* L, float x, float z) {
  int cx, cz;
  return level_cell(L, x, z, &cx, &cz) && L->cell[cz][cx] == C_PIT;
}

bool level_pit_near(const Level* L, float x, float z, float r) {
  const float px = x + (float)L->W * 0.5f, pz = z + (float)L->H;
  int x0, z0, x1, z1;
  circle_cells(L, x, z, r, &x0, &z0, &x1, &z1);
  for (int cz = z0; cz <= z1; ++cz)
    for (int cx = x0; cx <= x1; ++cx)
      if (cx >= 0 && cz >= 0 && cx < L->W && cz < L->H && L->cell[cz][cx] == C_PIT && cell_d2(px, pz, cx, cz) < r * r) return true;
  return false;
}

// Attraversamento delle caselle lungo il segmento (Amanatides-Woo). Per ogni casella chiama
// visit(cx, cz, t_ingresso, t_uscita); si ferma quando visit restituisce true.
template <class F> static bool walk_cells(const Level* L, Vec3 a, Vec3 b, F visit) {
  const float ax = a.x + (float)L->W * 0.5f, az = a.z + (float)L->H;
  const float dx = b.x - a.x, dz = b.z - a.z;
  int cx = (int)floorf(ax), cz = (int)floorf(az);
  const int sx = dx > 0.0f ? 1 : -1, sz = dz > 0.0f ? 1 : -1;
  const float inf = 1e30f;
  const float tdx = dx != 0.0f ? fabsf(1.0f / dx) : inf, tdz = dz != 0.0f ? fabsf(1.0f / dz) : inf;
  float tmx = dx != 0.0f ? ((sx > 0 ? (float)(cx + 1) - ax : ax - (float)cx) * tdx) : inf;
  float tmz = dz != 0.0f ? ((sz > 0 ? (float)(cz + 1) - az : az - (float)cz) * tdz) : inf;
  float t = 0.0f;
  for (int guard = 0; guard < 4 * (LV_MAX_W + LV_MAX_H); ++guard) {
    const float tn = fminf(fminf(tmx, tmz), 1.0f);
    if (visit(cx, cz, t, tn)) return true;
    if (tn >= 1.0f) return false;
    t = tn;
    if (tmx < tmz) { cx += sx; tmx += tdx; }
    else { cz += sz; tmz += tdz; }
  }
  return false;
}

bool level_trace(const Level* L, Vec3 a, Vec3 b, float* t_hit, int* hcx, int* hcz) {
  const float dy = b.y - a.y;
  return walk_cells(L, a, b, [&](int cx, int cz, float t0, float t1) {
    const float top = level_shot_top(L, cx, cz);
    if (top <= 0.0f) return false;
    const float y0 = a.y + dy * t0, y1 = a.y + dy * t1;
    if (fminf(y0, y1) >= top) return false;
    // Entra nell'ostacolo dove la quota scende sotto la cima (o subito, se e' gia' sotto).
    float th = t0;
    if (y0 >= top && y1 < top && y0 != y1) th = t0 + (t1 - t0) * (y0 - top) / (y0 - y1);
    *t_hit = th;
    *hcx = cx;
    *hcz = cz;
    return true;
  });
}

bool level_walk_blocked(const Level* L, Vec3 a, Vec3 b, bool flying) {
  const int body = flying ? BODY_FLY : BODY_WALK | BODY_PIT;
  return walk_cells(L, a, b, [&](int cx, int cz, float, float) { return level_blocks(L, cx, cz, 0.0f, 0.36f, body); });
}
