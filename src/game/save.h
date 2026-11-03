// Saves on the console, text files with key=value lines (several files since 0.5.3):
//
//   pupazzi.sav             settings of this console: aim of each slot, antialiasing, volumes, language,
//                           graphics (0.5.6)
//   partita1.sav .. partita4.sav    the games (the four saves of the first screen)
//
//   save_open(directory)    once at startup (the core calls it in retro_load_game): reads the
//                           settings; no game is active until the game creates or loads one
//                           (save_slot_new, save_slot_load)
//   save_data()             the data in memory (settings and active game): the game reads and
//                           changes it directly
//   save_write()            when there is something to keep (end of map, settings changed); the
//                           core also calls it on exit (retro_deinit) for what is not written yet.
//                           Writes the settings and the active game, each only if changed
//
// Each file is written atomically: first NAME.tmp (fflush + fsync), then it is renamed over NAME,
// which is therefore always the whole old one or the whole new one (it works on the FAT of the SD
// card too). A write costs an fsync, on the card even tens of milliseconds: do it for an event, not
// every frame. If it fails it writes that once to the log and returns false: the game carries on
// with the data in memory and retries on the next call.
//
// pupazzi.sav (lines starting with # are ignored, the order does not matter):
//   versione=2             format of whoever wrote it last (1: up to 0.5.2, everything in one file)
//   mira.1=1 .. mira.4=1   aim of each player slot: 0 extra soft, 1 soft, 2 medium, 3 fast
//   antialias=1            0 off, 1 the best the GPU can handle, 4 or 8 samples (since 0.5.4; before,
//                          0 or 1: earlier versions read any number other than 0 as on)
//   volume_musica=6        from 0 to 10 (up to 0.4 default 7)
//   volume_effetti=8       from 0 to 10
//   lingua=it              it, en, es, pt, zh, ja, hi (since 0.5.3)
//   (since 0.5.6, GRAFICA in the pause)
//   telecamera_distanza=0  play camera, in steps: from -2 (closer) to 2 (farther), 0 as before
//   telecamera_inclinazione=0  from -2 (more from the side) to 2 (more from above), 0 as before
//   risoluzione=640        width of the picture: 320, 640, 960 or 1280 (height 3/4: 240 ... 960); the
//                          other numbers go to the nearest one
//   dettagli=1             1 high, 0 low (no shadows and no distant landscape, half the particles)
//   margine_tv=0           from 0 to 4: the picture shrinks by 2.5% of its size on every side per step,
//                          with black around it (TVs that crop the edges)
//   salvataggi=1           (since 0.5.3) nothing to take from the 0.5.2 file (already turned into
//                          partita1.sav, or there was none); 0 if that failed: retried at startup
// partitaN.sav:
//   versione=2
//   nome=ANDREA            (since 0.5.4) name of the game: up to 12 of A-Z 0-9 space - ' ! ? .
//                          (lowercase becomes uppercase); without it, "PARTITA N" is shown
//   id=0123456789abcdef    (since 0.5.6) identity of the game, 16 hexadecimal digits: given the first
//                          time the game is played on the network; a console that joins and saves it
//                          too writes the same one (then, next time, it updates the same slot); a copy
//                          (DUPLICA) gets a new one
//   mappe.terra=1          open maps of the planet, in campaign order: finishing a map opens the
//                          next one (they stay open in later rounds too); Earth has at least 1, another
//                          planet opens (1) by beating the boss of the previous one. From 0 to 99.
//   giro.terra=1           round of the planet, from 1 to 99: beating the boss with at least 10 stars
//                          of the current round moves on to the next round (stronger monsters) and the
//                          stars start again from zero
//   stelle.terra1=5        stars of the map in the current round of its planet (file name without
//                          .txt): sum of 1 (finished), 2 (explorer rescued), 4 (without restarting
//                          the map from the start); 0 after a new round
//   tempo=3600             seconds of play (since 0.5.3)
// Planets are written in lowercase, in the order of WORLD_* in level.h: terra, luna, marte, venere,
// mercurio, giove, saturno, urano, nettuno; the file holds Earth and the planets already reached.
//
// From 0.5.2 to 0.5.3: if pupazzi.sav still has the progress (mappe.*, giro.*, stelle.*,
// storia_pianeti) and there is no game, it becomes partita1.sav; it stays in pupazzi.sav (a 0.5.2
// put back on the console finds it again) and the line salvataggi=1 says it was already taken.
// If there is already a game, the old progress is no longer taken.
// From 0.4: mappe_sbloccate, storia_vista and the prateria stars stay where they are, like the
// unknown keys; since 0.5.4 storia_pianeti too (the story no longer exists).
//
// A file that cannot be read entirely (damaged, card error, too big) is kept as NAME.illeggibile on
// the first write; if it cannot be set aside it is not replaced.
// In every file: out-of-range values are brought back into range, invalid lines are ignored. Keys
// that this version does not know are written back unchanged at the end of the file: different
// versions of the game do not erase each other's data (rule: a key never changes meaning, something
// new uses a new key). In a game file the settings do not count (they are ignored).
#pragma once
#include <stdint.h>

