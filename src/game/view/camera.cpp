// Camera of the view (Model-View): it follows the game, which announces with GameEvents when to
// follow and when to cut (new map, zone restarted, another screen). It reads the game, never changes it.
#include "game/view/view.h"
#include "game/sim/game_int.h"
#include "platform/plat.h"

namespace {

// During a game: from CAM_DIST_MIN (group close together) to CAM_DIST_MAX (group spread out); without
// HDMI, on the console's small screen, it starts closer (0.5.3). Before: 0.5.1-0.5.2 from 22 to 30 for
// everyone; 0.5 from 15 to 30; 0.3.2-0.4 from 15 to 21; up to 0.3.1 from 11 to 17.
const float CAM_DIST_MIN = 22.0f, CAM_DIST_MIN_LCD = 15.0f, CAM_DIST_MAX = 30.0f;
// 0.5.6, GRAFICA in the pause: every step of the distance multiplies the minimum by DIST_STEP (the
// maximum stays: beyond it the landscape around the map is not built); every step of the pitch adds
// PITCH_STEP radians (more: from above). From -2 to 2, 0 as before.
const float DIST_STEP = 1.15f, PITCH_STEP = 0.08f;

// Ground framed by the play camera at distance d with pitch p (field of view CAM_FOVY, 4:3 screen),
// in meters per meter of distance: towards north (up) as far as n d from the framed point, towards
// south s d; sideways, t meters south of the framed point (t negative: north), as far as w (d - k t)
// from the center (the framing on the ground is a trapezoid). nu: the feet of whoever plays at most
// nu d to the north, so that head and bars stay below the top line. At the pitch of 0.98 (fixed
// before 0.5.6) n 0.81, s 0.425, w 0.62, k 0.557, nu 0.6: 0.62 d sideways level with the framed
// point, 0.47 d at the bottom and 0.90 d at the top.
struct Framing { float n, s, w, k, nu; };
Framing framing(float pitch) {
  if (pitch == PITCH_PLAY) return { 0.81f, 0.425f, 0.62f, 0.557f, 0.6f };   // the values used up to 0.5.5
  const float half = CAM_FOVY * 0.5f, sn = sinf(pitch), cs = cosf(pitch), th = tanf(half);
  Framing f;
  f.n = sn / tanf(pitch - half) - cs;
  f.s = cs - sn / tanf(pitch + half);
  f.w = th * 4.0f / 3.0f;
  f.k = cs;
  f.nu = sn / tanf(pitch - atanf(0.805f * th)) - cs;   // the line 0.805 of the way up from the center (0.6 at 0.98)
  return f;
}

// Whoever plays must be visible with their bars: sideways within CAM_SAFE_X of the half width of
// their row (the bars are about twenty points wide out of 160) minus CAM_MARGIN_X meters, the feet
// CAM_MARGIN_S above the bottom edge and at the top no further than nu d - CAM_MARGIN_N (head
// and bars below the top line).
const float CAM_SAFE_X = 0.875f, CAM_MARGIN_X = 0.4f, CAM_MARGIN_S = 1.0f, CAM_MARGIN_N = 2.5f;
// At the edges of the zone the camera stops, showing at most this much beyond (in meters: sideways
// level with the framed point, to the north, to the south), as long as the players are still
// visible; when the whole zone fits in the framing, CAM_OVER_S is shown to the south and what is
// left over goes to the north (at the top the ground is more squashed and the next zone is there),
// sideways the zone is centered.
const float CAM_OVER_X = 1.8f, CAM_OVER_N = 6.0f, CAM_OVER_S = 3.5f;

// Minimum distance at which the points p[] are all visible, framing (cx, cz).
float cam_need(const Framing& f, const Vec3* p, int n, float cx, float cz) {
  float d = 0.0f;
  for (int k = 0; k < n; ++k) {
    const float a = fabsf(p[k].x - cx), s = p[k].z - cz;     // sideways; southward (negative: north)
    d = fmaxf(d, (a + CAM_MARGIN_X) / (CAM_SAFE_X * f.w) + f.k * s);
    d = fmaxf(d, (s + CAM_MARGIN_S) / f.s);
    d = fmaxf(d, (CAM_MARGIN_N - s) / f.nu);
  }
  return d;
}

float play_pitch(const View& v) { return PITCH_PLAY + PITCH_STEP * (float)v.gfx_pitch; }

}  // namespace

