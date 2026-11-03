// Map: grid of 1-metre cells read from a text file, zones separated by gates, collisions and
// line of sight computed on the cells, static geometry for drawing.
//
// Coordinates: column cx goes from west to east, row cz from north (row 0) to south. The world
// has X towards east and Z towards south: cell (cx, cz) has its centre at
//   x = cx - W/2 + 0.5,  z = cz - H + 0.5
// so the map lies between x = -W/2 and +W/2 and between z = -H and 0; the players start at the
// bottom (south).
#pragma once
#include "engine/mesh.h"

#define LV_MAX_W 48
#define LV_MAX_H 96
#define LV_MAX_ZONES 6
#define LV_MAX_GATES 6
#define LV_MAX_SPAWN 32
#define LV_MAX_ITEMS 64
#define LV_MAX_WAVES 4
#define LV_MAX_WAVE_KINDS 4
#define LV_MAX_DYN 480          // bushes, tall grass, breakable crates, trampolines
// Scenery drawn around the map (0.5.1): how much the play camera can show beyond the border
// walls, at 30 m with the players at the corners (see game.cpp, CAM_OVER_*): sideways up to 14 m
// (at the top the framing is wider), 13 m to the north, 10 m to the south. Beyond that there is
// only flat ground and, towards the south (menus) and the north (end of map), a few far-off
// silhouettes.
#define LV_MARGIN_X 14          // scenery cells to the west and to the east
#define LV_MARGIN_N 13          // ...to the north
#define LV_MARGIN_S 10          // ...to the south
#define LV_CHUNK 8              // the static geometry is split into blocks of 8x8 cells
#define LV_MAX_CHUNKS ((LV_MAX_W + 2 * LV_MARGIN_X + LV_CHUNK - 1) / LV_CHUNK * ((LV_MAX_H + LV_MARGIN_N + LV_MARGIN_S + LV_CHUNK - 1) / LV_CHUNK))
#define LV_MAX_SCENERY 96       // pieces of the distant scenery: ground beyond the border and 24 m tiles (pyramids, hills...)

// Cells of the grid. What they look like depends on the map's style (level_mesh.cpp):
// for example the wall is sandstone in Piramidi and grey bricks in Muraglia.
enum CellType {
  C_VOID = 0,   // ' '  outside the map
  C_FLOOR,      // '.'  ground (sand, lawn, arena)
  C_PATH,       // ','  paved path
  C_WALL,       // '#'  high wall: cannot be climbed over
  C_TREE,       // 't'  tree (palm, pine...)
  C_ROCK,       // 'o'  boulder, ruin, broken column
  C_LOW,        // '='  low wall: can be jumped over, shots pass
  C_CRATE,      // 'c'  1 m block: can be climbed onto by jumping
  C_BREAK,      // 'x'  amphora or crate that breaks when hit, with a power-up inside (0.5.4); 'X' with a talisman, 'p' capsule with the explorer
  C_BUSH,       // '*'  bush: hides the players
  C_GRASS,      // '"'  tall grass, papyrus, bamboo: hides everyone
  C_WATER,      // '~'  water: cannot be crossed, not even by jumping; shots pass (flyers pass over it)
  C_TRAMP,      // 'T'  trampoline
  C_PLATFORM,   // 'P'  raised platform ('Q' with a talisman on top)
  C_GATE,       // 'G'  gate between two zones
  C_PIT,        // 'O'  cenote: deep pit; whoever ends up over it while on the ground falls in (0.5.1)
  C_NTYPES
};
// Dynamic objects (they get squashed, break, bounce): their meshes are in the map's style, built
// by level_build_mesh and drawn by game_draw.cpp (origin on the ground at the centre of the cell,
// within the cell's metre).
enum { DM_BUSH = 0, DM_GRASS, DM_CRATE, DM_CRATE_TAL, DM_TRAMP, DM_COUNT };

// The worlds are the planets (17/10), in the order of the journey.
enum { WORLD_TERRA = 0, WORLD_LUNA, WORLD_MARTE, WORLD_VENERE, WORLD_MERCURIO, WORLD_GIOVE, WORLD_SATURNO,
       WORLD_URANO, WORLD_NETTUNO, NWORLDS };
// Map style ("stile:" line of the file): colours, walls, objects, scenery and music. The order
// stays that of 0.5 (game.cpp picks the music by index): Chichen Itza (0.5.1) took the place of
// Machu Picchu.
enum { STYLE_PIRAMIDI = 0, STYLE_CHICHENITZA, STYLE_MURAGLIA, STYLE_COLOSSEO, NSTYLES };
// Map items: talisman (heals), bomb to pick up, power-up hidden in an 'x' crate (0.5.4: which one
// is decided when the crate breaks, see PW_* in game_int.h).
enum { IT_TALISMAN = 0, IT_BOMB = 1, IT_POWER = 2 };
// Monsters the maps can call in the waves, and bosses of the arenas (Terra). Since 0.5.1 the
// runner (MO_SCORPIONE) is the scorpion and the shooter (MO_ARCIERE) the terracotta archer.
enum { MO_SCORPIONE = 0, MO_ARCIERE, MO_SCARABEO, MO_GUERRIERO, MO_NTYPES };
enum { BOSS_NONE = -1, BOSS_GLADIATORE = 0 };

