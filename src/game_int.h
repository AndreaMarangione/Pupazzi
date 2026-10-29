// Stato interno del gioco, condiviso tra i moduli: regole e flusso (game.cpp), personaggi
// (player.cpp), colpi, bombe ed effetti (combat.cpp), mostri e boss (monsters.cpp), percorsi
// (flow.cpp), menu (menu.cpp), disegno (game_draw.cpp) e giocatori automatici (bot.cpp).
// Tutto avanza a passi fissi di 1/60 di secondo e usa solo la matematica di dmath.h: stessa
// partita, bit per bit, su ogni processore.
#pragma once
#include "game.h"
#include "lang.h"
#include "level.h"
#include "models.h"
#include "audio.h"

#define MAX_MONSTERS 56          // <= 64: i colpi che attraversano ricordano i colpiti in un uint64_t
#define MAX_SHOTS 256
#define MAX_PARTICLES 420
#define MAX_FLOATS 40
#define MAX_BOMBS 24
#define MAX_MARKERS 160
#define MAX_BOMBS_CARRIED 3

const float DT = 1.0f / 60.0f;
const float GRAVITY = 22.0f;
const float AIM_FORESHORTEN = 0.83f;    // sin dell'inclinazione della telecamera: mira "come sullo schermo"

// --- personaggi ---------------------------------------------------------------

const float PL_R = 0.32f, PL_H = 1.4f, PL_STEP = 0.36f;
const float PL_ACCEL = 0.40f, PL_JUMP_V = 7.1f, PL_TURN = 20.0f;
const float HEAL_KILL = 8.0f, HEAL_TALISMAN = 35.0f;
const int RESPAWN_FRAMES = 300, INVULN_FRAMES = 90;
const float RESPAWN_HP = 0.6f;
// Vite (17/10): 3 a testa per mappa (VITA EXTRA: una in piu'). Da soli ogni caduta fa rifare la
// zona e l'ultima la mappa da capo; in piu' giocatori chi cade rientra dopo RESPAWN_FRAMES finche'
// ha vite, poi resta fuori (out) fino alla fine della mappa; tutti a terra insieme: mappa da capo.
const int START_LIVES = 3;
const int OUT_VANISH = 100;       // chi resta senza vite sparisce in uno sbuffo dopo cosi' tanti fotogrammi a terra
const int WIPE_ZONE_FRAMES = 150, WIPE_MAP_FRAMES = 180;   // dopo "a terra": zona rifatta / mappa da capo
// Giri (17/10): ogni pianeta ha il suo giro (save.h); battuto il boss con almeno GIRO_STARS stelle
// del giro in corso si apre il giro dopo, piu' difficile, e le stelle ripartono da zero.
const int GIRO_STARS = 10;
const float SPAN_X = 31.0f, SPAN_Z = 21.0f;   // quanto si possono allontanare i giocatori: quanto inquadra la telecamera a 30 m (17/10)
const int FROZEN_FRAMES = 30 * 60;            // controller fermo sullo stesso stato: il personaggio si ferma

enum { AIM_LINE = 0, AIM_FAN, AIM_LOB, AIM_AREA };

// Mira con la levetta destra (player.cpp). Ogni giocatore sceglie nella pausa quanto e' pronta:
//  - on: inclinazione oltre la quale si mira (zona morta circolare);
//  - min_cut, beta: filtro sulla direzione (sul modello del filtro "1 euro"). Quando la levetta
//    punta dove gia' si mira il taglio e' min_cut Hz (spariscono i tremolii); piu' la levetta e'
//    lontana dalla mira, piu' il taglio sale: beta Hz per ogni radiante al secondo con cui la mira
//    dovrebbe muoversi per raggiungerla (gli scatti ampi restano rapidi).
enum { AIM_XSOFT = 0, AIM_SOFT, AIM_MEDIUM, AIM_QUICK, NAIMLEVELS };
const int AIM_DEFAULT = AIM_SOFT;   // scelta da Andrea il 15/10
struct AimTune { const char* name; const char* label; float on, min_cut, beta; };
extern const AimTune AIM_TUNE[NAIMLEVELS];
const float AIM_FULL = 0.95f;     // inclinazione che conta come "tutta" (gittata piena per i colpi a parabola)
const float AIM_DROP = 0.10f;     // l'inclinazione cala di piu' in un fotogramma: forse la levetta e' stata lasciata
const float AIM_RELEASE = 0.3f;   // ...e se negli ultimi AIM_HIST fotogrammi e' calata di piu', lo era davvero
const int AIM_HIST = 4;
const float AIM_D_CUT = 5.0f;     // filtro sulla velocita' della mira (Hz)
const int AIM_REARM = 12;         // dopo averla lasciata, per cosi' tanti fotogrammi servono 2 letture di fila
static inline float aim_k_of(float m, float on) { return clampf((m - on) / (AIM_FULL - on), 0.15f, 1.0f); }