void view_camera(View& v, const Game& g, int how) {
  if (how == CAM_CUT_PLAY) v.cam_pitch = play_pitch(v);   // a map begins: the play pitch even if nobody is framed
  const bool snap = how != CAM_FOLLOW;
  Vec3 want;
  float dist, pitch;
  bool back = false;
  if (g.screen != SC_PLAY) {
    // Menu: low framing on the characters at the start of the map shown, from the north
    // (behind them the border hedge and the trees).
    Vec3 c = v3(0, 0, 0);
    for (int i = 0; i < 4; ++i) c = c + g.L.zones[0].start[i] * 0.25f;
    want = v3(c.x, 0.85f, c.z + 0.2f);
    if (g.screen == SC_TITLE) want.x -= 1.8f;       // 0.5.3: the heroes on the left, the menu on the right
    dist = g.screen == SC_CHARS ? 5.6f : (g.screen == SC_TITLE ? 7.4f : 6.6f);
    pitch = g.screen == SC_CHARS ? 0.36f : PITCH_MENU;
    back = true;
    v.cull_x = 0.95f;      // near_view: as at the play pitch of 0.98
    v.cull_s = 0.55f;
    v.cull_n = 1.0f;
    if (g.screen == SC_STATS) {
      // End of the map: the characters where they are, celebrating while facing the camera.
      float n = 0.0f;
      Vec3 s = v3(0, 0, 0);
      for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g.pl[i].active) { s = s + g.pl[i].pos; n += 1.0f; }
      if (n > 0.0f) want = v3(s.x / n, 0.85f + s.y / n, s.z / n - 0.2f);
      dist = 7.0f;
      back = false;
    }
  } else {
    // Frame whoever is standing; if they are all down, whoever still has lives, otherwise everyone.
    // In a LAN game (0.5.4) every console frames its own hero (even when down), as long as it has
    // lives; then the others.
    Vec3 pts[MAX_PLAYERS + 4];
    int np = 0;
    float y = 0.0f;
    const int me = net_local(g);
    const bool mine = me >= 0 && g.pl[me].active && !g.pl[me].out;
    for (int pass = 0; pass < 3 && np == 0; ++pass)
      for (int i = 0; i < MAX_PLAYERS; ++i) {
        const Player& p = g.pl[i];
        if (mine && i != me) continue;
        if (!p.active || (pass == 0 && !p.alive) || (pass == 1 && p.out)) continue;
        pts[np++] = p.pos;
        y += p.pos.y;
      }
    if (np == 0) return;
    const int n = np;
    float mnx = 1e30f, mxx = -1e30f, mnz = 1e30f, mxz = -1e30f;
    for (int k = 0; k < np; ++k) {
      mnx = fminf(mnx, pts[k].x); mxx = fmaxf(mxx, pts[k].x);
      mnz = fminf(mnz, pts[k].z); mxz = fmaxf(mxz, pts[k].z);
    }
    const float gx = 0.5f * (mnx + mxx), gz = 0.5f * (mnz + mxz);
    if (g.boss.alive && dist2d(g.boss.pos, v3(gx, 0, gz)) < 16.0f) {
      // Arena: the boss always fully in the framing together with the players.
      const Vec3 b = g.boss.pos;
      static const float BOX[4][2] = { { -1.6f, -0.5f }, { 1.6f, -0.5f }, { -1.6f, 1.5f }, { 1.6f, 1.5f } };
      for (int k = 0; k < 4; ++k) {
        pts[np++] = v3(b.x + BOX[k][0], 0.0f, b.z + BOX[k][1]);
        mnx = fminf(mnx, b.x + BOX[k][0]); mxx = fmaxf(mxx, b.x + BOX[k][0]);
        mnz = fminf(mnz, b.z + BOX[k][1]); mxz = fmaxf(mxz, b.z + BOX[k][1]);
      }
    }
    // The framing at the pitch chosen in GRAFICA (0.5.6): from above (more than 0.98) the same ground
    // is visible from farther away, so the maximum goes up as much (up to 35 m); from the side it
    // stays 30. The minimum, for this screen, with the steps of the distance.
    pitch = play_pitch(v);
    const Framing f = framing(pitch), f0 = framing(PITCH_PLAY);
    const float dmax = CAM_DIST_MAX * fmaxf(1.0f, (f0.n + f0.s) / (f.n + f.s));
    float dmin = (v.hdmi ? CAM_DIST_MIN : CAM_DIST_MIN_LCD);
    for (int k = 0; k < v.gfx_dist; ++k) dmin *= DIST_STEP;
    for (int k = 0; k > v.gfx_dist; --k) dmin /= DIST_STEP;
    dmin = fminf(dmin, dmax);
    // near_view (game_draw.cpp): a rectangle around the framed point that contains the ground seen.
    v.cull_x = f.w * (1.0f + f.k * f.n) + 0.05f;
    v.cull_s = f.s + 0.125f;
    v.cull_n = f.n + 0.19f;
    if (pitch == PITCH_PLAY) {
      v.cull_x = 0.95f;
      v.cull_s = 0.55f;
      v.cull_n = 1.0f;
    }
    const ZoneDef& z = g.L.zones[g.zone];
    float zx0 = z.wx0, zx1 = z.wx1, zz0 = z.wz0, zz1 = z.wz1;
    auto join = [&](int k) {
      if (k < 0 || k >= g.L.nzones) return;
      const ZoneDef& o = g.L.zones[k];
      zx0 = fminf(zx0, o.wx0); zx1 = fmaxf(zx1, o.wx1);
      zz0 = fminf(zz0, o.wz0); zz1 = fmaxf(zz1, o.wz1);
    };
    if (g.phase == PH_CLEAR) join(g.zone + 1);
    if (g.phase == PH_ENTER) join(g.zone - 1);
    // The framed point is a little north of the group (more is visible in front). Distance: from
    // 15 m (about 19 x 19 m are visible) up to 30 m when the group spreads out; if at 30 m they are
    // still not all visible, the framed point moves south as much as needed (at the top the
    // framing is wider: a zone of 31 x 21 m with a player in each corner fits).
    const float cx = 0.5f * (mnx + mxx), cz0 = 0.5f * (mnz + mxz) - 1.2f;
    float cz = cz0;
    dist = cam_need(f, pts, np, cx, cz0);
    for (int k = 1; k <= 24 && dist > dmax; ++k) {
      const float d = cam_need(f, pts, np, cx, cz0 + 0.5f * (float)k);
      if (d < dist) {
        dist = d;
        cz = cz0 + 0.5f * (float)k;
      }
    }
    dist = clampf(dist, dmin, dmax);
    // At the edges of the zone it stops, showing a little beyond, at every distance, but the players
    // stay in the framing: first north-south (if the point stopped at the edge would leave someone
    // out, it goes back towards the wanted one as much as needed), then sideways with the row each
    // one is in.
    const float loz = zz0 - CAM_OVER_N + f.n * dist, hiz = zz1 + CAM_OVER_S - f.s * dist;
    const float czc = loz <= hiz ? clampf(cz, loz, hiz) : hiz;
    want.z = czc;
    for (int k = 1; k <= 10 && cam_need(f, pts, np, cx, want.z) > dist + 0.01f; ++k) want.z = czc + (cz - czc) * 0.1f * (float)k;
    const float lox = zx0 - CAM_OVER_X + f.w * dist, hix = zx1 + CAM_OVER_X - f.w * dist;
    const float tx = lox <= hix ? clampf(cx, lox, hix) : 0.5f * (zx0 + zx1);
    float vlo = -1e30f, vhi = 1e30f;
    for (int k = 0; k < np; ++k) {
      const float hw = CAM_SAFE_X * f.w * (dist - f.k * (pts[k].z - want.z)) - CAM_MARGIN_X;
      vlo = fmaxf(vlo, pts[k].x - hw);
      vhi = fminf(vhi, pts[k].x + hw);
    }
    want.x = vlo <= vhi ? clampf(tx, vlo, vhi) : 0.5f * (vlo + vhi);
    want.y = 0.6f + y / (float)n * 0.5f;
  }
  if (snap || back != v.cam_back) {
    v.cam_target = want;
    v.cam_dist = dist;
    v.cam_pitch = pitch;
    v.cam_back = back;
  } else {
    v.cam_target = v.cam_target + (want - v.cam_target) * 0.12f;
    v.cam_dist += (dist - v.cam_dist) * 0.05f;
    v.cam_pitch += (pitch - v.cam_pitch) * 0.08f;
  }
}

void view_set_gfx(View& v, int dist, int pitch, bool low) {
  v.gfx_dist = dist < -2 ? -2 : (dist > 2 ? 2 : dist);
  v.gfx_pitch = pitch < -2 ? -2 : (pitch > 2 ? 2 : pitch);
  v.low_detail = low;
}

void view_set_hdmi(View& v, bool on) {
  if (on == v.hdmi) return;
  v.hdmi = on;
  plat_log("schermo: %s, visuale da %.0f a %.0f m", on ? "HDMI (TV)" : "della console", (double)(on ? CAM_DIST_MIN : CAM_DIST_MIN_LCD),
           (double)CAM_DIST_MAX);
}