const float WALL_H = 1.6f, TREE_H = 3.0f, ROCK_H = 1.3f, LOW_H = 0.55f, CRATE_H = 1.0f;
const float PLAT_H = 0.8f, GATE_H = 1.7f, TRAMP_H = 0.15f, WATER_Y = -0.14f;
const float PIT_WATER_Y = -2.6f; // water at the bottom of the cenote (whoever falls in disappears under it)
const float BLOCK_ALL = 99.0f;   // "infinite" height: not even by jumping
const float FLY_H = 1.3f;        // flyers pass over everything that is lower
// Bodies for the collisions (last argument of level_push_out, level_body_ok and level_body_guard):
// BODY_WALK whoever walks, also when jumping (water always stops them); BODY_FLY whoever flies
// (only bumps into what is higher than FLY_H, passes over water and cenote); BODY_PIT, together
// with BODY_WALK, for whoever avoids the cenote like a wall (walking monsters, unless pushed; the
// explorer). false and true still count as "walk" and "fly".
enum { BODY_WALK = 0, BODY_FLY = 1, BODY_PIT = 2 };
const float CRATE_HP = 50.0f, CAPSULE_HP = 3.0f * CRATE_HP;   // the explorer's capsule is a sturdier crate

struct WaveDef {
  int nkinds;
  uint8_t kind[LV_MAX_WAVE_KINDS];     // MO_*
  uint8_t count[LV_MAX_WAVE_KINDS];
};

struct ZoneDef {
  int x0, z0, x1, z1;                  // extreme cells (inclusive)
  float wx0, wx1, wz0, wz1;            // outer edges in world coordinates
  int nwaves;
  WaveDef wave[LV_MAX_WAVES];
  int nspawn;
  int16_t spawn[LV_MAX_SPAWN][2];      // spawn cells of the monsters
  int entry_gate, exit_gate;           // entry and exit gate (-1 none)
  Vec3 start[4];                       // where the players (re)start in the zone
};

// Swing gates (0.5.4; before, they sank into the ground): two leaves with their hinges at the two
// ends of the gate. They open by swinging 90 degrees towards the zone that opens them (from_zone:
// whoever plays in it sees them open towards themselves, and whoever is in front of the leaves is
// pushed away) and they close again behind the players when they are all in the next zone,
// slamming (on the side of from_zone, empty by then).
struct Gate {
  int x0, z0, x1, z1;                  // cells of the gate
  int from_zone;                       // it opens when this zone is clear
  bool horiz;                          // from west to east (the zones are to the north and to the south)
  float open;                          // 0 closed .. 1 open: leaves turned by gate_theta(open)
  float vel;                           // how much open changes every frame (game.cpp, update_gates)
  float mx, mz;                        // towards from_zone, perpendicular to the gate: the leaves open on that side
  float stop[2];                       // how far each leaf opens (1: fully; less if there is an obstacle)
  Mesh leaf[2];                        // closed leaves, with the hinge at the origin (level_mesh.cpp)
};
#define GATE_LEAF_HALF 0.1f            // half the thickness of the leaves (for the pushes)
struct MapItem {
  uint8_t kind, zone;
  int16_t cx, cz;
  int16_t crate;                       // talisman or power-up inside a crate: index in dyn[], otherwise -1
  float y;                             // height of the surface it stands on
};
struct DynCell { uint8_t type, zone, capsule; int16_t cx, cz; float hp; };  // hp: breakable crates; capsule 1: the explorer's capsule 'p'
static inline float dyn_hp_max(const DynCell& d) { return d.capsule ? CAPSULE_HP : CRATE_HP; }

struct Level {
  char name[40];                       // name shown in the menus
  char id[24];                         // file name (without .txt)
  int world, boss, style;
  int W, H;
  uint8_t cell[LV_MAX_H][LV_MAX_W];
  int8_t zone[LV_MAX_H][LV_MAX_W];     // -1 outside the zones
  int16_t dyn_at[LV_MAX_H][LV_MAX_W];  // index in dyn[] or -1
  int8_t gate_at[LV_MAX_H][LV_MAX_W];  // gate of the G cell or -1
  int nzones;
  ZoneDef zones[LV_MAX_ZONES];
  int ngates;
  Gate gates[LV_MAX_GATES];
  int nitems;
  MapItem items[LV_MAX_ITEMS];
  int ndyn;
  DynCell dyn[LV_MAX_DYN];
  int16_t capsule_dyn;                 // capsule with the explorer: index in dyn[], -1 if the map has none
  Vec3 boss_pos;
  int nchunks;
  Mesh chunk[LV_MAX_CHUNKS];           // ground, walls, fixed obstacles, nearby scenery (blocks of 8x8 cells)
  int nscenery;
  Mesh scenery[LV_MAX_SCENERY];        // distant scenery: ground beyond the border and large shapes
  Mesh dyn_mesh[DM_COUNT];             // bush, tall grass, crate, crate with the talisman, trampoline
};