struct CharDef {
  const char* name;        // nome in maiuscolo per i menu
  const char* who;         // chi e'
  const char* role;        // ruolo nella squadra
  const char* normal;      // colpo normale, in breve
  const char* special;     // speciale, in breve
  uint32_t color;
  float hp, speed;
  float ammo_max;          // tacche della barra (Zolfo: carburante)
  float ammo_cost;         // quanto costa un attacco
  int fire_frames;         // tra un attacco e il successivo tenendo R1
  int reload_wait;         // la barra riparte cosi' tanti fotogrammi dopo l'ultimo attacco
  int reload_frames;       // da vuota a piena
  float range;             // gittata del colpo normale
  uint8_t aim;             // forma della striscia di mira del colpo normale
  float aim_w;             // larghezza della striscia o apertura del ventaglio
  float special_cost;      // danni da fare con il colpo normale per caricare lo speciale
  uint8_t aim_sp;          // forma della mira dello speciale
  float sp_range, sp_w;
};
extern const CharDef CHARS[NCHARS];

// --- mostri -------------------------------------------------------------------

struct MonDef {
  const char* name;
  float hp, speed, dmg, r, h;
  bool flying;
};
extern const MonDef MONS[MO_NTYPES];

// --- colpi --------------------------------------------------------------------

enum ShotKind {
  SK_BULLET = 0,   // Astro: colpo del fucilone
  SK_ROCKET,       // Astro, speciale: razzo che insegue i mostri
  SK_SHARD,        // Mochi: scheggia di luna (anche quelle dello speciale)
  SK_FROST,        // Marzio: palla di ghiaccio secco che rallenta
  SK_FLAME,        // Zolfo: fiamma che attraversa i mostri
  SK_LAVA,         // Zolfo, speciale: bolla di lava a parabola
  SK_ARROW,         // arciere di terracotta: freccia lenta a parabola
  SK_JAVELIN,      // gladiatore gigante: giavellotto a parabola
  NSHOTKINDS
};

enum { PH_FIGHT = 0, PH_ENTER, PH_CLEAR, PH_MAP_DONE, PH_WIPE };
enum { SC_TITLE = 0, SC_MODE, SC_MAPS, SC_CHARS, SC_PLAY, SC_STATS, SC_SLOTS, SC_LANG, SC_NAME, NSCREENS };
// Prima schermata (0.5.3): nuova partita, carica, gestisci i salvataggi, lingua, esci.
enum { TI_NEW = 0, TI_LOAD, TI_MANAGE, TI_LANG, TI_QUIT, TITLE_ITEMS };
// Schermata dei salvataggi: per caricarne uno, per gestirli (duplica, elimina) o, con la nuova partita
// e i posti pieni, per scegliere quale sovrascrivere. Finestre sopra: azioni, conferma.
enum { SL_LOAD = 0, SL_MANAGE, SL_OVERWRITE };
enum { SP_NONE = 0, SP_ACTIONS, SP_DELETE, SP_OVERWRITE };
// Voci della finestra delle azioni (0.5.4: anche RINOMINA).
enum { SA_COPY = 0, SA_RENAME, SA_DELETE, SA_COUNT };
// Tastiera per il nome della partita (0.5.4): partita nuova (prima di crearla) o nome nuovo.
enum { NM_NEW = 0, NM_RENAME };
// Voci della pausa: riprendi, comandi (0.5.2: apre il riquadro con i tasti), mira, antialias, musica,
// effetti, lascia la partita, torna al menu.
enum { PS_RESUME = 0, PS_CONTROLS, PS_AIM, PS_AA, PS_MUSIC, PS_SFX, PS_LEAVE, PS_MENU, PAUSE_ITEMS };

