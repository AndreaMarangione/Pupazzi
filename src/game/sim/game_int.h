// Internal state of the game, shared between the modules: rules and flow (game.cpp), characters
// (player.cpp), shots, bombs and effects (combat.cpp), monsters and bosses (monsters.cpp), paths
// (flow.cpp), menus (menu.cpp), drawing (game_draw.cpp) and bots (bot.cpp).
// Everything advances in fixed steps of 1/60 of a second and uses only the math of dmath.h: the
// same game, bit for bit, on every processor.
#pragma once
#include "game/game.h"
#include "engine/lang.h"
#include "game/sim/level.h"
#include "game/sim/shapes.h"
#include "engine/audio.h"
#include "game/save.h"

#define MAX_MONSTERS 56          // <= 64: shots that pass through remember the ones they hit in a uint64_t
#define MAX_SHOTS 256
#define MAX_PARTICLES 420
#define MAX_FLOATS 40
#define MAX_BOMBS 24
#define MAX_MARKERS 160
#define MAX_BOMBS_CARRIED 3

const float DT = 1.0f / 60.0f;
const float GRAVITY = 22.0f;
const float AIM_FORESHORTEN = 0.83f;    // sin of the camera's tilt: aim "as on the screen"

// --- characters ---------------------------------------------------------------

const float PL_R = 0.32f, PL_H = 1.4f, PL_STEP = 0.36f;
const float PL_ACCEL = 0.40f, PL_JUMP_V = 7.1f, PL_TURN = 20.0f;
const float HEAL_KILL = 8.0f, HEAL_TALISMAN = 35.0f;
const int RESPAWN_FRAMES = 300, INVULN_FRAMES = 90;
const float RESPAWN_HP = 0.6f;
// Lives (17/10): 3 each per map (VITA EXTRA: one more). Playing alone, every fall replays the
// zone and the last one restarts the map from the start; with more players whoever falls comes
// back after RESPAWN_FRAMES while they have lives, then stays out (out) until the end of the map;
// everyone down at once: map restarted from the start.
const int START_LIVES = 3;
const int OUT_VANISH = 100;       // whoever is left out of lives vanishes in a puff after this many frames down
const int WIPE_ZONE_FRAMES = 150, WIPE_MAP_FRAMES = 180;   // after "down": zone replayed / map restarted from the start
// Rounds (17/10): every planet has its own round (save.h); beating the boss with at least
// GIRO_STARS stars of the current round opens the next round, harder, and the stars start again
// from zero.
const int GIRO_STARS = 10;
const float SPAN_X = 31.0f, SPAN_Z = 21.0f;   // how far apart the players can get: what the camera frames at 30 m (17/10)
// Power-ups in the 'x' crates (0.5.4, decided with Andrea on 29/10): breaking a crate makes a
// random one pop out, and whoever walks over it takes it. VELOCITA' (+40% for 10 s), FURIA (double
// damage for 10 s), INVINCIBILE (no damage for 6 s; not SCUDO, which is a card), CARICA (special
// ready at once).
enum { PW_SPEED = 0, PW_FURY, PW_INVINCIBLE, PW_CHARGE, NPOWERS };
const int NPW_TIMED = 3;                      // the first three have a duration (Player::pw_t); CARICA is instant
const int PW_FRAMES[NPW_TIMED] = { 600, 600, 360 };
const float PW_SPEED_K = 1.4f, PW_FURY_K = 2.0f;
const int PW_WARN = 120;                      // in the last 2 seconds the power-up blinks
extern const char* const PW_NAME[NPOWERS];    // "VELOCITA'!"... (N_, for add_float_text)
const int FROZEN_FRAMES = 30 * 60;            // controller stuck in the same state: the character stops

enum { AIM_LINE = 0, AIM_FAN, AIM_LOB, AIM_AREA };

// Aim with the right stick (player.cpp). Every player chooses in the pause how responsive it is:
//  - on: tilt beyond which the player aims (circular dead zone);
//  - min_cut, beta: filter on the direction (modelled on the "1 euro" filter). When the stick
//    points where the aim already is, the cutoff is min_cut Hz (the jitter goes away); the farther
//    the stick is from the aim, the higher the cutoff: beta Hz for every radian per second at which
//    the aim would have to move to reach it (wide flicks stay fast).
enum { AIM_XSOFT = 0, AIM_SOFT, AIM_MEDIUM, AIM_QUICK, NAIMLEVELS };
const int AIM_DEFAULT = AIM_SOFT;   // chosen by Andrea on 15/10
struct AimTune { const char* name; const char* label; float on, min_cut, beta; };
extern const AimTune AIM_TUNE[NAIMLEVELS];
const float AIM_FULL = 0.95f;     // tilt that counts as "full" (full range for lobbed shots)
const float AIM_DROP = 0.10f;     // the tilt drops by more than this in one frame: maybe the stick was let go
const float AIM_RELEASE = 0.3f;   // ...and if over the last AIM_HIST frames it dropped by more than this, it really was
const int AIM_HIST = 4;
const float AIM_D_CUT = 5.0f;     // filter on the aim's speed (Hz)
const int AIM_REARM = 12;         // after letting it go, for this many frames 2 readings in a row are needed
static inline float aim_k_of(float m, float on) { return clampf((m - on) / (AIM_FULL - on), 0.15f, 1.0f); }

