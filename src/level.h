// Mappa: griglia di caselle da 1 metro letta da un file di testo, zone separate da cancelli,
// collisioni e linea di vista calcolate sulle caselle, geometria fissa per il disegno.
//
// Coordinate: la colonna cx va da ovest a est, la riga cz da nord (riga 0) a sud. Il mondo ha
// X verso est e Z verso sud: la casella (cx, cz) ha il centro in
//   x = cx - W/2 + 0.5,  z = cz - H + 0.5
// quindi la mappa sta tra x = -W/2 e +W/2 e tra z = -H e 0; i giocatori partono in basso (sud).
#pragma once
#include "mesh.h"

#define LV_MAX_W 48
#define LV_MAX_H 96
#define LV_MAX_ZONES 6
#define LV_MAX_GATES 6
#define LV_MAX_SPAWN 32
#define LV_MAX_ITEMS 64
#define LV_MAX_WAVES 4
#define LV_MAX_WAVE_KINDS 4
#define LV_MAX_DYN 480          // cespugli, erba alta, casse che si rompono, trampolini
// Paesaggio disegnato attorno alla mappa (0.5.1): quanto la telecamera di gioco puo' mostrare
// oltre i muri del bordo, a 30 m con i giocatori agli angoli (vedi game.cpp, CAM_OVER_*): di lato
// fino a 14 m (in alto l'inquadratura e' piu' larga), a nord 13 m, a sud 10 m. Oltre c'e' solo il
// terreno piatto e, verso sud (menu) e verso nord (fine mappa), poche sagome lontane.
#define LV_MARGIN_X 14          // caselle di paesaggio a ovest e a est
#define LV_MARGIN_N 13          // ...a nord
#define LV_MARGIN_S 10          // ...a sud
#define LV_CHUNK 8              // la geometria fissa e' divisa in blocchi di 8x8 caselle
#define LV_MAX_CHUNKS ((LV_MAX_W + 2 * LV_MARGIN_X + LV_CHUNK - 1) / LV_CHUNK * ((LV_MAX_H + LV_MARGIN_N + LV_MARGIN_S + LV_CHUNK - 1) / LV_CHUNK))
#define LV_MAX_SCENERY 96       // pezzi del paesaggio lontano: terreno oltre il bordo e tessere di 24 m (piramidi, colline...)

// Caselle della griglia. Che aspetto hanno dipende dallo stile della mappa (level_mesh.cpp):
// per esempio il muro e' di arenaria nelle Piramidi e di mattoni grigi nella Muraglia.
enum CellType {
  C_VOID = 0,   // ' '  fuori dalla mappa
  C_FLOOR,      // '.'  terreno (sabbia, prato, arena)
  C_PATH,       // ','  sentiero lastricato
  C_WALL,       // '#'  muro alto: non si scavalca
  C_TREE,       // 't'  albero (palma, pino...)
  C_ROCK,       // 'o'  masso, rovina, colonna spezzata
  C_LOW,        // '='  muretto basso: si scavalca saltando, i colpi passano
  C_CRATE,      // 'c'  blocco da 1 m: ci si sale saltando
  C_BREAK,      // 'x'  anfora o cassa che si rompe a colpi, con un bonus dentro (0.5.4); 'X' con un talismano, 'p' capsula con l'esploratore
  C_BUSH,       // '*'  cespuglio: nasconde i giocatori
  C_GRASS,      // '"'  erba alta, papiri, bambu': nasconde tutti
  C_WATER,      // '~'  acqua: non si attraversa, nemmeno saltando; i colpi passano (chi vola ci passa sopra)
  C_TRAMP,      // 'T'  trampolino
  C_PLATFORM,   // 'P'  piattaforma rialzata ('Q' con un talismano sopra)
  C_GATE,       // 'G'  cancello tra due zone
  C_PIT,        // 'O'  cenote: pozzo profondo; chi ci finisce sopra stando a terra ci cade (0.5.1)
  C_NTYPES
};
// Oggetti mobili (si schiacciano, si rompono, rimbalzano): le loro mesh sono dello stile della
// mappa, costruite da level_build_mesh e disegnate da game_draw.cpp (origine a terra al centro
// della casella, dentro il metro della casella).
enum { DM_BUSH = 0, DM_GRASS, DM_CRATE, DM_CRATE_TAL, DM_TRAMP, DM_COUNT };