struct Player {
  bool active, alive, grounded, frozen, hidden;
  uint8_t ch;
  Vec3 pos;                   // punto a terra tra i piedi
  float vx, vz, vy, yaw;
  float hp, hp_max, speed;
  float ammo; bool locked;
  int fire_cd, reload_wait;
  float charge;               // danni fatti con il colpo normale dall'ultimo speciale
  int burst_left, burst_cd;   // Astro: colpi della raffica ancora da sparare
  float burst_yaw;
  bool aiming;                // levetta destra inclinata
  float aim_yaw, aim_k;       // dove si mira (per girarsi, sparare e disegnare) e quanto e' inclinata (0..1)
  float aim_f, aim_dx;        // stato del filtro: direzione filtrata e sua velocita' (rad/s)
  float aim_m;                // inclinazione della levetta al fotogramma prima
  float aim_hy[AIM_HIST], aim_hm[AIM_HIST];   // ultimi fotogrammi di mira: direzione e inclinazione
  int aim_nh;
  int aim_on_n;               // letture di fila oltre la soglia
  int aim_off_t;              // fotogrammi da quando si e' smesso di mirare
  unsigned aim_tick;          // ultimo fotogramma in cui si e' letta la levetta
  int aim_hold;               // resta girato verso dove ha attaccato
  int bombs;
  int respawn, invuln, hurt, recoil, down, reveal, land;
  float phase, amp;           // camminata
  float sq, sq_v;             // squash & stretch: 0 = forma normale
  int leap_t, leap_n;         // Mochi: salto dello speciale (fotogrammi passati / totali)
  Vec3 leap_a, leap_b;
  int blizzard_t;             // Marzio: tempesta attiva
  int flame_t;                // Zolfo: sta sputando fuoco
  int tramp_t;                // lanciato da un trampolino
  float tvx, tvz;
  float kx, kz;               // spinta di un'esplosione o di un colpo forte (si smorza)
  uint16_t prev_btn;
  Pad last_pad;
  int same_t;
  // Statistiche della mappa in corso.
  int kills, deaths;
  float dmg_done;
  // Finestra per entrare a partita iniziata.
  bool joining;
  int join_pick;
  // Carte della mappa in corso (cards.cpp).
  uint32_t cards;             // carte prese (bit CA_*)
  bool shield;                // SCUDO carico: il prossimo colpo si ferma
  // Vite della mappa in corso.
  int lives;                  // vite rimaste (cadere ne toglie una)
  bool out;                   // senza vite: a terra fino alla fine della mappa (o finche' non ricomincia)
};

struct Monster {
  bool alive, direct, los, summoned;
  uint8_t type;
  int8_t target;
  Vec3 pos;                   // piedi (per chi vola, il punto a terra sotto)
  float vx, vz, kx, kz, yaw, fly;
  float hp, hp_max, speed, dmg;
  int rise, cd, windup, hurt, shown, repath, state, state_t, strafe_t, reveal, slow_t, los_t;
  float strafe, phase, slow_k, sq, sq_v;
  float cdx, cdz;             // scorpione: direzione della carica; arciere: dove mira (x, z)
  uint8_t hitmask;            // giocatori gia' colpiti in questa carica o zampata; arciere: bit 0, il prossimo tiro e' una raffica
  Vec3 last_seen;
  int burn_t;                 // carte combo: brucia ancora per cosi' tanti fotogrammi
  int8_t burn_by;             // ...acceso da questo giocatore
  int vapor_t;                // VAPORE: fotogrammi prima che possa scoppiare di nuovo
  int8_t push_by;             // giocatore dell'ultima spinta, piu' uno (0 nessuno): sua la vittoria se cade nel cenote
};

// Gladiatore gigante (0.5.1): insegue il bersaglio e combatte. Azioni (monsters.cpp; le pose sono in
// game_draw.cpp): giavellotti a parabola, spaccaterra (la crepa in linea), rinforzi dalle botole,
// fendente davanti a se', carica con lo scudo, stordito dopo una carica contro un ostacolo.
enum { BA_NONE = -1, BA_JAVELIN = 0, BA_SLAM, BA_SUMMON, BA_SWEEP, BA_RUSH, BA_STUN, BA_COUNT };
const float BOSS_SWEEP_R = 4.0f;                            // fendente: settore di 4 m davanti (BOSS_SWEEP_HALF in models.h)
const float BOSS_LANE_HW = 1.25f;                           // carica: mezza larghezza della corsia (il suo raggio)
const float BOSS_STUN_DMG = 1.5f;                           // stordito: i colpi fanno il 50% in piu'
const int BOSS_STUN_FRAMES = 90;