struct CharDef {
  const char* name;        // name in capitals for the menus
  const char* who;         // who they are
  const char* role;        // role in the team
  const char* normal;      // normal shot, in short
  const char* special;     // special, in short
  uint32_t color;
  float hp, speed;
  float ammo_max;          // notches of the bar (Zolfo: fuel)
  float ammo_cost;         // what one attack costs
  int fire_frames;         // between one attack and the next while holding R1
  int reload_wait;         // the bar starts refilling this many frames after the last attack
  int reload_frames;       // from empty to full
  float range;             // range of the normal shot
  uint8_t aim;             // shape of the aim strip of the normal shot
  float aim_w;             // width of the strip or opening of the fan
  float special_cost;      // damage to deal with the normal shot to charge the special
  uint8_t aim_sp;          // shape of the special's aim
  float sp_range, sp_w;
};
extern const CharDef CHARS[NCHARS];

// --- monsters -----------------------------------------------------------------

struct MonDef {
  const char* name;
  float hp, speed, dmg, r, h;
  bool flying;
};
extern const MonDef MONS[MO_NTYPES];

// --- shots --------------------------------------------------------------------

enum ShotKind {
  SK_BULLET = 0,   // Astro: shot of the big rifle
  SK_ROCKET,       // Astro, special: rocket that homes in on the monsters
  SK_SHARD,        // Mochi: moon shard (also those of the special)
  SK_FROST,        // Marzio: slowing ball of dry ice
  SK_FLAME,        // Zolfo: flame that passes through the monsters
  SK_LAVA,         // Zolfo, special: lobbed lava bubble
  SK_ARROW,         // terracotta archer: slow lobbed arrow
  SK_JAVELIN,      // giant gladiator: lobbed javelin
  NSHOTKINDS
};

enum { PH_FIGHT = 0, PH_ENTER, PH_CLEAR, PH_MAP_DONE, PH_WIPE };
// SC_NET: GIOCO IN RETE (create or join); SC_LOBBY: waiting room (0.5.4, only on this console:
// the LAN game starts afterwards, from COME SI GIOCA?).
enum { SC_TITLE = 0, SC_MODE, SC_MAPS, SC_CHARS, SC_PLAY, SC_STATS, SC_SLOTS, SC_LANG, SC_NAME, SC_NET, SC_LOBBY, NSCREENS };
// First screen (0.5.3): new game, load, LAN game (0.5.4), manage saves, language, quit.
enum { TI_NEW = 0, TI_LOAD, TI_NET, TI_MANAGE, TI_LANG, TI_QUIT, TITLE_ITEMS };
// Saves screen: to load one, to manage them (duplicate, delete) or, with a new game and the slots
// full, to choose which one to overwrite; on the network (0.5.4), which one to host the game with.
// Popups on top: actions, confirmation.
// SL_JOIN (0.5.6): joining a LAN game to save it here too, with all the slots full: which one to replace.
enum { SL_LOAD = 0, SL_MANAGE, SL_OVERWRITE, SL_NET, SL_JOIN };
enum { SP_NONE = 0, SP_ACTIONS, SP_DELETE, SP_OVERWRITE };
// Items of the actions popup (0.5.4: also RINOMINA).
enum { SA_COPY = 0, SA_RENAME, SA_DELETE, SA_COUNT };
// Keyboard for the game's name (0.5.4): new game (before creating it), new name, new game to be
// created on the network.
enum { NM_NEW = 0, NM_RENAME, NM_NET };
// Pause items: resume, controls (0.5.2: opens the box with the buttons), aim, graphics (0.5.6: opens
// the GRAFICA popup; antialiasing before), music, sound effects, leave the game, back to the menu.
enum { PS_RESUME = 0, PS_CONTROLS, PS_AIM, PS_GFX, PS_MUSIC, PS_SFX, PS_LEAVE, PS_MENU, PAUSE_ITEMS };
// Items of the GRAFICA popup (0.5.6), settings of this console: camera distance and pitch,
// antialiasing, resolution, details, TV margin.
enum { GF_DIST = 0, GF_PITCH, GF_AA, GF_RES, GF_DETAIL, GF_MARGIN, GFX_ITEMS };

