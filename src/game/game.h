// Gioco: cooperativa da 1 a 4 giocatori, mappe fatte di zone da ripulire dai mostri.
#pragma once
#include "engine/render.h"

#define PUPAZZI_VERSION "0.5.4"   // unico posto: core, menu e Makefile la leggono da qui
#define MAX_PLAYERS 4

// The whole state of a game (defined in game/sim/game_int.h). The frontend creates one with
// game_create and passes it to every game_* function.
struct Game;
Game* game_create(void);
void game_destroy(Game* g);

// What the game announces while it runs (Observer): sounds and camera moves. The game never reads
// anything back from whoever listens (the view of this console, game/view/view.h); without a
// listener (tests) the events are simply dropped. Set once: it outlives game_init.
enum { CAM_FOLLOW = 0, CAM_CUT, CAM_CUT_PLAY };    // follow the game; jump there at once; ...from the play pitch
struct GameEvents {
  void* ctx;
  void (*sound_at)(void* ctx, int id, Vec3 pos);   // a sound at a point of the world
  void (*sound_ui)(void* ctx, int id);             // a menu or announcement sound
  void (*camera)(void* ctx, const Game& g, int how);
};
void game_set_events(Game& g, const GameEvents& ev);

// Personaggi disponibili (i 4 iniziali, uno per mondo).
enum { CH_ASTRO = 0, CH_MOCHI, CH_MARZIO, CH_ZOLFO, NCHARS };

// Stesso ordine degli identificatori RetroPad di libretro.
enum {
  PAD_B = 0, PAD_Y, PAD_SELECT, PAD_START, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
  PAD_A, PAD_X, PAD_L, PAD_R, PAD_L2, PAD_R2, PAD_L3, PAD_R3
};

struct Pad {
  uint16_t btn;          // maschera di bit dei tasti
  float lx, ly, rx, ry;  // levette in [-1, 1]; y positivo = verso il basso
};

struct GameInfo {
  int screen, map, loop, zone, nzones, enemies, players, alive, phase;   // loop: giro del pianeta (da 1)
  int kills, deaths, wipes, maps_done;     // wipes: zone rifatte (da soli, caduti con vite rimaste)
  int restarts;                            // mappe ricominciate da capo
  int lives[MAX_PLAYERS];                  // vite rimaste di ogni giocatore (-1 se non gioca)
  char map_name[40];
};

void game_init(Game& g);
void game_shutdown(Game& g);
void game_reset(Game& g);                              // di nuovo dal titolo
// Prove: subito in partita sulla mappa map (0 = la prima), senza menu; chars sceglie i
// personaggi (per esempio "0213"), NULL o vuoto: uno diverso per giocatore.
void game_quick_start(Game& g, int map, int players, const char* chars);
void game_update(Game& g, const Pad pads[MAX_PLAYERS]);      // un passo di 1/60 di secondo
void game_start_tap(Game& g, int player);                    // START premuto e lasciato subito
bool game_paused(Game& g);
bool game_playing(Game& g);                            // (0.5.4) in partita (non nei menu, non in pausa, niente carte)
bool game_wants_quit(Game& g);                         // scelto "Esci" dal titolo
bool game_player_active(Game& g, int i);
uint32_t game_player_rgb(int i);
// Quanto e' pronta la mira del giocatore (0 extra morbida, 1 morbida, 2 media, 3 rapida; si sceglie nella pausa).
void game_set_aim_level(Game& g, int player, int level);
const char* game_aim_name(Game& g, int player);
// Antialias: il renderer dice se lo sa fare, il gioco se e' scelto (voce della pausa).
void game_set_aa_available(Game& g, int max_samples);        // 0: il renderer non lo sa fare; altrimenti fino a quanti campioni
void game_set_aa_level(Game& g, int level);                  // come la voce della pausa: 0 spento, 1 il migliore, 4, 8
int game_aa_level(Game& g);
void game_aa_cap(Game& g, int samples);                      // (0.5.4) in questa sessione al massimo tanti campioni (8x troppo pesante)
int game_aa_wanted(const Game& g);                           // campioni da usare adesso: 0, 4 o 8
// Chiamata dal core subito dopo save_open (retro_load_game): il gioco prende le impostazioni dal salvataggio (save.h).
void game_save_loaded(Game& g);
// Carte (dalla 0.4): all'avvio attive; spente il gioco va esattamente come prima (prove).
void game_set_cards_enabled(Game& g, bool on);
void game_info(Game& g, GameInfo* gi);
uint64_t game_state_hash(Game& g);                     // per confrontare partite tra processori diversi
void game_trace_monsters(Game& g, void (*out)(const char* line));
// Solo per le prove automatiche: i primi n giocatori li guida il computer.
void game_bot_pads(Game& g, Pad pads[MAX_PLAYERS], int n);
// ... oppure un giocatore solo (in rete: quello di questa console), senza toccare gli altri.
void game_bot_pad(Game& g, int player, Pad* out);

