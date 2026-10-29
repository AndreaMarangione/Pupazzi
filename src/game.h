// Gioco: cooperativa da 1 a 4 giocatori, mappe fatte di zone da ripulire dai mostri.
#pragma once
#include "render.h"

#define PUPAZZI_VERSION "0.5.4"   // unico posto: core, menu e Makefile la leggono da qui
#define MAX_PLAYERS 4

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

void game_init(void);
void game_shutdown(void);
void game_reset(void);                              // di nuovo dal titolo
// Prove: subito in partita sulla mappa map (0 = la prima), senza menu; chars sceglie i
// personaggi (per esempio "0213"), NULL o vuoto: uno diverso per giocatore.
void game_quick_start(int map, int players, const char* chars);
void game_update(const Pad pads[MAX_PLAYERS]);      // un passo di 1/60 di secondo
void game_start_tap(int player);                    // START premuto e lasciato subito
bool game_paused(void);
bool game_wants_quit(void);                         // scelto "Esci" dal titolo
// Telecamera, oggetti da disegnare e HUD del gioco (hud puo' essere NULL).
void game_build_frame(FrameDesc* fd, int w, int h, Hud* hud);
bool game_player_active(int i);
uint32_t game_player_rgb(int i);
// Quanto e' pronta la mira del giocatore (0 extra morbida, 1 morbida, 2 media, 3 rapida; si sceglie nella pausa).
void game_set_aim_level(int player, int level);
const char* game_aim_name(int player);
// Antialias: il renderer dice se lo sa fare, il gioco se e' scelto (voce della pausa).
void game_set_aa_available(bool avail);
void game_set_aa_on(bool on);                       // come la voce della pausa (all'avvio e' acceso)
bool game_aa_wanted(void);
// Schermo: con l'HDMI (TV) la visuale va da 22 a 30 m, sullo schermo della console da 15 a 30. Il
// core lo legge dal sistema (all'avvio e' come con l'HDMI). Cambia solo l'inquadratura, non la partita.
void game_set_hdmi(bool on);
// Chiamata dal core subito dopo save_open (retro_load_game): il gioco prende le impostazioni dal salvataggio (save.h).
void game_save_loaded(void);
// Carte (dalla 0.4): all'avvio attive; spente il gioco va esattamente come prima (prove).
void game_set_cards_enabled(bool on);
void game_info(GameInfo* gi);
uint64_t game_state_hash(void);                     // per confrontare partite tra processori diversi
void game_trace_monsters(void (*out)(const char* line));
// Solo per le prove automatiche: i primi n giocatori li guida il computer.
void game_bot_pads(Pad pads[MAX_PLAYERS], int n);