struct Boss {
  bool alive;
  int type;
  Vec3 pos;
  float hp, hp_max;
  int stage;                  // 0, 1, 2: sempre piu' rapido
  int cd, act, act_t, hurt, shown, roar;   // cd: pausa prima del prossimo attacco
  float sq, sq_v, sway;
  int target;                 // giocatore che insegue (-1 nessuno)
  int target_t;               // fotogrammi con lo stesso bersaglio
  float aggro[MAX_PLAYERS];   // danni recenti di ogni giocatore (si smorzano): chi picchia forte si fa inseguire
  float face;                 // dove guarda: fendente e carica partono da questa parte
  float vx, vz;               // velocita' della camminata
  // Azione in corso: preparazione (piu' corta a ogni fase), direzione, carica.
  int wind;                   // fotogrammi di preparazione (fino al colpo)
  float act_yaw;              // direzione dell'attacco
  float lane, run;            // carica: metri della corsia, metri gia' fatti
  int end_t;                  // carica: act_t a cui la corsa e' finita (poi la ripresa)
  bool crash;                 // carica: la corsia finisce contro un ostacolo
  uint8_t hitmask;            // giocatori gia' colpiti da questo attacco
  unsigned crack_t[MAX_PLAYERS];   // quando una crepa ha colpito ognuno l'ultima volta (una sola volta per spaccaterra)
  int last[2];                // ultimi due attacchi (niente ripetizioni lunghe)
  int alt;                    // scelte da media e lunga distanza: a turno la carica e l'altro attacco
  int idle_t;                 // fotogrammi pronto ad attaccare senza un attacco adatto alla distanza
  int queued;                 // attacco subito dopo questo (giavellotti dopo una carica), BA_NONE nessuno
  // Camminata: dritto o verso il punto del percorso, controllo di "bloccato" e passo di lato.
  Vec3 way, check;
  int check_t, side_t, stuck_n;
  float side;
  int repath;
  bool direct;
  // Disegno (cambiano solo con la partita): passo della camminata, fotogrammi dalla sconfitta
  // (cade e sparisce nella polvere).
  float phase, amp;
  int dead_t;
};

struct Shot {
  bool alive;
  uint8_t kind;
  int8_t owner;               // 0..3 giocatori, -1 mostri, -2 boss
  Vec3 pos, vel;
  float left;                 // metri che restano (colpi dritti)
  float dmg, r, yaw;
  int life, target;
  Vec3 dest;                  // colpi a parabola: punto di arrivo
  uint64_t hit;               // mostri gia' colpiti (colpi che attraversano)
  bool hit_boss;
  bool special;               // colpo di uno speciale: non carica lo speciale
  uint8_t fx;                 // carte combo: SFX_BURN incendia, SFX_SLOW rallenta
  uint8_t pierce, bounce;     // PERFORANTE: mostri che puo' ancora attraversare; RIMBALZO: rimbalzi rimasti
};
enum { SFX_BURN = 1, SFX_SLOW = 2 };

struct Bomb {
  bool alive, lit;
  int8_t owner;               // chi l'ha lasciata o accesa (-1 nessuno)
  int8_t item;                // bomba della mappa: indice dell'oggetto, -1 lasciata da un giocatore
  Vec3 pos;
  float vy;
  int fuse;
};

enum { PK_CUBE = 0, PK_PUFF, PK_SPARK, PK_LEAF, PK_FLAME, PK_SNOW };
struct Particle {
  bool alive;
  uint8_t col, kind;
  int life, life0;
  Vec3 pos, vel;
  float size, spin;
};

// Segni a terra: dove cade un colpo a parabola, dove sta per spuntare un attacco del boss, esplosioni...
// Gladiatore (0.5.1): MK_SWEEP il settore del fendente (centro pos, raggio r, verso yaw), MK_LANE la
// corsia della carica (da pos verso yaw, lunga len, mezza larghezza r); si riempiono fino a fire_at.
enum { MK_LOB = 0, MK_ROOT, MK_RING, MK_BLAST, MK_SWEEP, MK_LANE };
struct Marker {
  bool alive;
  uint8_t kind;
  Vec3 pos;
  float r;
  int t, life;                // t: fotogrammi passati
  int fire_at;                // crepe: quando spuntano le pietre; fendente e carica: quando parte il colpo
  int open_at;                // crepe: quando si apre la crepa
  bool done;
  bool crash;                 // carica: la corsia finisce contro un ostacolo (li' sbatte)
  float yaw, len;             // crepe, fendente, carica: direzione; carica: lunghezza (m)
};

// Numeri dei danni e delle cure: i colpi ravvicinati sullo stesso bersaglio si sommano.
struct FloatText { bool alive; int life, key, value; uint32_t rgb; Vec3 pos; char txt[10]; };

// Tasti del giocatore per i menu: nav e' la direzione appena scelta (0 nessuna, 1 su, 2 giu',
// 3 sinistra, 4 destra), con la ripetizione automatica tenendo premuto.
enum { NAV_NONE = 0, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT };
struct PadState { uint16_t btn, prev; float lx, ly; int nav, nav_t, navdir; };