struct Player {
  bool active, alive, grounded, frozen, hidden;
  uint8_t ch;
  Vec3 pos;                   // point on the ground between the feet
  float vx, vz, vy, yaw;
  float hp, hp_max, speed;
  float ammo; bool locked;
  int fire_cd, reload_wait;
  float charge;               // damage dealt with the normal shot since the last special
  int burst_left, burst_cd;   // Astro: shots of the burst still to be fired
  float burst_yaw;
  bool aiming;                // right stick tilted
  float aim_yaw, aim_k;       // where the aim points (to turn, shoot and draw) and how far the stick is tilted (0..1)
  float aim_f, aim_dx;        // filter state: filtered direction and its speed (rad/s)
  float aim_m;                // stick tilt in the previous frame
  float aim_hy[AIM_HIST], aim_hm[AIM_HIST];   // last frames of aim: direction and tilt
  int aim_nh;
  int aim_on_n;               // readings in a row beyond the threshold
  int aim_off_t;              // frames since aiming stopped
  unsigned aim_tick;          // last frame in which the stick was read
  int aim_hold;               // stays turned towards where it attacked
  int bombs;
  int respawn, invuln, hurt, recoil, down, reveal, land;
  float phase, amp;           // walk
  float sq, sq_v;             // squash & stretch: 0 = normal shape
  int leap_t, leap_n;         // Mochi: leap of the special (frames elapsed / total)
  Vec3 leap_a, leap_b;
  int blizzard_t;             // Marzio: storm active
  int flame_t;                // Zolfo: is spitting fire
  int tramp_t;                // launched by a trampoline
  float tvx, tvz;
  float kx, kz;               // push from an explosion or a strong hit (it fades)
  uint16_t prev_btn;
  Pad last_pad;
  int same_t;
  // Stats of the current map.
  int kills, deaths;
  float dmg_done;
  // Popup to join a game that has already started.
  bool joining;
  int join_pick;
  // Cards of the current map (cards.cpp).
  uint32_t cards;             // cards taken (CA_* bits)
  bool shield;                // SCUDO charged: the next hit is stopped
  // Lives of the current map.
  int lives;                  // lives left (falling takes one away)
  bool out;                   // out of lives: down until the end of the map (or until it restarts)
  // Crate power-ups (0.5.4): frames left of VELOCITA', FURIA, INVINCIBILE (0: none).
  int pw_t[NPW_TIMED];
};

struct Monster {
  bool alive, direct, los, summoned;
  uint8_t type;
  int8_t target;
  Vec3 pos;                   // feet (for those who fly, the point on the ground below)
  float vx, vz, kx, kz, yaw, fly;
  float hp, hp_max, speed, dmg;
  int rise, cd, windup, hurt, shown, repath, state, state_t, strafe_t, reveal, slow_t, los_t;
  float strafe, phase, slow_k, sq, sq_v;
  float cdx, cdz;             // scorpion: direction of the charge; archer: where it aims (x, z)
  uint8_t hitmask;            // players already hit in this charge or swipe; archer: bit 0, the next shot is a burst
  Vec3 last_seen;
  int burn_t;                 // combo cards: keeps burning for this many frames
  int8_t burn_by;             // ...set alight by this player
  int vapor_t;                // VAPORE: frames before it can burst again
  int8_t push_by;             // player of the last push, plus one (0 none): the kill is theirs if it falls into the cenote
};

// Giant gladiator (0.5.1): chases the target and fights. Actions (monsters.cpp; the poses are in
// game_draw.cpp): lobbed javelins, ground slam (the crack along a line), reinforcements from the
// trapdoors, slash in front of itself, shield charge, stunned after a charge into an obstacle.
enum { BA_NONE = -1, BA_JAVELIN = 0, BA_SLAM, BA_SUMMON, BA_SWEEP, BA_RUSH, BA_STUN, BA_COUNT };
const float BOSS_SWEEP_R = 4.0f;                            // slash: 4 m sector in front (BOSS_SWEEP_HALF in models.h)
const float BOSS_LANE_HW = 1.25f;                           // charge: half width of the lane (its radius)
const float BOSS_STUN_DMG = 1.5f;                           // stunned: hits deal 50% more
const int BOSS_STUN_FRAMES = 90;

struct Boss {
  bool alive;
  int type;
  Vec3 pos;
  float hp, hp_max;
  int stage;                  // 0, 1, 2: faster and faster
  int cd, act, act_t, hurt, shown, roar;   // cd: pause before the next attack
  float sq, sq_v, sway;
  int target;                 // player it chases (-1 none)
  int target_t;               // frames with the same target
  float aggro[MAX_PLAYERS];   // recent damage of each player (it fades): whoever hits hard gets chased
  float face;                 // where it faces: slash and charge start on this side
  float vx, vz;               // walking velocity
  // Current action: wind-up (shorter at every stage), direction, charge.
  int wind;                   // wind-up frames (until the hit)
  float act_yaw;              // direction of the attack
  float lane, run;            // charge: metres of the lane, metres already covered
  int end_t;                  // charge: act_t at which the run ended (then the recovery)
  bool crash;                 // charge: the lane ends against an obstacle
  uint8_t hitmask;            // players already hit by this attack
  unsigned crack_t[MAX_PLAYERS];   // when a crack last hit each one (only once per ground slam)
  int last[2];                // last two attacks (no long repetitions)
  int alt;                    // mid and long range choices: the charge and the other attack in turn
  int idle_t;                 // frames ready to attack without an attack suited to the distance
  int queued;                 // attack right after this one (javelins after a charge), BA_NONE none
  // Walk: straight or towards the path's waypoint, "stuck" check and side step.
  Vec3 way, check;
  int check_t, side_t, stuck_n;
  float side;
  int repath;
  bool direct;
  // Drawing (they change only with the game): walking step, frames since the defeat (it falls
  // and vanishes in the dust).
  float phase, amp;
  int dead_t;
};