#define SAVE_VERSION 2
#define SAVE_PLAYERS 4
#define SAVE_MAX_MAPS 64
#define SAVE_ID_LEN 24              // like Level::id: at most 23 characters among a-z A-Z 0-9 _ -
#define SAVE_WORLDS 9               // planets, like NWORLDS in level.h
#define SAVE_GIRO_MAX 99
#define SAVE_SLOTS 4                // games
#define SAVE_LANGS 7                // languages (lang.h)
#define SAVE_NAME_LEN 13            // name of a game: at most 12 characters (save_name_char)
#define SAVE_CAM_STEPS 2            // camera distance and pitch: from -2 to 2
#define SAVE_MARGIN_MAX 4           // TV margin: from 0 to 4

// Stars of a map: finished, explorer rescued, without restarting the map from the start.
enum { SAVE_STAR_DONE = 1, SAVE_STAR_EXPLORER = 2, SAVE_STAR_NO_WIPE = 4 };
enum { SAVE_NUOVO = 0, SAVE_LETTO, SAVE_ILLEGGIBILE };   // outcome of a read

struct SaveMap {
  char id[SAVE_ID_LEN];
  uint8_t stars;                    // SAVE_STAR_*
};

struct SaveData {
  int version;                      // format of the file read last (0: no file)
  // Settings (pupazzi.sav).
  uint8_t aim[SAVE_PLAYERS];        // from 0 to 3 (default 1, soft)
  uint8_t antialias;                // 0, 1 (the best, default), 4 or 8 samples
  uint8_t music_vol, sfx_vol;       // from 0 to 10 (defaults 6 and 8)
  uint8_t lang;                     // LANG_* (lang.h), default Italian
  // Graphics (0.5.6).
  int cam_dist, cam_pitch;          // play camera: steps from -2 to 2 (default 0)
  int res;                          // width of the picture: 320, 640 (default), 960 or 1280
  uint8_t detail;                   // 1 high (default), 0 low
  uint8_t margin;                   // TV margin: from 0 (default) to SAVE_MARGIN_MAX
  // Game (partitaN.sav).
  char name[SAVE_NAME_LEN];         // nome=: "" until it is given one (PARTITA N is shown)
  uint64_t id;                      // id=: 0 until it is played on the network (0.5.6)
  uint8_t maps_open[SAVE_WORLDS];   // mappe.<planet>: open maps in order (Earth from 1, the others from 0)
  uint8_t giro[SAVE_WORLDS];        // giro.<planet>: from 1 to SAVE_GIRO_MAX
  int nmaps;                        // maps with stars (those not listed have 0 stars)
  SaveMap maps[SAVE_MAX_MAPS];
  uint32_t seconds;                 // play time
};