// --- carte (cards.cpp) ----------------------------------------------------------
// All'inizio di ogni mappa e dopo ogni zona ripulita (se ne segue un'altra) ogni giocatore sceglie
// 1 carta su 3; valgono fino alla fine della mappa. Le combo vanno a un personaggio solo se un
// altro giocatore usa il compagno indicato.
enum CardId {
  CA_NONE = 0,
  CA_BOMBAROLO, CA_PASSO, CA_PELLE, CA_RICARICA, CA_SUPER, CA_PESANTI, CA_GITTATA, CA_MERENDA,
  CA_SCUDO, CA_VITA, CA_CALAMITA, CA_BOMBE, CA_GENEROSO, CA_PERFORANTE, CA_RIMBALZO,
  CA_COLPI_FUOCO, CA_COLPI_GELO, CA_SCHEGGE_FUOCO, CA_SCHEGGE_GELO, CA_VAPORE,   // combo
  NCARDS
};
struct CardDef {
  const char* name;        // come si vede (maiuscolo)
  const char* desc;        // descrizione (ASCII)
  const char* brief;       // piu' corta per i riquadri stretti dei 4 giocatori (NULL: desc)
  bool combo;
};
extern const CardDef CARDS[NCARDS];
struct CardOffer {
  uint8_t card[3];
  int8_t n, sel;           // carte proposte, voce su cui sta il cursore
  bool on, done;           // ha una proposta (era in partita all'apertura) / ha gia' scelto
  bool shown;              // (0.5.4) ha premuto A sul banner "RITIRA LE TUE CARTE": vede le sue carte
};
const int CARDS_CLOSE_FRAMES = 20;   // quando hanno scelto tutti, si chiude dopo cosi'
const int CARDS_CLEAR_DELAY = 40;    // zona ripulita: si vede "ZONA LIBERA!", poi le carte
const int CARDS_TIMEOUT = 60 * 60;   // chi non sceglie in un minuto (pad spento?) prende quella col cursore
const int CARDS_BANNER_MIN = 15;     // (0.5.4) il banner c'e' da cosi' prima che A conti: niente scelte per sbaglio
enum { EL_NONE = 0, EL_FIRE, EL_SNOW };   // colpi di fuoco o di neve (per VAPORE)

