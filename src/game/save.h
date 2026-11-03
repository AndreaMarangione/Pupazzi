// Saves on the console, text files with key=value lines (several files since 0.5.3):
//
//   pupazzi.sav             settings of this console: aim of each slot, antialiasing, volumes, language
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
//   salvataggi=1           (since 0.5.3) nothing to take from the 0.5.2 file (already turned into
//                          partita1.sav, or there was none); 0 if that failed: retried at startup
// partitaN.sav:
//   versione=2
//   nome=ANDREA            (since 0.5.4) name of the game: up to 12 of A-Z 0-9 space - ' ! ? .
//                          (lowercase becomes uppercase); without it, "PARTITA N" is shown
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
  // Game (partitaN.sav).
  char name[SAVE_NAME_LEN];         // nome=: "" until it is given one (PARTITA N is shown)
  uint8_t maps_open[SAVE_WORLDS];   // mappe.<planet>: open maps in order (Earth from 1, the others from 0)
  uint8_t giro[SAVE_WORLDS];        // giro.<planet>: from 1 to SAVE_GIRO_MAX
  int nmaps;                        // maps with stars (those not listed have 0 stars)
  SaveMap maps[SAVE_MAX_MAPS];
  uint32_t seconds;                 // play time
};

// Ricorda la cartella e legge pupazzi.sav se c'e': manca -> valori predefiniti (SAVE_NUOVO);
// illeggibile -> valori predefiniti (SAVE_ILLEGGIBILE) e alla prima scrittura il file vecchio resta
// come pupazzi.sav.illeggibile. dir NULL o vuota: solo valori predefiniti, niente file. Nessuna
// partita attiva (i suoi dati sono quelli predefiniti). Si puo' richiamare: riparte da zero.
int save_open(const char* dir);
// Com'e' andata la lettura delle impostazioni, per il registro: "nuovo", "letto" o "illeggibile: ...".
const char* save_status(void);
// Percorso di pupazzi.sav; "" prima di save_open (e allora save_write non fa nulla).
const char* save_path(void);
// Sempre valido: prima di save_open contiene i valori predefiniti.
SaveData* save_data(void);
// Stelle della mappa nella partita attiva (0 se non c'e' o se il nome non e' valido).
uint8_t save_stars(const char* map_id);
// Stelle della mappa in dati letti a parte (save_slot_peek).
uint8_t save_stars_in(const SaveData* d, const char* map_id);
// Aggiunge le stelle (OR con quelle che ci sono gia'): solo in memoria, poi serve save_write.
void save_add_stars(const char* map_id, uint8_t bits);
// Mette le stelle della mappa a bits (giro nuovo: 0), solo in memoria come save_add_stars.
void save_set_stars(const char* map_id, uint8_t bits);
// Nome del pianeta w nel file ("terra", "luna"...; "" fuori dai limiti).
const char* save_world_id(int w);
// Scrive le impostazioni e la partita attiva (riporta nei limiti i valori in memoria). true se i
// file sono aggiornati.
bool save_write(void);

// --- partite (da 0 a SAVE_SLOTS - 1) ---
// Partita attiva, -1 nessuna.
int save_slot(void);
// Percorso del file della partita i ("" prima di save_open).
const char* save_slot_path(int i);
// Legge la partita i in out (solo i dati della partita; le impostazioni di out restano quelle
// predefinite) senza toccare quella attiva. SAVE_NUOVO: il file non c'e' (posto libero).
int save_slot_peek(int i, SaveData* out);
// La partita i diventa quella attiva, letta dal suo file. false se il file non c'e'. Se e'
// illeggibile si parte dai valori predefiniti (e il file vecchio, alla prima scrittura, resta come
// partitaN.sav.illeggibile): true.
bool save_slot_load(int i);
// Partita nuova (valori predefiniti, con il nome name se c'e') al posto i, attiva e subito scritta
// (anche se non riesce a scriverla resta attiva in memoria). false se i e' fuori dai limiti o se la
// scrittura non riesce.
bool save_slot_new(int i, const char* name = 0);
// Cambia il nome della partita i (attiva o no) e la scrive; il resto del file resta com'e'. false se
// il posto e' libero, il file non si legge o non si scrive, o il nome non e' valido.
bool save_slot_rename(int i, const char* name);
// Il carattere c puo' stare nel nome di una partita (A-Z, 0-9, spazio, - ' ! ? .).
bool save_name_char(char c);
// Copia la partita from al posto to (prima si scrive quella attiva, se e' from). false se non riesce.
bool save_slot_copy(int from, int to);
// Toglie il file della partita i; se era quella attiva non c'e' piu' una partita attiva.
bool save_slot_delete(int i);
// Gioco in rete (0.5.4), sulle console che si uniscono a una partita creata da un'altra: la
// partita attiva (se c'e') si scrive e si stacca, e i progressi in memoria ripartono da quelli
// predefiniti; chi chiama ci mette quelli di chi ha creato la partita. Si gioca con quelli, ma non
// si scrivono da nessuna parte (le impostazioni si', come sempre). save_guest_end: di nuovo
// nessuna partita attiva e progressi predefiniti (si sceglie dal titolo).
void save_guest_begin(void);
void save_guest_end(void);
// Com'e' andata l'ultima lettura di una partita, per il registro.
const char* save_slot_status(void);
// Codice della lingua l nel file ("it", "en"...; "" fuori dai limiti).
const char* save_lang_id(int l);
