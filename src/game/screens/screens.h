// Screens of the game (State pattern): every screen is a module with what it does when it opens
// (enter), at every step (update) and what it draws (hud: const Game&, it only reads the game),
// plus the ways to reach it (enter_*, back_to_title). SCREENS (screens.cpp) is the table; go_screen
// changes screen. Shared by the screen modules only: the rest of the game sees game_int.h.
// In a LAN game (0.5.4) the screens from COME SI GIOCA? on run the same on every console (each with
// its own pad, as on one screen): nothing here depends on this console, except what stays its own
// (pause settings, the saves of the console that created the game). In the menus A confirms and B
// goes back; a short START counts as A. Drawn in the Orbita style (ui.h), in pixels at 640x480.
// SC_PLAY is the game itself: play_update (game/sim) and the play HUD (game/view).
#pragma once
#include "game/sim/game_int.h"
#include "engine/lang.h"
#include "game/sim/maps.h"
#include "platform/plat.h"
#include "game/save.h"
#include "platform/sysinfo.h"
#include "engine/ui.h"

namespace screens {

// A screen: what it does when it opens (go_screen), at every step (screens_update) and what it
// draws (screens_hud). NULL: nothing.
struct Screen {
  void (*enter)(Game& g);
  void (*update)(Game& g);
  void (*hud)(const Game& g, Hud* h);
};
extern const Screen SCREENS[NSCREENS];

inline uint32_t O(uint32_t rgb) { return HUD_OPAQUE(rgb); }
inline uint32_t A(uint32_t rgb, float a) { return hud_a(rgb, a); }

// Stars of the round of a planet: how many, how many are needed, what to say on the right.
struct Giro { int stars, max, need, giro; bool last, ready; };

// common.cpp
bool confirm(Game& g, int i);
bool back(Game& g, int i);
bool any_confirm(Game& g, int* who);
bool any_back(Game& g);
int any_nav(Game& g);
void info(Game& g, const char* s);
void back_to_title(Game& g, int item);
bool blinking(const Game& g, unsigned period);
void toast(Hud* h, const char* s, uint32_t color);
void footer(const Game& g, Hud* h, const char* a_label, const char* b_label);
void yes_no(Hud* h, float cy);
// the screens
void slots_refresh(Game& g);
int first_free_slot(Game& g);
int used_slots(const Game& g);
void enter_slots(Game& g, int mode);
void slots_update(Game& g);
void play_time(char* buf, int n, uint32_t s);
void slot_row(const Game& g, Hud* h, float x, float y, float w, float hh, int i, bool sel);
void slots_popup(const Game& g, Hud* h);
void slots_hud(const Game& g, Hud* h);
void enter_name(Game& g, int mode, int slot, int who, bool overwrite);
int kb_action(int col);
void name_type(Game& g, char c);
void name_cancel(Game& g);
void name_done(Game& g);
void name_update(Game& g);
void name_hud(const Game& g, Hud* h);
void enter_lang(Game& g);
void lang_update(Game& g);
void lang_hud(const Game& g, Hud* h);
void title_update(Game& g);
void title_hud(const Game& g, Hud* h);
void net_update(Game& g);
void lobby_update(Game& g);
int net_game_index(const Game& g);
void net_join(Game& g, int slot);
void net_ip_line(const Game& g, Hud* h, float cx, float cy);
void subtitle(Hud* h, float cy, const char* s);
void net_game_where(const NetGame& gm, char* buf, int n);
void net_hud(const Game& g, Hud* h);
void lobby_places(Hud* h, const NetView& v, float PX, float PY, float PW);
void lobby_hud(const Game& g, Hud* h);
void mode_update(Game& g);
void mode_card(Hud* h, float x, float y, bool insieme, bool sel);
void mode_hud(const Game& g, Hud* h);
void maps_update(Game& g);
Giro giro_of(int w);
void giro_text(char* buf, int n, const Giro& gr);
void maps_hud(const Game& g, Hud* h);
void chars_enter(Game& g);
void chars_update(Game& g);
void chars_hud(const Game& g, Hud* h);
void stats_update(Game& g);
void stats_hud(const Game& g, Hud* h);

}  // namespace screens
