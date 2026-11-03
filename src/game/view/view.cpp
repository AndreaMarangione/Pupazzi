// The view of a game on this console: it listens to the game's events (sounds placed in the world,
// interface sounds, camera moves) and picks the music. See view.h.
#include "game/view/view.h"
#include "game/sim/game_int.h"
#include "game/view/models.h"
#include "engine/audio.h"
#include <stdlib.h>
#include <type_traits>

namespace {

// A sound at a point of the world: within the frame (about 0.62 times the camera distance on each
// side) it is heard in full, outside it fades to zero at one and a half times; left/right from the
// position on screen.
void on_sound_at(void* ctx, int id, Vec3 pos) {
  const View& v = *(const View*)ctx;
  const float dist = fmaxf(v.cam_dist, 8.0f);
  const float dx = pos.x - v.cam_target.x, dz = pos.z - v.cam_target.z;
  const float pan = clampf(dx / (0.62f * dist * 1.3f), -0.75f, 0.75f);
  const float d = sqrtf(dx * dx + dz * dz);
  const float vol = d < dist ? 1.0f : clampf(1.0f - (d - dist) / (0.5f * dist), 0.0f, 1.0f);
  if (vol > 0.02f) audio_play(id, pan, vol);
}

// Menu and announcement sounds: in the middle.
void on_sound_ui(void* ctx, int id) { audio_play(id, 0.0f, 1.0f); }

void on_camera(void* ctx, const Game& g, int how) { view_camera(*(View*)ctx, g, how); }

}  // namespace

static_assert(std::is_trivially_default_constructible<View>::value && std::is_trivially_copyable<View>::value,
              "View is plain data: created with calloc");

static int g_views;   // views alive: the models are shared by all of them

View* view_create(void) {
  View* v = (View*)calloc(1, sizeof(View));
  v->hdmi = true;
  if (g_views++ == 0) models_build();
  return v;
}

void view_destroy(View* v) {
  if (!v) return;
  level_meshes_free(&v->lm);
  free(v);
  if (--g_views == 0) models_free();
}

GameEvents view_events(View* v) {
  GameEvents e;
  e.ctx = v;
  e.sound_at = on_sound_at;
  e.sound_ui = on_sound_ui;
  e.camera = on_camera;
  return e;
}

void view_step(View& v, const Game& g) {
  // Music: the title tune in the menus, the map's in play, the fanfare at the end of a map (then
  // silence on the statistics); lower in the pause and during the cards.
  int want = MUS_TITLE;
  static const int STYLE_MUSIC[NSTYLES] = { MUS_PIRAMIDI, MUS_CHICHENITZA, MUS_MURAGLIA, MUS_COLOSSEO };
  if (g.screen == SC_PLAY)
    want = g.phase == PH_MAP_DONE ? MUS_VICTORY : STYLE_MUSIC[g.L.style >= 0 && g.L.style < NSTYLES ? g.L.style : 0];
  else if (g.screen == SC_STATS) want = MUS_VICTORY;
  audio_music(want);
  audio_duck(g.screen == SC_PLAY && (g.paused || g.cards_open));
}