struct Game {
  // Schermate e menu.
  int screen, screen_t;
  int menu_sel, menu_owner;
  bool joined[MAX_PLAYERS], ready[MAX_PLAYERS];
  int pick[MAX_PLAYERS];
  int chars_t;
  int map_sel, map_shown, map_load_t;
  char map_note[120];
  int slots_mode, slots_sel, slots_pop, slots_pop_sel;   // salvataggi (SL_*, posto col cursore, SP_*, voce della finestra)
  int lang_sel;
  int name_mode, name_slot, name_row, name_col;          // tastiera del nome (NM_*, posto, tasto col cursore)
  bool name_overwrite;                                   // partita nuova al posto di un'altra (B torna ai posti)
  char name_buf[SAVE_NAME_LEN];
  int pause_owner, pause_sel;
  bool paused;
  bool pause_help;            // nella pausa e' aperto il riquadro dei comandi
  uint8_t aim_level[MAX_PLAYERS];   // mira scelta da ogni giocatore nella pausa (AIM_XSOFT...)
  bool aa_avail, aa_on;             // antialias: il renderer lo sa fare / scelto nella pausa (solo grafica)
  // Campagna. loop: giro del pianeta della mappa in corso, da 0 (preso dal salvataggio quando la
  // mappa comincia; i mostri hanno piu' vita, danni e velocita' a ogni giro).
  int map_idx, loop;
  bool level_ok;
  Level L;
  Player pl[MAX_PLAYERS];
  Monster mo[MAX_MONSTERS];
  Boss boss;
  Shot sh[MAX_SHOTS];
  Bomb bo[MAX_BOMBS];
  Particle pa[MAX_PARTICLES];
  FloatText ft[MAX_FLOATS];
  Marker mk[MAX_MARKERS];
  bool item_on[LV_MAX_ITEMS];
  int item_t[LV_MAX_ITEMS];
  float squash[LV_MAX_DYN];     // cespugli ed erba schiacciati da chi ci passa
  int crate_hit[LV_MAX_DYN];    // casse appena colpite (traballano)
  int tramp_t[LV_MAX_DYN];      // trampolini appena usati
  bool start_tap[MAX_PLAYERS];  // START premuto e lasciato subito (dal core)
  int bots;                     // i primi "bots" giocatori li guida il computer (prove)
  Vec3 last_vis[MAX_PLAYERS];   // dove i mostri hanno visto ogni giocatore l'ultima volta
  int boss_seq;                 // attacchi del boss fatti finora (per l'alternanza)
  int zone, phase, phase_t;
  int wave, spawn_left[MO_NTYPES], spawn_cd, spawn_wait, enemies_left;
  uint32_t rng;
  unsigned tick, ui_tick;       // ui_tick va avanti anche in pausa e nei menu
  PadState in[MAX_PLAYERS];
  Vec3 cam_target;
  float cam_dist, cam_pitch, shake;
  bool cam_back;                // telecamera rivolta verso sud (menu: i personaggi guardano verso chi gioca)
  float gate_want[LV_MAX_GATES];
  char msg[160], msg2[224];        // gia' nella lingua scelta (tr al momento del messaggio)
  int msg_t, msg_size;
  uint32_t msg_rgb;
  int kills, deaths, wipes, maps_done;
  int restarts;                 // mappe ricominciate da capo in questa sessione
  bool wipe_cleared;
  bool wipe_map;                // PH_WIPE: alla fine si ricomincia la mappa da capo (altrimenti solo la zona)
  int mvp;
  int lives_kept[MAX_PLAYERS];  // vite di chi ha lasciato la partita in questo tentativo (-1: nessuno)
  bool vita_kept[MAX_PLAYERS];  // ...e se aveva preso VITA EXTRA (rientrando non la riprende)
  // Statistiche di chi ha lasciato la partita in questa mappa (anche prima di una mappa da capo):
  // rientrando le ritrova.
  bool stats_kept[MAX_PLAYERS];
  int kills_kept[MAX_PLAYERS], deaths_kept[MAX_PLAYERS];
  float dmg_kept[MAX_PLAYERS];
  // Carte: mentre la scelta e' aperta il gioco resta fermo come nella pausa (ui_tick va avanti).
  bool cards_open;
  int cards_t;                  // fotogrammi da quando hanno scelto tutti
  int cards_age;                // fotogrammi da quando la scelta e' aperta
  int cards_due;                // zona appena ripulita: fotogrammi prima che si apra la scelta
  unsigned cards_seq;           // scelte aperte finora (per i giocatori automatici)
  CardOffer offer[MAX_PLAYERS];
  // Capsula nascosta con l'esploratore e stelle della mappa in corso.
  bool explorer_free;           // capsula aperta in questa mappa
  int expl_t;                   // fotogrammi da quando e' uscito (-1: ancora nella capsula)
  Vec3 expl_pos;
  float expl_yaw;
  int help_t;                   // tra quanto chiama di nuovo "aiuto!"
  int map_wipes;                // volte che la mappa e' ricominciata da capo in questa visita (stella 3)
  uint8_t stars_got, stars_new; // stelle prese alla fine della mappa / quelle nuove per il salvataggio
  uint8_t stars_had;            // stelle della mappa nel salvataggio prima di questa volta
  // Boss del pianeta battuto: esito del giro, per la schermata di fine mappa.
  bool giro_done;               // si e' guardato il giro (fine dell'arena)
  bool giro_up;                 // aperto il giro dopo (le stelle del pianeta ripartono da zero)
  int giro_stars, giro_was;     // stelle del pianeta nel giro appena finito; quel giro
};

extern Game g;

static inline bool has_card(const Player& p, int c) { return ((p.cards >> c) & 1u) != 0u; }
// Valori del personaggio con le carte: li usano il gioco, il disegno (mira, barre) e i giocatori
// automatici. Senza la carta restano esattamente quelli di CHARS.
static inline float pl_speed(const Player& p) { return has_card(p, CA_PASSO) ? CHARS[p.ch].speed * 1.15f : CHARS[p.ch].speed; }
static inline float pl_range(const Player& p) { return has_card(p, CA_GITTATA) ? CHARS[p.ch].range * 1.25f : CHARS[p.ch].range; }
static inline float pl_special_cost(const Player& p) {
  return has_card(p, CA_SUPER) ? CHARS[p.ch].special_cost * 0.65f : CHARS[p.ch].special_cost;
}
static inline int pl_reload_wait(const Player& p) {
  return has_card(p, CA_RICARICA) ? (CHARS[p.ch].reload_wait * 3 + 2) / 5 : CHARS[p.ch].reload_wait;
}
static inline float pl_reload_frames(const Player& p) {
  return has_card(p, CA_RICARICA) ? (float)CHARS[p.ch].reload_frames * 0.6f : (float)CHARS[p.ch].reload_frames;
}
static inline int pl_bombs_cap(const Player& p) { return has_card(p, CA_BOMBAROLO) ? 4 : MAX_BOMBS_CARRIED; }

static inline int aim_level_of(int i) { const int l = g.aim_level[i]; return l < (int)NAIMLEVELS ? l : AIM_DEFAULT; }
static inline const AimTune& aim_tune(int i) { return AIM_TUNE[aim_level_of(i)]; }