struct Shot {
  bool alive;
  uint8_t kind;
  int8_t owner;               // 0..3 players, -1 monsters, -2 boss
  Vec3 pos, vel;
  float left;                 // metres left (straight shots)
  float dmg, r, yaw;
  int life, target;
  Vec3 dest;                  // lobbed shots: landing point
  uint64_t hit;               // monsters already hit (shots that pass through)
  bool hit_boss;
  bool special;               // shot of a special: does not charge the special
  uint8_t fx;                 // combo cards: SFX_BURN sets on fire, SFX_SLOW slows
  uint8_t pierce, bounce;     // PERFORANTE: monsters it can still pass through; RIMBALZO: bounces left
};
enum { SFX_BURN = 1, SFX_SLOW = 2 };

struct Bomb {
  bool alive, lit;
  int8_t owner;               // who dropped or lit it (-1 none)
  int8_t item;                // bomb of the map: index of the item, -1 dropped by a player
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

// Ground marks: where a lobbed shot lands, where a boss attack is about to spring up, explosions...
// Gladiator (0.5.1): MK_SWEEP the slash sector (centre pos, radius r, direction yaw), MK_LANE the
// charge lane (from pos towards yaw, len long, half width r); they fill up until fire_at.
enum { MK_LOB = 0, MK_ROOT, MK_RING, MK_BLAST, MK_SWEEP, MK_LANE };
struct Marker {
  bool alive;
  uint8_t kind;
  Vec3 pos;
  float r;
  int t, life;                // t: frames elapsed
  int fire_at;                // cracks: when the stones spring up; slash and charge: when the hit starts
  int open_at;                // cracks: when the crack opens
  bool done;
  bool crash;                 // charge: the lane ends against an obstacle (it crashes there)
  float yaw, len;             // cracks, slash, charge: direction; charge: length (m)
};

// Damage and healing numbers: hits close together on the same target add up.
struct FloatText { bool alive; int life, key, value; uint32_t rgb; Vec3 pos; char txt[16]; };

// Player buttons for the menus: nav is the direction just chosen (0 none, 1 up, 2 down,
// 3 left, 4 right), with auto-repeat while held down.
enum { NAV_NONE = 0, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT };
struct PadState { uint16_t btn, prev; float lx, ly; int nav, nav_t, navdir; };

// --- cards (cards.cpp) ----------------------------------------------------------
// At the start of every map and after every cleared zone (if another one follows) every player
// chooses 1 card out of 3; they last until the end of the map. The combos go to a character only
// if another player uses the given teammate.
enum CardId {
  CA_NONE = 0,
  CA_BOMBAROLO, CA_PASSO, CA_PELLE, CA_RICARICA, CA_SUPER, CA_PESANTI, CA_GITTATA, CA_MERENDA,
  CA_SCUDO, CA_VITA, CA_CALAMITA, CA_BOMBE, CA_GENEROSO, CA_PERFORANTE, CA_RIMBALZO,
  CA_COLPI_FUOCO, CA_COLPI_GELO, CA_SCHEGGE_FUOCO, CA_SCHEGGE_GELO, CA_VAPORE,   // combo
  NCARDS
};
struct CardDef {
  const char* name;        // as it is shown (capitals)
  const char* desc;        // description (ASCII)
  const char* brief;       // shorter, for the narrow boxes of 4 players (NULL: desc)
  bool combo;
};
extern const CardDef CARDS[NCARDS];
struct CardOffer {
  uint8_t card[3];
  int8_t n, sel;           // cards offered, item the cursor is on
  bool on, done;           // has an offer (was in the game when it opened) / has already chosen
  bool shown;              // (0.5.4) pressed A on the "RITIRA LE TUE CARTE" banner: sees their cards
};
const int CARDS_CLOSE_FRAMES = 20;   // when everyone has chosen, it closes after this long
const int CARDS_CLEAR_DELAY = 40;    // zone cleared: "ZONA LIBERA!" is shown, then the cards
const int CARDS_TIMEOUT = 60 * 60;   // whoever does not choose within a minute (pad off?) takes the one under the cursor
const int CARDS_BANNER_MIN = 15;     // (0.5.4) the banner has been up this long before A counts: no choices by mistake
enum { EL_NONE = 0, EL_FIRE, EL_SNOW };   // fire or snow shots (for VAPORE)

// Grid of the distance fields (flow.cpp): half-metre cells over the whole map.
#define FLOW_W (LV_MAX_W * 2)
#define FLOW_H (LV_MAX_H * 2)
#define FLOW_FAR 0xFFFF

// Bots (bot.cpp): what each automatic player is doing.
struct Bot {
  Vec3 last, goal;
  int check_t, side_t, jump_t, strafe_t, field_t, menu_t;
  float side, strafe;
  int orbit_t;                        // gladiator: frames before the direction of circling around it can change
  float orbit;                        // ...and the direction (+1 / -1)
  bool l1_prev, a_prev, field_ok;
  unsigned card_seq;                  // card choice already seen
  int card_t, card_want, card_wait;
  int cage_t;                         // how long they have been trying to open the explorer's capsule
  int heal_t, heal_off;               // how long they have been chasing a talisman / for how long to leave them alone
  uint16_t field[FLOW_W * FLOW_H];
};

// A save as the slot screens show it (game/screens/slots.cpp): the farthest planet reached with its
// round, the stars of that round, the open maps, the play time.
struct SlotView { int state, world, giro, stars, smax, open, total; uint32_t seconds; char name[SAVE_NAME_LEN]; };

// State that outlives game_init (until 0.5.4 it lived in module variables): session settings, caches
// that are rebuilt when needed, what this console sees of the network, the menus' notices. game_init
// keeps it; game_create sets its defaults.
struct GameSession {
  // game.cpp
  bool settings_dirty;          // pause settings to be saved
  NetView net_view;             // what is on the network (network pages)
  int net_req, net_req_arg;     // request of the menus to the core (NR_*)
  int net_local;                // player of this console in a LAN game (-1: none)
  int net_wait;                 // frames this console has been waiting for the network
  uint8_t net_silent;           // consoles that have not answered for a while
  bool quit;                    // Exit chosen from the title
  GameEvents events;            // who listens to the game (game_set_events)
  // bot.cpp
  Bot bot[MAX_PLAYERS];
  int bot_lag;                  // tests: the marks on the ground are seen this many frames late (bot_set_lag)
  // flow.cpp: blocked cells (1 blocked, 2 free but next to an obstacle, 0 free) and grid size
  uint8_t flow_blk[FLOW_W * FLOW_H];
  int flow_w, flow_h;
  // monsters.cpp: distance field towards the players, and the gladiator's (free cells, steps to the
  // target, Dijkstra heap)
  uint16_t mon_flow[FLOW_W * FLOW_H];
  bool mon_flow_ok;
  int mon_flow_t;
  uint8_t boss_free[FLOW_W * FLOW_H];
  uint16_t boss_field[FLOW_W * FLOW_H];
  int16_t boss_hpos[FLOW_W * FLOW_H];
  uint16_t boss_heap[FLOW_W * FLOW_H];
  int boss_hn, boss_bw, boss_bh, boss_free_t, boss_field_t, boss_crates;
  bool boss_field_ok;
  Vec3 boss_goal;
  // cards.cpp
  bool cards_on;                // game_set_cards_enabled: for the whole session
  // menu.cpp
  int info_t;                   // temporary notice in the menus
  char info[256];
  int net_err_seen;             // network errors already shown (NetView::error_seq)
  bool lobby_join;              // the waiting room is the one of a console that joined
  SlotView slot_view[SAVE_SLOTS];
  uint32_t level_serial;        // grows at every map load: the views build the geometry of the new one
};

struct Game {
  // Screens and menus.
  int screen, screen_t;
  int menu_sel, menu_owner;
  bool joined[MAX_PLAYERS], ready[MAX_PLAYERS];
  int pick[MAX_PLAYERS];
  int chars_t;
  int map_sel, map_shown, map_load_t;
  char map_note[120];
  int slots_mode, slots_sel, slots_pop, slots_pop_sel;   // saves (SL_*, slot under the cursor, SP_*, popup item)
  int lang_sel;
  int name_mode, name_slot, name_row, name_col;          // name keyboard (NM_*, slot, key under the cursor)
  bool name_overwrite;                                   // new game in place of another one (B goes back to the slots)
  char name_buf[SAVE_NAME_LEN];
  int pause_owner, pause_sel;
  bool paused;
  // LAN play (0.5.4): LAN game (the same on all the consoles), slots with a console connected,
  // "finire la partita in rete?" popup; game found under the cursor (waiting room).
  bool net;
  uint8_t net_consoles;
  bool net_pop;
  int lobby_sel;
  // (0.5.6) "Salvare la partita anche su questa console?" over the games found: the chosen game and
  // the slot where it would go (the one that already has it, or the first free one; -1 all full).
  bool lobby_pop;
  uint64_t join_id;
  uint32_t join_session;
  int join_slot;
  bool join_linked;
  bool pause_help;            // the controls box is open in the pause
  bool pause_gfx;             // (0.5.6) the GRAFICA popup is open in the pause...
  int gfx_sel;                // ...with this item selected (GF_*)
  uint8_t aim_level[MAX_PLAYERS];   // aim chosen by each player in the pause (AIM_XSOFT...)
  int aa_max, aa_level, aa_cap;     // antialiasing (graphics only): the renderer's maximum samples (0: none) /
                                    // pause choice (0, 1 the best, 4, 8) / session cap (8 or 4)
  // Campaign. loop: round of the current map's planet, from 0 (taken from the save when the map
  // starts; the monsters have more health, damage and speed at every round).
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
  uint8_t item_pw[LV_MAX_ITEMS];  // power-up that came out of that crate (PW_*), decided when it breaks
  float squash[LV_MAX_DYN];     // bushes and grass flattened by whoever walks through them
  int crate_hit[LV_MAX_DYN];    // crates just hit (they wobble)
  int tramp_t[LV_MAX_DYN];      // trampolines just used
  bool start_tap[MAX_PLAYERS];  // START pressed and released at once (from the core)
  int bots;                     // the first "bots" players are driven by the computer (tests)
  Vec3 last_vis[MAX_PLAYERS];   // where the monsters last saw each player
  int boss_seq;                 // boss attacks made so far (for the alternation)
  int zone, phase, phase_t;
  int wave, spawn_left[MO_NTYPES], spawn_cd, spawn_wait, enemies_left;
  uint32_t rng;
  unsigned tick, ui_tick;       // ui_tick advances also in the pause and in the menus
  int time_frames;              // frames towards the next second of play time of the active save
  PadState in[MAX_PLAYERS];
  float shake;                  // screen shake (explosions, slams): the view adds it to its camera
  float gate_want[LV_MAX_GATES];
  char msg[160], msg2[224];        // already in the chosen language (tr when the message is made)
  int msg_t, msg_size;
  uint32_t msg_rgb;
  int kills, deaths, wipes, maps_done;
  int restarts;                 // maps restarted from the start in this session
  bool wipe_cleared;
  bool wipe_map;                // PH_WIPE: at the end the map restarts from the start (otherwise only the zone)
  int mvp;
  int lives_kept[MAX_PLAYERS];  // lives of whoever left the game in this attempt (-1: nobody)
  bool vita_kept[MAX_PLAYERS];  // ...and whether they had taken VITA EXTRA (rejoining does not give it back)
  // Stats of whoever left the game in this map (also before a map restart): they get them back
  // when rejoining.
  bool stats_kept[MAX_PLAYERS];
  int kills_kept[MAX_PLAYERS], deaths_kept[MAX_PLAYERS];
  float dmg_kept[MAX_PLAYERS];
  // Cards: while the choice is open the game stays still as in the pause (ui_tick advances).
  bool cards_open;
  int cards_t;                  // frames since everyone chose
  int cards_age;                // frames since the choice opened
  int cards_due;                // zone just cleared: frames before the choice opens
  unsigned cards_seq;           // choices opened so far (for the bots)
  int boss_thirds;              // (0.5.6) thirds of the gladiator's health already rewarded with a choice in this map
  int boss_card_due;            // (0.5.6) thirds it has just lost: the choice opens at the end of the step (0 none)
  CardOffer offer[MAX_PLAYERS];
  // Hidden capsule with the explorer, and stars of the current map.
  bool explorer_free;           // capsule opened in this map
  int expl_t;                   // frames since they came out (-1: still in the capsule)
  Vec3 expl_pos;
  float expl_yaw;
  int help_t;                   // how long until they call "aiuto!" again
  int map_wipes;                // times the map restarted from the start in this visit (star 3)
  uint8_t stars_got, stars_new; // stars won at the end of the map / the new ones for the save
  uint8_t stars_had;            // stars of the map in the save before this time
  // Planet boss beaten: outcome of the round, for the end-of-map screen.
  bool giro_done;               // the round has been checked (end of the arena)
  bool giro_up;                 // next round opened (the planet's stars start again from zero)
  int giro_stars, giro_was;     // stars of the planet in the round just finished; that round
  GameSession session;          // keep it last: game_init clears everything before it
};




// Events (game.h): the game announces, the view of this console listens. Output only: nothing in the
// game depends on them.
static inline void ev_sound(const Game& g, int id, Vec3 pos) {
  if (g.session.events.sound_at) g.session.events.sound_at(g.session.events.ctx, id, pos);
}
static inline void ev_sound_ui(const Game& g, int id) {
  if (g.session.events.sound_ui) g.session.events.sound_ui(g.session.events.ctx, id);
}
static inline void ev_camera(const Game& g, int how) {
  if (g.session.events.camera) g.session.events.camera(g.session.events.ctx, g, how);
}

static inline bool has_card(const Player& p, int c) { return ((p.cards >> c) & 1u) != 0u; }
// Character values with the cards: used by the game, the drawing (aim, bars) and the bots.
// Without the card they stay exactly those of CHARS.
static inline float pl_speed(const Player& p) {
  const float s = has_card(p, CA_PASSO) ? CHARS[p.ch].speed * 1.15f : CHARS[p.ch].speed;
  return p.pw_t[PW_SPEED] > 0 ? s * PW_SPEED_K : s;   // VELOCITA' (crate power-up)
}
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

static inline int aim_level_of(const Game& g, int i) { const int l = g.aim_level[i]; return l < (int)NAIMLEVELS ? l : AIM_DEFAULT; }
static inline const AimTune& aim_tune(const Game& g, int i) { return AIM_TUNE[aim_level_of(g, i)]; }

static inline uint32_t rng_next(Game& g) {
  uint32_t x = g.rng;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return g.rng = x;
}
static inline float rng_f(Game& g) { return (float)(rng_next(g) >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
static inline float rng_s(Game& g) { return rng_f(g) * 2.0f - 1.0f; }                             // [-1, 1)
// Random vector (x and z in [-sx, sx) and [-sz, sz), y in [y0, y0 + sy)). The calls to the
// generator always go in separate statements: the evaluation order of a function's arguments is
// not fixed by C++ and two compilers would give different games.
static inline Vec3 rng_vec(Game& g, float sx, float y0, float sy, float sz) {
  const float x = rng_s(g) * sx;
  const float y = y0 + rng_f(g) * sy;
  const float z = rng_s(g) * sz;
  return v3(x, y, z);
}
static inline float dist2d(Vec3 a, Vec3 b) {
  const float dx = a.x - b.x, dz = a.z - b.z;
  return sqrtf(dx * dx + dz * dz);
}
static inline Vec3 dir_yaw(float yaw) { return v3(d_sin(yaw), 0.0f, d_cos(yaw)); }
static inline float yaw_of(float dx, float dz) { return d_atan2(dx, dz); }
static inline bool pad_down(Game& g, int i, int b) { return (g.in[i].btn >> b) & 1u; }
static inline bool pad_pressed(Game& g, int i, int b) { return ((g.in[i].btn & ~g.in[i].prev) >> b) & 1u; }

// --- game.cpp ---
int count_active(Game& g);
int count_alive(Game& g);
int count_in_play(Game& g);      // active and not yet out of lives
int count_monsters(const Game& g);
void show_msg(Game& g, const char* big, const char* small, uint32_t rgb, int size, int frames);
float diff_hp(Game& g);          // monster multipliers according to the campaign round
float diff_dmg(Game& g);
float diff_speed(Game& g);
void start_map(Game& g, int idx);      // loads the map and starts from zone 1
void game_request_quit(Game& g);
bool load_map_preview(Game& g, int idx);
Vec3 menu_spot(const Game& g, int i);        // where player i's character stands in the menus
void settings_changed(Game& g);  // aim, antialiasing or volumes changed (already in save_data): written when the pause closes
void stars_late_explorer(Game& g);   // explorer rescued when the map was already finished (end-of-map party)
// Planets and rounds (data in the save, maps from maps.h).
int world_giro(int w);            // round of planet w, from 1
int world_maps(int w);            // maps of planet w in the campaign
int world_stars(int w);           // stars of planet w in the current round
int giro_need(int w);             // stars needed for the next round of planet w (GIRO_STARS with 4 maps)
int world_first_map(int w);       // index of the planet's first map (-1 if it has none)
int map_in_world(int i);          // position of map i in its planet (0 = the first)
bool map_unlocked(int i);         // can map i be played?
int star_count(uint8_t s);        // how many stars in a SAVE_STAR_* mask
extern const char* const WORLD_NAMES[NWORLDS];   // "TERRA", "LUNA"...
// --- player.cpp ---
void player_join(Game& g, int i, int ch, bool mid_game);
void player_leave(Game& g, int i);
void players_gather(Game& g);    // end of map: whoever is down (or out of lives) joins the teammates for the party
void player_update(Game& g, int i, const Pad& pad);
void place_players(Game& g, int z);
Vec3 player_chest(const Player& p);
bool player_visible_from(Game& g, int i, Vec3 from);
void hurt_player(Game& g, int i, float dmg, Vec3 from, float knock);
void heal_player(Game& g, int i, float amount);
void push_player(Game& g, int i, float vx, float vz, int frames);
// --- combat.cpp ---
Shot* shot_new(Game& g, int kind, int owner, Vec3 pos, Vec3 vel);
void shots_update(Game& g);
void damage_monster(Game& g, int k, float dmg, int owner, Vec3 dir, float knock, bool charges);
void damage_boss(Game& g, float dmg, int owner, bool charges);
// elem: EL_FIRE for Zolfo's lava (VAPORE on slowed monsters), EL_NONE for the rest.
void explode(Game& g, Vec3 p, float radius, float dmg, int owner, float knock, bool big, int elem = EL_NONE);
bool bomb_drop(Game& g, int i);
void bombs_update(Game& g);
void items_reset(Game& g, int zone);   // zone < 0: the whole map
void items_update(Game& g);
void crate_damage(Game& g, int d, float dmg, Vec3 from);
bool power_take(Game& g, int player, int pw);   // PW_* power-up to the player (false: CARICA they do not need)
void burst(Game& g, Vec3 p, int n, int col, float speed);
void puff(Game& g, Vec3 p, int kind, int col, Vec3 vel, float size, int life);
void add_float(Game& g, Vec3 p, uint32_t rgb, int key, int value, char sign);
void add_float_text(Game& g, Vec3 p, uint32_t rgb, const char* txt);   // rising text (at most 15 bytes: the Italian text, translated when drawn)
void explorer_update(Game& g);   // the explorer calls for help; then, once rescued, celebrates and leaves
void explorer_rescue(Game& g, Vec3 c); // capsule opened: the explorer's star
void add_shake(Game& g, float a);
Marker* marker_new(Game& g, int kind, Vec3 pos, float r, int life);
void effects_update(Game& g);
// --- monsters.cpp ---
bool monster_spawn(Game& g, int type, bool summoned, const Vec3* where);
void monsters_update(Game& g);
void monsters_clear(Game& g);
float monster_r(const Monster& m);
float monster_h(const Monster& m);
Vec3 monster_center(const Monster& m);
bool monster_drawn(const Game& g, const Monster& m);   // hidden in the tall grass far from the players?
void boss_start(Game& g);
void boss_update(Game& g);
float boss_r(void);
float boss_speed(Game& g);                 // the gladiator's walking speed in the current stage (m/s)
Vec3 boss_javelin_start(const Boss& b); // where the javelins start from (the drawing starts from the hand)
bool boss_force_act(Game& g, int act);           // tests: starts action act towards the target right away
// --- flow.cpp ---
// Distance field on a grid of half-metre cells: the monsters follow it to reach the players
// going around the obstacles, the bots to go where they want.
void flow_build(Game& g, uint16_t* field, const Vec3* goals, int ngoals, int zone);   // zone < 0: the whole map
bool flow_dir(Game& g, const uint16_t* field, float x, float z, float* dx, float* dz);
bool walk_clear(Game& g, Vec3 a, Vec3 b, float r, bool flying);
// --- LAN play (game.cpp; see net.h) ---
const NetView& net_view(const Game& g);   // what is on the network (from the core, only for the network pages)
void net_req(Game& g, int req, int arg);  // request to the core (NR_*): it picks it up after game_update
int net_local(const Game& g);             // player of this console in a LAN game, -1 outside one
// On the network only those with a console connected count (for the "premete un tasto" notices).
static inline bool net_console(const Game& g, int i) { return !g.net || ((g.net_consoles >> i) & 1u) != 0u; }
int net_wait_frames(const Game& g);       // how many frames this console has been waiting for the network (HUD only)
uint8_t net_silent(const Game& g);        // consoles that have not answered for a while (HUD only)
// --- screens (game/screens: the menus as states, the pause, the card choice) ---
void go_screen(Game& g, int s);           // change screen (SC_*)
void screens_update(Game& g);             // a step of the current screen (not SC_PLAY)
void screens_hud(const Game& g, Hud* h);  // what the current screen draws (not SC_PLAY)
void menu_info(Game& g, const char* s);   // notice in the menus in place of the footer (2 seconds)
const char* net_error_text(Game& g, int e, char* buf, int n);   // what to say for a network error (NE_*)
void play_menus_update(Game& g);  // pause and join popups, during the game
void play_menus_hud(const Game& g, Hud* h);
// --- game_draw.cpp ---
bool to_screen(const Camera& c, Vec3 p, int* sx, int* sy);
// Face of hero ch (drawings from tools/make_font.py) in a square of side 8 * size from (x, y):
// true colours, dimmed (down) or grey (out of lives).
enum { FACE_NORMAL = 0, FACE_DOWN, FACE_OUT };   // look of the faces (ui::face)
// --- bot.cpp ---
void bot_reset(Game& g);
void bot_set_lag(Game& g, int frames);   // tests: the bots react late to the marks on the ground, like a person
// --- cards.cpp ---
bool cards_enabled(Game& g);
void cards_map_start(Game& g);           // new map (or map restarted from the start): all the cards go away
void cards_offer(Game& g);               // opens the choice (if the cards are enabled) for whoever still has lives
void cards_zone_cleared(Game& g);        // zone cleared and another one follows: the choice opens shortly
// (0.5.6) The gladiator has lost `thirds` thirds of its health (1 or 2): if one of them has not been
// rewarded yet in this map, opens the choice for it. True if the choice is now open.
bool cards_boss_third(Game& g, int thirds);
void cards_zone_start(Game& g);          // start (or new start) of a zone: the shields recharge
void card_take(Game& g, int i, int c);   // the player i takes the card c
void cards_update(Game& g);              // choice open: everyone's cursors and confirmations, then it closes
void cards_hud(const Game& g, Hud* h);               // boxes of the choice (in place of the pause)
void card_lose_all(Game& g, int i);            // whoever leaves the game loses their cards
uint8_t card_shot_fx(Game& g, int i, int kind);       // SFX_* of player i's shots of this kind
void card_shot_mods(Game& g, int i, Shot* s);         // normal shot just fired: damage, PERFORANTE, RIMBALZO, combo
void card_shot_hit(Game& g, int k, const Shot& s);    // a player's shot hit monster k (still alive)
void card_elem_hit(Game& g, int k, int owner, int elem);   // fire or snow on monster k: maybe VAPORE
void card_burn_tick(Game& g, int k);                  // burning monster: damage and little flames
bool card_shield_hit(Game& g, int i);                 // SCUDO: true if the hit is stopped
void card_special_used(Game& g, int i);               // SPECIALE GENEROSO
bool cards_layout_ok(Game& g, char* why, int n, bool all = false);   // tests: names and descriptions fit in the boxes (all: writes them all)
// Tests (0.5.6), in the current language: the texts of GRAFICA (pause item, popup) and of the LAN
// game saved also on the console that joins (question, slots, waiting room) fit in their places.
bool gfx_layout_ok(Game& g, char* why, int n);
bool join_layout_ok(char* why, int n);