// Reads a map from the text. false (with the reason in err) if the text is not valid.
bool level_parse(Level* L, const char* text, char* err, int errlen);
// Builds the static geometry, the scenery and the meshes of the dynamic objects in the map's
// style (to be called once after level_parse; level_free frees everything).
void level_build_mesh(Level* L);
void level_free(Level* L);
// Puts crates and gates back as at the start of the map (z < 0) or only those of zone z.
void level_reset_dynamic(Level* L, int z);
// Gate leaves (0.5.4): how far leaf side is turned (radians, from 0 closed to pi/2 open). The
// motion (it starts slowly, slams when closing) is done by update_gates on open.
static inline float gate_theta(const Gate& gt, int side) {
  float o = gt.open < gt.stop[side] ? gt.open : gt.stop[side];
  if (o < 0.0f) o = 0.0f;
  return D_HALF_PI * o;
}
// Leaf side (0: at end u0, that is west or north; 1: at the other one) of gate gi turned by
// theta: hinge (on the ground), direction from the hinge to the tip and length (from the hinge to
// the centre of the gate). closed: the direction when closed (from the hinge towards the centre
// of the gate).
void level_gate_leaf(const Level* L, int gi, int side, float theta, Vec3* hinge, Vec3* dir, float* len, Vec3* closed = 0);

static inline Vec3 cell_center(const Level* L, int cx, int cz) {
  return v3((float)cx - (float)L->W * 0.5f + 0.5f, 0.0f, (float)cz - (float)L->H + 0.5f);
}
// Cell that contains (x, z); false if outside the grid.
bool level_cell(const Level* L, float x, float z, int* cx, int* cz);
int level_type_at(const Level* L, float x, float z);   // C_VOID outside the grid
// Height of the cell's obstacles for the various uses (they take crates and gates into account):
float level_walk_top(const Level* L, int cx, int cz);  // for walkers (99 = never: water too)
float level_stand_top(const Level* L, int cx, int cz); // surface one stands on (water and fences: 0)
float level_shot_top(const Level* L, int cx, int cz);  // for straight shots and sight
float level_solid_top(const Level* L, int cx, int cz); // true height (lobbed shots)
// Height on which someone rests with their feet at height feet, climbing steps up to step.
float level_ground(const Level* L, float x, float z, float feet, float step);
// Does the cell stop the body (BODY_*) with its feet at height feet that climbs steps up to step?
// Outside the grid everything stops.
bool level_blocks(const Level* L, int cx, int cz, float feet, float step, int body);
// Pushes a circle of radius r with its feet at height feet out of the obstacles. It never pushes
// it into a cell that stops it: if the centre is inside an obstacle it exits from the nearest side
// that faces a free cell, and if there is none it stays where it is (level_body_guard handles it).
void level_push_out(const Level* L, float* x, float* z, float feet, float r, float step, int body);
// Does the circle of radius r at (x, z) touch no cell that stops the body?
bool level_body_ok(const Level* L, float x, float z, float feet, float r, float step, int body);
// Final guard of every move (0.5.1), to be called after level_push_out and the other pushes:
// if at (*x, *z) the body touches an obstacle it tries (*x, oz), then (ox, *z) (sliding along the
// walls), otherwise it goes back to (ox, oz). true if the new position was taken as it was.
bool level_body_guard(const Level* L, float ox, float oz, float* x, float* z, float feet, float r, float step, int body);
// Cenote: is the point (x, z) over a pit cell? (Whoever stands on it on the ground falls in.)
bool level_pit_at(const Level* L, float x, float z);
// Does a cell of the cenote come within r of the point (x, z)?
bool level_pit_near(const Level* L, float x, float z, float r);
// First obstacle for a straight shot from a to b: true if there is one, with the fraction of the
// path and the cell.
bool level_trace(const Level* L, Vec3 a, Vec3 b, float* t, int* hcx, int* hcz);
static inline bool level_blocked(const Level* L, Vec3 a, Vec3 b) { float t; int x, z; return level_trace(L, a, b, &t, &x, &z); }
// True if a walking (or flying) monster cannot go in a straight line from a to b (walkers also
// avoid the cenote).
bool level_walk_blocked(const Level* L, Vec3 a, Vec3 b, bool flying);
// Zone of the cell that contains (x, z), -1 if none.
int level_zone_at(const Level* L, float x, float z);