// I mondi sono i pianeti (17/10), nell'ordine del viaggio.
enum { WORLD_TERRA = 0, WORLD_LUNA, WORLD_MARTE, WORLD_VENERE, WORLD_MERCURIO, WORLD_GIOVE, WORLD_SATURNO,
       WORLD_URANO, WORLD_NETTUNO, NWORLDS };
// Stile della mappa (riga "stile:" del file): colori, muri, oggetti, paesaggio e musica. L'ordine
// resta quello della 0.5 (game.cpp sceglie la musica per indice): Chichen Itza (0.5.1) ha preso
// il posto di Machu Picchu.
enum { STYLE_PIRAMIDI = 0, STYLE_CHICHENITZA, STYLE_MURAGLIA, STYLE_COLOSSEO, NSTYLES };
// Oggetti della mappa: talismano (cura), bomba da raccogliere, bonus nascosto in una cassa 'x' (0.5.4:
// quale si decide quando la cassa si rompe, vedi PW_* in game_int.h).
enum { IT_TALISMAN = 0, IT_BOMB = 1, IT_POWER = 2 };
// Mostri che le mappe possono chiamare nelle ondate e boss delle arene (Terra). Dalla 0.5.1 il
// corridore (MO_SCORPIONE) e' lo scorpione e il tiratore (MO_ARCIERE) l'arciere di terracotta.
enum { MO_SCORPIONE = 0, MO_ARCIERE, MO_SCARABEO, MO_GUERRIERO, MO_NTYPES };
enum { BOSS_NONE = -1, BOSS_GLADIATORE = 0 };

const float WALL_H = 1.6f, TREE_H = 3.0f, ROCK_H = 1.3f, LOW_H = 0.55f, CRATE_H = 1.0f;
const float PLAT_H = 0.8f, GATE_H = 1.7f, TRAMP_H = 0.15f, WATER_Y = -0.14f;
const float PIT_WATER_Y = -2.6f; // acqua in fondo al cenote (chi ci cade sparisce sotto)
const float BLOCK_ALL = 99.0f;   // altezza "infinita": nemmeno saltando
const float FLY_H = 1.3f;        // chi vola passa sopra a tutto cio' che e' piu' basso
// Corpi per le collisioni (ultimo argomento di level_push_out, level_body_ok e level_body_guard):
// BODY_WALK chi cammina, anche saltando (l'acqua lo ferma sempre); BODY_FLY chi vola (urta solo
// cio' che e' piu' alto di FLY_H, passa sopra acqua e cenote); BODY_PIT, insieme a BODY_WALK, per
// chi il cenote lo evita come un muro (i mostri che camminano, se non sono spinti; l'esploratore).
// false e true valgono ancora come "cammina" e "vola".
enum { BODY_WALK = 0, BODY_FLY = 1, BODY_PIT = 2 };
const float CRATE_HP = 50.0f, CAPSULE_HP = 3.0f * CRATE_HP;   // la capsula dell'esploratore e' una cassa piu' robusta

struct WaveDef {
  int nkinds;
  uint8_t kind[LV_MAX_WAVE_KINDS];     // MO_*
  uint8_t count[LV_MAX_WAVE_KINDS];
};

struct ZoneDef {
  int x0, z0, x1, z1;                  // caselle estreme (comprese)
  float wx0, wx1, wz0, wz1;            // bordi esterni in coordinate del mondo
  int nwaves;
  WaveDef wave[LV_MAX_WAVES];
  int nspawn;
  int16_t spawn[LV_MAX_SPAWN][2];      // caselle di comparsa dei mostri
  int entry_gate, exit_gate;           // cancello di ingresso e di uscita (-1 nessuno)
  Vec3 start[4];                       // dove si (ri)parte nella zona
};

