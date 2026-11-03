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
// Ground framed by the play camera at distance d (pitch 0.98, field of view 0.87 in
// game_draw.cpp, 4:3 screen), in meters per meter of distance: towards north (up) as far as
// 0.81 d from the framed point, towards south 0.425 d; sideways, s meters south of the framed
// point (s negative: north), as far as 0.62 (d - 0.557 s) from the center: 0.62 d level with the
// framed point, 0.47 d at the bottom and 0.90 d at the top (the framing on the ground is a
// trapezoid).
const float VIEW_N = 0.81f, VIEW_S = 0.425f, VIEW_W = 0.62f, VIEW_K = 0.557f;
// Whoever plays must be visible with their bars: sideways within CAM_SAFE_X of the half width of
// their row (the bars are about twenty points wide out of 160) minus CAM_MARGIN_X meters, the feet
// CAM_MARGIN_S above the bottom edge and at the top no further than VIEW_NU d - CAM_MARGIN_N (head
// and bars below the top line).
const float CAM_SAFE_X = 0.875f, CAM_MARGIN_X = 0.4f, CAM_MARGIN_S = 1.0f, CAM_MARGIN_N = 2.5f, VIEW_NU = 0.6f;
// At the edges of the zone the camera stops, showing at most this much beyond (in meters: sideways
// level with the framed point, to the north, to the south), as long as the players are still
// visible; when the whole zone fits in the framing, CAM_OVER_S is shown to the south and what is
// left over goes to the north (at the top the ground is more squashed and the next zone is there),
// sideways the zone is centered.
const float CAM_OVER_X = 1.8f, CAM_OVER_N = 6.0f, CAM_OVER_S = 3.5f;

// Minimum distance at which the points p[] are all visible, framing (cx, cz).
float cam_need(const Vec3* p, int n, float cx, float cz) {
  float d = 0.0f;
  for (int k = 0; k < n; ++k) {
    const float a = fabsf(p[k].x - cx), s = p[k].z - cz;     // sideways; southward (negative: north)
    d = fmaxf(d, (a + CAM_MARGIN_X) / (CAM_SAFE_X * VIEW_W) + VIEW_K * s);
    d = fmaxf(d, (s + CAM_MARGIN_S) / VIEW_S);
    d = fmaxf(d, (CAM_MARGIN_N - s) / VIEW_NU);
  }
  return d;
}

}  // namespace

void view_camera(View& v, const Game& g, int how) {
  if (how == CAM_CUT_PLAY) v.cam_pitch = PITCH_PLAY;   // a map begins: the play pitch even if nobody is framed
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
    dist = cam_need(pts, np, cx, cz0);
    for (int k = 1; k <= 24 && dist > CAM_DIST_MAX; ++k) {
      const float d = cam_need(pts, np, cx, cz0 + 0.5f * (float)k);
      if (d < dist) {
        dist = d;
        cz = cz0 + 0.5f * (float)k;
      }
    }
    dist = clampf(dist, v.hdmi ? CAM_DIST_MIN : CAM_DIST_MIN_LCD, CAM_DIST_MAX);
    // At the edges of the zone it stops, showing a little beyond, at every distance, but the players
    // stay in the framing: first north-south (if the point stopped at the edge would leave someone
    // out, it goes back towards the wanted one as much as needed), then sideways with the row each
    // one is in.
    const float loz = zz0 - CAM_OVER_N + VIEW_N * dist, hiz = zz1 + CAM_OVER_S - VIEW_S * dist;
    const float czc = loz <= hiz ? clampf(cz, loz, hiz) : hiz;
    want.z = czc;
    for (int k = 1; k <= 10 && cam_need(pts, np, cx, want.z) > dist + 0.01f; ++k) want.z = czc + (cz - czc) * 0.1f * (float)k;
    const float lox = zx0 - CAM_OVER_X + VIEW_W * dist, hix = zx1 + CAM_OVER_X - VIEW_W * dist;
    const float tx = lox <= hix ? clampf(cx, lox, hix) : 0.5f * (zx0 + zx1);
    float vlo = -1e30f, vhi = 1e30f;
    for (int k = 0; k < np; ++k) {
      const float hw = CAM_SAFE_X * VIEW_W * (dist - VIEW_K * (pts[k].z - want.z)) - CAM_MARGIN_X;
      vlo = fmaxf(vlo, pts[k].x - hw);
      vhi = fminf(vhi, pts[k].x + hw);
    }
    want.x = vlo <= vhi ? clampf(tx, vlo, vhi) : 0.5f * (vlo + vhi);
    want.y = 0.6f + y / (float)n * 0.5f;
    pitch = PITCH_PLAY;
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

void view_set_hdmi(View& v, bool on) {
  if (on == v.hdmi) return;
  v.hdmi = on;
  plat_log("schermo: %s, visuale da %.0f a %.0f m", on ? "HDMI (TV)" : "della console", (double)(on ? CAM_DIST_MIN : CAM_DIST_MIN_LCD),
           (double)CAM_DIST_MAX);
}
