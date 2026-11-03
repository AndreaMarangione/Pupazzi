// Presentation of a game on this console (Model-View): camera, where sounds come from, music,
// models and map geometry, drawing. The view reads the game (const Game&) and never changes it; the
// game tells it what happens through GameEvents (game.h), which view_events returns for
// game_set_events. In a LAN game every console has its own view of the same game.
#pragma once
#include "game/game.h"
#include "game/view/level_mesh.h"

// Camera pitch in play (before the steps of GRAFICA, 0.5.6) and in the menus (radians from the
// horizon); vertical field of view.
const float PITCH_PLAY = 0.98f, PITCH_MENU = 0.5f;
const float CAM_FOVY = 0.87f;

struct View {
  // Camera: framed point, distance, pitch; cam_back: looking south (menus: the heroes face the
  // player).
  Vec3 cam_target;
  float cam_dist, cam_pitch;
  bool cam_back;
  // Screen: TV (HDMI) or the console's own screen; only the framing changes (0.5.3).
  bool hdmi;
  // Graphics settings of this console (0.5.6, GRAFICA in the pause): steps of the play camera's
  // distance and pitch (from -2 to 2, 0 as before), low details.
  int gfx_dist, gfx_pitch;
  bool low_detail;
  // Ground seen per meter of distance around the framed point (view_camera), for near_view.
  float cull_x, cull_s, cull_n;
  // Geometry of the map on show and the load it comes from (GameSession::level_serial).
  LevelMeshes lm;
  uint32_t lm_serial;
};

// The first view also builds the models (shared, built once), the last one frees them.
View* view_create(void);
void view_destroy(View* v);
GameEvents view_events(View* v);
// TV or console screen (the core reads it from the system; at start-up it is as with the TV).
void view_set_hdmi(View& v, bool on);
// Graphics settings of this console (0.5.6): steps of the play camera's distance and pitch (from
// -2 to 2) and low details (no shadows, no distant landscape, half the particles).
void view_set_gfx(View& v, int dist, int pitch, bool low);
// The game aims the right stick "as on the screen" for the pitch of 0.98 (AIM_FORESHORTEN): by how
// much the vertical of the stick is to be multiplied with the pitch chosen in GRAFICA (1 at 0.98).
float view_aim_tilt(const View& v);
// Camera move announced by the game (CAM_*); also used by the tests to frame a scene.
void view_camera(View& v, const Game& g, int how);
// After every game_update: the music that goes with what is on screen.
void view_step(View& v, const Game& g);
// Camera, objects to draw and the game HUD (hud may be NULL); first, the geometry of a map just
// loaded.
void view_build_frame(View& v, const Game& g, FrameDesc* fd, int w, int h, Hud* hud);
