// The screens as states (State pattern): the table with what each screen does when it opens, at
// every step and on screen; the change of screen; the dispatch for game_update and the view.
#include "game/screens/screens.h"
#include <stdio.h>
#include <string.h>

namespace screens {

// In the order of SC_* (game_int.h). SC_PLAY is the game itself: play_update (game/sim) and the
// play HUD (game/view) take its place.
const Screen SCREENS[NSCREENS] = {
  /* SC_TITLE */ { NULL, title_update, title_hud },
  /* SC_MODE  */ { NULL, mode_update, mode_hud },
  /* SC_MAPS  */ { NULL, maps_update, maps_hud },
  /* SC_CHARS */ { chars_enter, chars_update, chars_hud },
  /* SC_PLAY  */ { NULL, NULL, NULL },
  /* SC_STATS */ { NULL, stats_update, stats_hud },
  /* SC_SLOTS */ { NULL, slots_update, slots_hud },
  /* SC_LANG  */ { NULL, lang_update, lang_hud },
  /* SC_NAME  */ { NULL, name_update, name_hud },
  /* SC_NET   */ { NULL, net_update, net_hud },
  /* SC_LOBBY */ { NULL, lobby_update, lobby_hud },
};

}  // namespace screens

using namespace screens;

void go_screen(Game& g, int s) {
  g.screen = s;
  g.screen_t = 0;
  g.menu_sel = 0;
  g.paused = false;
  // Behind the menus there is always a map (on the map and hero choice: the one with the cursor).
  if (s != SC_PLAY && s != SC_STATS) {
    if (!g.level_ok) load_map_preview(g, s == SC_MAPS || s == SC_CHARS ? g.map_sel : 0);
  }
  if (SCREENS[s].enter) SCREENS[s].enter(g);
  ev_camera(g, CAM_CUT);
}

void menu_info(Game& g, const char* s) { info(g, s); }

void screens_update(Game& g) {
  if (g.session.info_t > 0) g.session.info_t--;
  if (SCREENS[g.screen].update) SCREENS[g.screen].update(g);
}

void screens_hud(const Game& g, Hud* h) {
  if (SCREENS[g.screen].hud) SCREENS[g.screen].hud(g, h);
  // The console battery at the top right (0.5.4), above the titles.
  const SysBattery bat = sys_battery(plat_now_ms());
  if (bat.present) ui::battery(h, 640.0f - ui::SAFE_X, 17.0f, bat.percent, bat.charging, g.ui_tick);
}