// Remembers the directory and reads pupazzi.sav if present: missing -> default values (SAVE_NUOVO);
// unreadable -> default values (SAVE_ILLEGGIBILE) and on the first write the old file is kept as
// pupazzi.sav.illeggibile. dir NULL or empty: only default values, no file. No active game (its
// data are the default ones). It can be called again: it starts from scratch.
int save_open(const char* dir);
// How reading the settings went, for the log: "nuovo", "letto" or "illeggibile: ...".
const char* save_status(void);
// Path of pupazzi.sav; "" before save_open (and then save_write does nothing).
const char* save_path(void);
// Always valid: before save_open it holds the default values.
SaveData* save_data(void);
// Stars of the map in the active game (0 if it is not there or if the name is not valid).
uint8_t save_stars(const char* map_id);
// Stars of the map in data read separately (save_slot_peek).
uint8_t save_stars_in(const SaveData* d, const char* map_id);
// Adds the stars (OR with those already there): only in memory, then save_write is needed.
void save_add_stars(const char* map_id, uint8_t bits);
// Sets the stars of the map to bits (new round: 0), only in memory like save_add_stars.
void save_set_stars(const char* map_id, uint8_t bits);
// Name of planet w in the file ("terra", "luna"...; "" out of range).
const char* save_world_id(int w);
// Writes the settings and the active game (brings the values in memory back into range). true if
// the files are up to date.
bool save_write(void);

// --- games (from 0 to SAVE_SLOTS - 1) ---
// Active game, -1 none.
int save_slot(void);
// Path of the file of game i ("" before save_open).
const char* save_slot_path(int i);
// Reads game i into out (only the game data; the settings of out stay at the default ones)
// without touching the active one. SAVE_NUOVO: the file does not exist (free slot).
int save_slot_peek(int i, SaveData* out);
// Game i becomes the active one, read from its file. false if the file does not exist. If it is
// unreadable it starts from the default values (and the old file, on the first write, is kept as
// partitaN.sav.illeggibile): true.
bool save_slot_load(int i);
// New game (default values, with the name name if given) in slot i, active and written right away
// (even if it cannot be written it stays active in memory). false if i is out of range or if the
// write fails.
bool save_slot_new(int i, const char* name = 0);
// Changes the name of game i (active or not) and writes it; the rest of the file stays as it is.
// false if the slot is free, the file cannot be read or written, or the name is not valid.
bool save_slot_rename(int i, const char* name);
// Character c can be part of a game name (A-Z, 0-9, space, - ' ! ? .).
bool save_name_char(char c);
// Copies game from into slot to (first the active one is written, if it is from). false if it fails.
bool save_slot_copy(int from, int to);
// Removes the file of game i; if it was the active one, there is no active game anymore.
bool save_slot_delete(int i);
// LAN game (0.5.4), on the consoles that join a game created by another one: the active game (if
// any) is written and detached, and the progress in memory starts again from the default one; the
// caller puts the host's progress there. Play goes on with that, but it is not written anywhere
// (the settings are, as always). save_guest_end: again no active game and default progress (a game
// is chosen from the title).
void save_guest_begin(void);
void save_guest_end(void);
// (0.5.6) As save_guest_begin, but the game is saved on this console too, in slot i, with identity
// id (the host's): slot i becomes the active game (the caller puts the host's progress in memory,
// then save_write). If slot i already holds the same game (the same id), at every write the planets
// where it had got further (played here alone) stay as they were: a later round wins whole (maps,
// round, stars of that planet's maps, which world_of tells), in the same round the open maps are
// the most and the stars add up; its play time goes on. Otherwise slot i is overwritten.
// save_guest_end writes it one last time.
void save_guest_slot_begin(int i, uint64_t id, int (*world_of)(const char* map_id));
// (0.5.6) Identity of the active game (it gets one, and is written, if it has none); 0 without an
// active game. The slot holding the game with identity id, -1 if none (the first one, if more).
uint64_t save_game_id(void);
int save_slot_with_id(uint64_t id);
// How the last read of a game went, for the log.
const char* save_slot_status(void);
// Code of language l in the file ("it", "en"...; "" out of range).
const char* save_lang_id(int l);