static inline uint32_t rng_next(void) {
  uint32_t x = g.rng;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return g.rng = x;
}
static inline float rng_f(void) { return (float)(rng_next() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
static inline float rng_s(void) { return rng_f() * 2.0f - 1.0f; }                             // [-1, 1)
// Vettore casuale (x e z in [-sx, sx) e [-sz, sz), y in [y0, y0 + sy)). Le chiamate al generatore
// vanno sempre in istruzioni separate: l'ordine di valutazione degli argomenti di una funzione
// non e' fissato dal C++ e due compilatori darebbero partite diverse.
static inline Vec3 rng_vec(float sx, float y0, float sy, float sz) {
  const float x = rng_s() * sx;
  const float y = y0 + rng_f() * sy;
  const float z = rng_s() * sz;
  return v3(x, y, z);
}
static inline float dist2d(Vec3 a, Vec3 b) {
  const float dx = a.x - b.x, dz = a.z - b.z;
  return sqrtf(dx * dx + dz * dz);
}
static inline Vec3 dir_yaw(float yaw) { return v3(d_sin(yaw), 0.0f, d_cos(yaw)); }
static inline float yaw_of(float dx, float dz) { return d_atan2(dx, dz); }
static inline bool pad_down(int i, int b) { return (g.in[i].btn >> b) & 1u; }
static inline bool pad_pressed(int i, int b) { return ((g.in[i].btn & ~g.in[i].prev) >> b) & 1u; }

// --- game.cpp ---
int count_active(void);
int count_alive(void);
int count_in_play(void);      // attivi e non ancora senza vite
int count_monsters(void);
void show_msg(const char* big, const char* small, uint32_t rgb, int size, int frames);
float diff_hp(void);          // moltiplicatori dei mostri secondo il giro della campagna
float diff_dmg(void);
float diff_speed(void);
void start_map(int idx);      // carica la mappa e comincia dalla zona 1
void go_screen(int s);
void game_request_quit(void);
void update_camera(bool snap);
bool load_map_preview(int idx);
Vec3 menu_spot(int i);        // dove sta il personaggio del giocatore i nei menu
// Sonoro: un effetto in un punto del mondo (sinistra/destra e volume dalla posizione rispetto
// all'inquadratura) o dei menu (al centro). Solo uscita: non cambia nulla della partita.
void snd_at(int id, Vec3 pos);
void snd_ui(int id);
void settings_changed(void);  // mira, antialias o volumi cambiati (gia' in save_data): si scrivono alla chiusura della pausa
void stars_late_explorer(void);   // esploratore salvato a mappa gia' finita (festa di fine mappa)
// Pianeti e giri (dati nel salvataggio, mappe da maps.h).
int world_giro(int w);            // giro del pianeta w, da 1
int world_maps(int w);            // mappe del pianeta w nella campagna
int world_stars(int w);           // stelle del pianeta w nel giro in corso
int giro_need(int w);             // stelle che servono per il giro dopo del pianeta w (GIRO_STARS con 4 mappe)
int world_first_map(int w);       // indice della prima mappa del pianeta (-1 se non ne ha)
int map_in_world(int i);          // posizione della mappa i nel suo pianeta (0 = la prima)
bool map_unlocked(int i);         // la mappa i si puo' giocare?
int star_count(uint8_t s);        // quante stelle in una maschera SAVE_STAR_*
extern const char* const WORLD_NAMES[NWORLDS];   // "TERRA", "LUNA"...
// --- player.cpp ---
void player_join(int i, int ch, bool mid_game);
void player_leave(int i);
void players_gather(void);    // fine mappa: chi e' a terra (o senza vite) raggiunge i compagni per la festa
void player_update(int i, const Pad& pad);
void place_players(int z);
Vec3 player_chest(const Player& p);
bool player_visible_from(int i, Vec3 from);
void hurt_player(int i, float dmg, Vec3 from, float knock);
void heal_player(int i, float amount);
void push_player(int i, float vx, float vz, int frames);
// --- combat.cpp ---
Shot* shot_new(int kind, int owner, Vec3 pos, Vec3 vel);
void shots_update(void);
void damage_monster(int k, float dmg, int owner, Vec3 dir, float knock, bool charges);
void damage_boss(float dmg, int owner, bool charges);
// elem: EL_FIRE per la lava di Zolfo (VAPORE sui mostri rallentati), EL_NONE per il resto.
void explode(Vec3 p, float radius, float dmg, int owner, float knock, bool big, int elem = EL_NONE);
bool bomb_drop(int i);
void bombs_update(void);
void items_reset(int zone);   // zona < 0: tutta la mappa
void items_update(void);
void crate_damage(int d, float dmg, Vec3 from);
void burst(Vec3 p, int n, int col, float speed);
void puff(Vec3 p, int kind, int col, Vec3 vel, float size, int life);
void add_float(Vec3 p, uint32_t rgb, int key, int value, char sign);
void add_float_text(Vec3 p, uint32_t rgb, const char* txt);   // scritta che sale (al massimo 9 caratteri)
void explorer_update(void);   // l'esploratore chiama aiuto; poi, salvato, festeggia e se ne va
void explorer_rescue(Vec3 c); // capsula aperta: stella dell'esploratore
void add_shake(float a);
Marker* marker_new(int kind, Vec3 pos, float r, int life);
void effects_update(void);
// --- monsters.cpp ---
bool monster_spawn(int type, bool summoned, const Vec3* where);
void monsters_update(void);
void monsters_clear(void);
float monster_r(const Monster& m);
float monster_h(const Monster& m);
Vec3 monster_center(const Monster& m);
bool monster_drawn(const Monster& m);   // nascosto nell'erba alta lontano dai giocatori?
void boss_start(void);
void boss_update(void);
float boss_r(void);
float boss_speed(void);                 // camminata del gladiatore nella fase in corso (m/s)
Vec3 boss_javelin_start(const Boss& b); // da dove partono i giavellotti (il disegno parte dalla mano)
bool boss_force_act(int act);           // prove: comincia subito l'azione act verso il bersaglio
// --- flow.cpp ---
// Campo di distanze su una griglia di celle da mezzo metro: i mostri lo seguono per arrivare ai
// giocatori girando attorno agli ostacoli, i giocatori automatici per andare dove vogliono.
#define FLOW_W (LV_MAX_W * 2)
#define FLOW_H (LV_MAX_H * 2)
#define FLOW_FAR 0xFFFF
void flow_build(uint16_t* field, const Vec3* goals, int ngoals, int zone);   // zone < 0: tutta la mappa
bool flow_dir(const uint16_t* field, float x, float z, float* dx, float* dz);
bool walk_clear(Vec3 a, Vec3 b, float r, bool flying);
// --- menu.cpp ---
void menu_update(void);
void play_menus_update(void);  // pausa e finestre per entrare, durante la partita
void menu_build_hud(Hud* h, const Camera& cam);
void play_menus_hud(Hud* h);
// --- game_draw.cpp ---
bool to_screen(const Camera& c, Vec3 p, int* sx, int* sy);
// Faccia dell'eroe ch (disegni di tools/make_font.py) in un quadrato di lato 8 * size da (x, y):
// colori veri, spenti (a terra) o grigi (senza vite).
enum { FACE_NORMAL = 0, FACE_DOWN, FACE_OUT };   // aspetto delle facce (ui::face)
// --- bot.cpp ---
void bot_reset(void);
void bot_set_lag(int frames);   // prove: i giocatori automatici reagiscono ai segni a terra in ritardo, come una persona
// --- cards.cpp ---
bool cards_enabled(void);
void cards_map_start(void);           // nuova mappa (o mappa da capo): via tutte le carte
void cards_offer(void);               // apre la scelta (se le carte sono attive) per chi ha ancora vite
void cards_zone_cleared(void);        // zona ripulita e ne segue un'altra: la scelta si apre tra poco
void cards_zone_start(void);          // inizio (o nuovo inizio) di una zona: si ricaricano gli scudi
void cards_update(void);              // scelta aperta: cursori e conferme di tutti, poi si chiude
void cards_hud(Hud* h);               // riquadri della scelta (al posto della pausa)
void card_lose_all(int i);            // chi lascia la partita perde le sue carte
uint8_t card_shot_fx(int i, int kind);       // SFX_* dei colpi di questo tipo del giocatore i
void card_shot_mods(int i, Shot* s);         // colpo normale appena sparato: danni, PERFORANTE, RIMBALZO, combo
void card_shot_hit(int k, const Shot& s);    // un colpo di un giocatore ha preso il mostro k (ancora vivo)
void card_elem_hit(int k, int owner, int elem);   // fuoco o neve sul mostro k: forse VAPORE
void card_burn_tick(int k);                  // mostro che brucia: danni e fiammelle
bool card_shield_hit(int i);                 // SCUDO: true se il colpo si ferma
void card_special_used(int i);               // SPECIALE GENEROSO
bool cards_layout_ok(char* why, int n, bool all = false);   // prove: nomi e descrizioni stanno nei riquadri (all: li scrive tutti)
