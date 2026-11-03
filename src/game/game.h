// Game: co-op for 1 to 4 players, maps made of zones to clear of monsters.
#pragma once
#include "engine/render.h"

#define PUPAZZI_VERSION "0.5.5"   // the only place: core, menus and Makefile read it from here
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

// Available characters (the 4 starting ones, one per world).
enum { CH_ASTRO = 0, CH_MOCHI, CH_MARZIO, CH_ZOLFO, NCHARS };

// Same order as libretro's RetroPad identifiers.
enum {
  PAD_B = 0, PAD_Y, PAD_SELECT, PAD_START, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
  PAD_A, PAD_X, PAD_L, PAD_R, PAD_L2, PAD_R2, PAD_L3, PAD_R3
};

struct Pad {
  uint16_t btn;          // bit mask of the buttons
  float lx, ly, rx, ry;  // sticks in [-1, 1]; positive y = down
};

struct GameInfo {
  int screen, map, loop, zone, nzones, enemies, players, alive, phase;   // loop: round of the planet (from 1)
  int kills, deaths, wipes, maps_done;     // wipes: zones replayed (alone, fallen with lives left)
  int restarts;                            // maps restarted from the start
  int lives[MAX_PLAYERS];                  // lives left of each player (-1 if not playing)
  char map_name[40];
};

void game_init(Game& g);
void game_shutdown(Game& g);
void game_reset(Game& g);                              // back to the title
// Tests: straight into a game on map map (0 = the first), without menus; chars chooses the
// characters (for example "0213"), NULL or empty: a different one per player.
void game_quick_start(Game& g, int map, int players, const char* chars);
void game_update(Game& g, const Pad pads[MAX_PLAYERS]);      // one step of 1/60 of a second
void game_start_tap(Game& g, int player);                    // START pressed and released at once
bool game_paused(Game& g);
bool game_playing(Game& g);                            // (0.5.4) in a game (not in the menus, not paused, no cards)
bool game_wants_quit(Game& g);                         // "Esci" chosen from the title
bool game_player_active(Game& g, int i);
uint32_t game_player_rgb(int i);
// How responsive the player's aim is (0 extra soft, 1 soft, 2 medium, 3 quick; chosen in the pause).
void game_set_aim_level(Game& g, int player, int level);
const char* game_aim_name(Game& g, int player);
// Antialiasing: the renderer says whether it can do it, the game whether it is chosen (pause item).
void game_set_aa_available(Game& g, int max_samples);        // 0: the renderer cannot do it; otherwise up to how many samples
void game_set_aa_level(Game& g, int level);                  // like the pause item: 0 off, 1 the best, 4, 8
int game_aa_level(Game& g);
void game_aa_cap(Game& g, int samples);                      // (0.5.4) at most this many samples in this session (8x too heavy)
int game_aa_wanted(const Game& g);                           // samples to use now: 0, 4 or 8
// Called by the core right after save_open (retro_load_game): the game takes the settings from the save (save.h).
void game_save_loaded(Game& g);
// Cards (since 0.4): enabled at start-up; when off, the game runs exactly as before (tests).
void game_set_cards_enabled(Game& g, bool on);
void game_info(Game& g, GameInfo* gi);
uint64_t game_state_hash(Game& g);                     // to compare games across different processors
void game_trace_monsters(Game& g, void (*out)(const char* line));
// Only for the automatic tests: the first n players are driven by the computer.
void game_bot_pads(Game& g, Pad pads[MAX_PLAYERS], int n);
// ... or a single player (on the network: the one of this console), without touching the others.
void game_bot_pad(Game& g, int player, Pad* out);

// --- LAN play (0.5.4, see net.h) -------------------------------------------------------------------
// The core passes the game what is on the network (GIOCO IN RETE pages and waiting room) and the
// game asks it to create, search, join, leave and start. During play the same game runs on every
// console: game_net_begin starts it identically everywhere, game_net_frame before every
// game_update brings who has disconnected and everyone's short STARTs.
#define NET_MAX_GAMES 6
#define NET_NAME_LEN 13                             // same as SAVE_NAME_LEN
enum { NG_OPEN = 0, NG_FULL, NG_STARTED, NG_VERSION, NG_MAPS };   // state of a game found
enum { NV_OFF = 0, NV_HOST, NV_SEARCH, NV_JOINING, NV_JOINED };   // the console in the waiting room
// Errors and end of the LAN game (what to say).
enum { NE_NONE = 0, NE_VERSION, NE_MAPS, NE_FULL, NE_STARTED, NE_NO_ANSWER, NE_HOST_LEFT, NE_LOST, NE_KICKED, NE_DESYNC,
       NE_SLOW, NE_SOCKET, NE_ALONE, NE_OVER, NE_CLOSED };
struct NetGame {
  char name[NET_NAME_LEN];       // name of the game ("" = PARTITA slot+1)
  int slot;                      // the host's save (0..3)
  int world, giro;               // farthest planet reached and its round
  int players;                   // consoles in the waiting room, the host included
  int state;                     // NG_*
  char version[16];              // version of the game on that console
  uint64_t id;                   // (0.5.6) identity of the host's game (save.h); 0: an older version
};
struct NetView {
  int mode;                      // NV_*
  char ip[16];                   // address of this console on the network ("" no network)
  int ngames;                    // NV_SEARCH: games found
  NetGame games[NET_MAX_GAMES];
  NetGame game;                  // NV_HOST: its own; NV_JOINING, NV_JOINED: the chosen one
  uint8_t present;               // waiting room: slots with a console (bits)
  int local;                     // waiting room: slot of this console
  int error, error_seq;          // last error (NE_*) and how many so far (to notice a new one)
  int save;                      // (0.5.6) NV_JOINING, NV_JOINED: slot where this console saves the game too (-1 no)
};
void game_net_view(Game& g, const NetView* v);
// NR_JOIN: argument the game in the list of those found, plus 256 times (slot + 1) to save it on this
// console too, in that slot (0.5.6).
enum { NR_NONE = 0, NR_HOST, NR_SEARCH, NR_JOIN, NR_LEAVE, NR_START };
int game_net_request(Game& g, int* arg);                     // after game_update: what the menus ask for (NR_*, one at a time)
void game_net_summary(NetGame* out);                // the active game: name, slot, planet and round
void game_net_lobby(Game& g);                          // the menus in the waiting room (tests: PUPAZZI_RETE)
struct NetStart;
void game_net_prepare(Game& g, NetStart* s);                 // number for shuffling, map under the cursor, aims of this console
struct NetStart {
  uint32_t seed;
  int local;                     // player of this console
  uint8_t consoles;              // slots with a console (bits)
  uint8_t aim[MAX_PLAYERS];      // aim of each player (the one saved on their console)
  int map_sel;                   // map under the cursor (the host's)
};
void game_net_begin(Game& g, const NetStart* s);             // on all of them: LAN game from the start, at COME SI GIOCA?
void game_net_frame(Game& g, uint8_t consoles, uint8_t silent, const bool taps[MAX_PLAYERS]);
bool game_net_over(Game& g);                           // the LAN game went back to the title: over for everyone
// End on this console (NE_*: what to say). The host, left alone, carries on alone; the others go
// back to the title with their saves.
void game_net_end(Game& g, int reason, bool host);
int game_net_local(Game& g);                           // player of this console on the network (-1: not on the network)
void game_net_wait(Game& g, int frames);                     // only for the HUD: how many frames we have been waiting for the network