// --- gioco in rete (0.5.4, vedi net.h) -------------------------------------------------------------
// Il core passa al gioco cosa c'e' in rete (pagine GIOCO IN RETE e sala d'attesa) e il gioco gli
// chiede di creare, cercare, unirsi, uscire e cominciare. In partita la stessa partita gira su ogni
// console: game_net_begin la fa partire uguale dappertutto, game_net_frame prima di ogni
// game_update porta chi si e' scollegato e gli START brevi di tutti.
#define NET_MAX_GAMES 6
#define NET_NAME_LEN 13                             // come SAVE_NAME_LEN
enum { NG_OPEN = 0, NG_FULL, NG_STARTED, NG_VERSION, NG_MAPS };   // stato di una partita trovata
enum { NV_OFF = 0, NV_HOST, NV_SEARCH, NV_JOINING, NV_JOINED };   // la console nella sala d'attesa
// Errori e fine della partita in rete (cosa dire).
enum { NE_NONE = 0, NE_VERSION, NE_MAPS, NE_FULL, NE_STARTED, NE_NO_ANSWER, NE_HOST_LEFT, NE_LOST, NE_KICKED, NE_DESYNC,
       NE_SLOW, NE_SOCKET, NE_ALONE, NE_OVER, NE_CLOSED };
struct NetGame {
  char name[NET_NAME_LEN];       // nome della partita ("" = PARTITA slot+1)
  int slot;                      // salvataggio di chi l'ha creata (0..3)
  int world, giro;               // pianeta piu' lontano raggiunto e suo giro
  int players;                   // console nella sala d'attesa, chi l'ha creata compreso
  int state;                     // NG_*
  char version[16];              // versione del gioco su quella console
};
struct NetView {
  int mode;                      // NV_*
  char ip[16];                   // indirizzo di questa console nella rete ("" nessuna rete)
  int ngames;                    // NV_SEARCH: partite trovate
  NetGame games[NET_MAX_GAMES];
  NetGame game;                  // NV_HOST: la propria; NV_JOINING, NV_JOINED: quella scelta
  uint8_t present;               // sala d'attesa: posti con una console (bit)
  int local;                     // sala d'attesa: posto di questa console
  int error, error_seq;          // ultimo errore (NE_*) e quanti finora (per accorgersi di uno nuovo)
};
void game_net_view(Game& g, const NetView* v);
enum { NR_NONE = 0, NR_HOST, NR_SEARCH, NR_JOIN, NR_LEAVE, NR_START };
int game_net_request(Game& g, int* arg);                     // dopo game_update: cosa chiedono i menu (NR_*, una alla volta)
void game_net_summary(NetGame* out);                // la partita attiva: nome, posto, pianeta e giro
void game_net_lobby(Game& g);                          // i menu nella sala d'attesa (prove: PUPAZZI_RETE)
struct NetStart;
void game_net_prepare(Game& g, NetStart* s);                 // numero per mescolare, mappa col cursore, mire di questa console
struct NetStart {
  uint32_t seed;
  int local;                     // giocatore di questa console
  uint8_t consoles;              // posti con una console (bit)
  uint8_t aim[MAX_PLAYERS];      // mira di ogni giocatore (quella salvata sulla sua console)
  int map_sel;                   // mappa col cursore (quella di chi ha creato la partita)
};
void game_net_begin(Game& g, const NetStart* s);             // su tutte: partita in rete da capo, a COME SI GIOCA?
void game_net_frame(Game& g, uint8_t consoles, uint8_t silent, const bool taps[MAX_PLAYERS]);
bool game_net_over(Game& g);                           // la partita in rete e' tornata al titolo: finita per tutti
// Fine su questa console (NE_*: cosa dire). Chi ha creato la partita, rimasto solo, continua da solo;
// le altre tornano al titolo con i loro salvataggi.
void game_net_end(Game& g, int reason, bool host);
int game_net_local(Game& g);                           // giocatore di questa console in rete (-1: non in rete)
void game_net_wait(Game& g, int frames);                     // solo per l'HUD: da quanti fotogrammi si aspetta la rete