// Cancelli a battente (0.5.4; prima scendevano sotto terra): due battenti con i cardini ai due capi
// del cancello. Si aprono girando di 90 gradi verso la zona che li apre (from_zone: chi ci gioca li
// vede aprirsi verso di se', e chi e' davanti ai battenti viene spinto via) e si richiudono dietro
// ai giocatori quando sono tutti nella zona dopo, sbattendo (dalla parte di from_zone, ormai vuota).
struct Gate {
  int x0, z0, x1, z1;                  // caselle del cancello
  int from_zone;                       // si apre quando questa zona e' libera
  bool horiz;                          // da ovest a est (le zone sono a nord e a sud)
  float open;                          // 0 chiuso .. 1 aperto: battenti girati di gate_theta(open)
  float vel;                           // quanto cambia open a ogni fotogramma (game.cpp, update_gates)
  float mx, mz;                        // verso from_zone, perpendicolare al cancello: da li' si aprono i battenti
  float stop[2];                       // fin dove si apre ogni battente (1: del tutto; meno se c'e' un ostacolo)
  Mesh leaf[2];                        // battenti chiusi, con il cardine nell'origine (level_mesh.cpp)
};
#define GATE_LEAF_HALF 0.1f            // meta' dello spessore dei battenti (per le spinte)
struct MapItem {
  uint8_t kind, zone;
  int16_t cx, cz;
  int16_t crate;                       // talismano o bonus dentro una cassa: indice in dyn[], altrimenti -1
  float y;                             // quota del piano su cui sta
};
struct DynCell { uint8_t type, zone, capsule; int16_t cx, cz; float hp; };  // hp: casse che si rompono; capsule 1: capsula dell'esploratore 'p'
static inline float dyn_hp_max(const DynCell& d) { return d.capsule ? CAPSULE_HP : CRATE_HP; }

struct Level {
  char name[40];                       // nome mostrato nei menu
  char id[24];                         // nome del file (senza .txt)
  int world, boss, style;
  int W, H;
  uint8_t cell[LV_MAX_H][LV_MAX_W];
  int8_t zone[LV_MAX_H][LV_MAX_W];     // -1 fuori dalle zone
  int16_t dyn_at[LV_MAX_H][LV_MAX_W];  // indice in dyn[] o -1
  int8_t gate_at[LV_MAX_H][LV_MAX_W];  // cancello della casella G o -1
  int nzones;
  ZoneDef zones[LV_MAX_ZONES];
  int ngates;
  Gate gates[LV_MAX_GATES];
  int nitems;
  MapItem items[LV_MAX_ITEMS];
  int ndyn;
  DynCell dyn[LV_MAX_DYN];
  int16_t capsule_dyn;                 // capsula con l'esploratore: indice in dyn[], -1 se la mappa non ce l'ha
  Vec3 boss_pos;
  int nchunks;
  Mesh chunk[LV_MAX_CHUNKS];           // terreno, muri, ostacoli fissi, paesaggio vicino (blocchi di 8x8 caselle)
  int nscenery;
  Mesh scenery[LV_MAX_SCENERY];        // paesaggio lontano: terreno oltre il bordo e forme grandi
  Mesh dyn_mesh[DM_COUNT];             // cespuglio, erba alta, cassa, cassa col talismano, trampolino
};

// Legge una mappa dal testo. false (con il motivo in err) se il testo non e' valido.
bool level_parse(Level* L, const char* text, char* err, int errlen);
// Costruisce la geometria fissa, il paesaggio e le mesh degli oggetti mobili nello stile della
// mappa (da chiamare una volta dopo level_parse; level_free libera tutto).
void level_build_mesh(Level* L);
void level_free(Level* L);
// Rimette casse e cancelli come all'inizio della mappa (z < 0) o solo quelli della zona z.
void level_reset_dynamic(Level* L, int z);
// Battenti dei cancelli (0.5.4): quanto e' girato il battente side (radianti, da 0 chiuso a pi/2
// aperto). Il movimento (parte piano, sbatte chiudendosi) lo fa update_gates su open.
static inline float gate_theta(const Gate& gt, int side) {
  float o = gt.open < gt.stop[side] ? gt.open : gt.stop[side];
  if (o < 0.0f) o = 0.0f;
  return D_HALF_PI * o;
}
// Il battente side (0: al capo u0, cioe' a ovest o a nord; 1: all'altro) del cancello gi girato di
// theta: cardine (a terra), direzione dal cardine alla punta e lunghezza (dal cardine al centro del
// cancello). closed: la direzione da chiuso (dal cardine verso il centro del cancello).
void level_gate_leaf(const Level* L, int gi, int side, float theta, Vec3* hinge, Vec3* dir, float* len, Vec3* closed = 0);

static inline Vec3 cell_center(const Level* L, int cx, int cz) {
  return v3((float)cx - (float)L->W * 0.5f + 0.5f, 0.0f, (float)cz - (float)L->H + 0.5f);
}
// Casella che contiene (x, z); false se fuori dalla griglia.
bool level_cell(const Level* L, float x, float z, int* cx, int* cz);
int level_type_at(const Level* L, float x, float z);   // C_VOID fuori dalla griglia
// Altezza degli ostacoli della casella secondo i vari usi (tengono conto di casse e cancelli):
float level_walk_top(const Level* L, int cx, int cz);  // per chi cammina (99 = mai: anche l'acqua)
float level_stand_top(const Level* L, int cx, int cz); // piano su cui si sta (acqua e staccionate: 0)
float level_shot_top(const Level* L, int cx, int cz);  // per i colpi dritti e la vista
float level_solid_top(const Level* L, int cx, int cz); // altezza vera (colpi a parabola)
// Quota su cui poggia chi ha i piedi a quota feet e sale gradini fino a step.
float level_ground(const Level* L, float x, float z, float feet, float step);
// La casella ferma il corpo (BODY_*) con i piedi a quota feet che sale gradini fino a step?
// Fuori dalla griglia tutto ferma.
bool level_blocks(const Level* L, int cx, int cz, float feet, float step, int body);
// Spinge fuori dagli ostacoli un cerchio di raggio r con i piedi a quota feet. Non lo spinge mai
// dentro una casella che lo ferma: se il centro e' dentro un ostacolo esce dal lato piu' vicino
// che da' su una casella libera, e se non ce n'e' resta dov'e' (ci pensa level_body_guard).
void level_push_out(const Level* L, float* x, float* z, float feet, float r, float step, int body);
// Il cerchio di raggio r in (x, z) non tocca nessuna casella che ferma il corpo?
bool level_body_ok(const Level* L, float x, float z, float feet, float r, float step, int body);
// Guardia finale di ogni spostamento (0.5.1), da chiamare dopo level_push_out e le altre spinte:
// se in (*x, *z) il corpo tocca un ostacolo prova (*x, oz), poi (ox, *z) (si scivola lungo i
// muri), se no torna in (ox, oz). true se la posizione nuova e' stata presa cosi' com'era.
bool level_body_guard(const Level* L, float ox, float oz, float* x, float* z, float feet, float r, float step, int body);
// Cenote: il punto (x, z) sta sopra una casella del pozzo? (Chi ci sta sopra a terra ci cade.)
bool level_pit_at(const Level* L, float x, float z);
// Una casella del cenote arriva a meno di r dal punto (x, z)?
bool level_pit_near(const Level* L, float x, float z, float r);
// Primo ostacolo per un colpo dritto da a a b: true se c'e', con la frazione del percorso e la casella.
bool level_trace(const Level* L, Vec3 a, Vec3 b, float* t, int* hcx, int* hcz);
static inline bool level_blocked(const Level* L, Vec3 a, Vec3 b) { float t; int x, z; return level_trace(L, a, b, &t, &x, &z); }
// Vero se un mostro che cammina (o vola) non puo' andare in linea retta da a a b (chi cammina
// evita anche il cenote).
bool level_walk_blocked(const Level* L, Vec3 a, Vec3 b, bool flying);
// Zona della casella che contiene (x, z), -1 se nessuna.
int level_zone_at(const Level* L, float x, float z);
