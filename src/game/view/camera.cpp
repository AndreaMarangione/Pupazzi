// Camera of the view (Model-View): it follows the game, which announces with GameEvents when to
// follow and when to cut (new map, zone restarted, another screen). It reads the game, never changes it.
#include "game/view/view.h"
#include "game/sim/game_int.h"
#include "platform/plat.h"

namespace {

// In partita: da CAM_DIST_MIN (gruppo vicino) a CAM_DIST_MAX (gruppo largo); senza HDMI, sullo schermo
// piccolo della console, si parte piu' vicino (0.5.3). Prima: 0.5.1-0.5.2 da 22 a 30 per tutti; 0.5 da 15
// a 30; 0.3.2-0.4 da 15 a 21; fino alla 0.3.1 da 11 a 17.
const float CAM_DIST_MIN = 22.0f, CAM_DIST_MIN_LCD = 15.0f, CAM_DIST_MAX = 30.0f;
// Terreno inquadrato dalla telecamera di gioco a distanza d (inclinazione 0,98, campo 0,87 in
// game_draw.cpp, schermo 4:3), in metri per metro di distanza: verso nord (in alto) fino a 0,81 d
// dal punto inquadrato, verso sud 0,425 d; di lato, s metri a sud del punto inquadrato (s
// negativo: a nord), fino a 0,62 (d - 0,557 s) dal centro: 0,62 d all'altezza del punto
// inquadrato, 0,47 d in basso e 0,90 d in alto (l'inquadratura a terra e' un trapezio).
const float VIEW_N = 0.81f, VIEW_S = 0.425f, VIEW_W = 0.62f, VIEW_K = 0.557f;
// Chi gioca si deve vedere con le sue barre: di lato entro CAM_SAFE_X della mezza larghezza della
// sua riga (le barre sono larghe una ventina di punti su 160) meno CAM_MARGIN_X metri, i piedi
// CAM_MARGIN_S sopra il bordo in basso e in alto non oltre VIEW_NU d - CAM_MARGIN_N (testa e barre
// sotto la riga in alto).
const float CAM_SAFE_X = 0.875f, CAM_MARGIN_X = 0.4f, CAM_MARGIN_S = 1.0f, CAM_MARGIN_N = 2.5f, VIEW_NU = 0.6f;
// Ai bordi della zona la telecamera si ferma mostrando al massimo cosi' tanto oltre (in metri: di
// lato all'altezza del punto inquadrato, a nord, a sud), purche' si veda ancora chi gioca; quando la
// zona sta tutta nell'inquadratura a sud si mostra CAM_OVER_S e quello che avanza va a nord (in
// alto il terreno e' piu' schiacciato e c'e' la zona dopo), di lato si centra la zona.
const float CAM_OVER_X = 1.8f, CAM_OVER_N = 6.0f, CAM_OVER_S = 3.5f;

// Distanza minima con cui i punti p[] si vedono tutti, inquadrando (cx, cz).
float cam_need(const Vec3* p, int n, float cx, float cz) {
  float d = 0.0f;
  for (int k = 0; k < n; ++k) {
    const float a = fabsf(p[k].x - cx), s = p[k].z - cz;     // di lato; verso sud (negativo: nord)
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
    // Menu: inquadratura bassa sui personaggi alla partenza della mappa mostrata, da nord
    // (dietro di loro la siepe di confine e gli alberi).
    Vec3 c = v3(0, 0, 0);
    for (int i = 0; i < 4; ++i) c = c + g.L.zones[0].start[i] * 0.25f;
    want = v3(c.x, 0.85f, c.z + 0.2f);
    if (g.screen == SC_TITLE) want.x -= 1.8f;       // 0.5.3: gli eroi a sinistra, le voci a destra
    dist = g.screen == SC_CHARS ? 5.6f : (g.screen == SC_TITLE ? 7.4f : 6.6f);
    pitch = g.screen == SC_CHARS ? 0.36f : PITCH_MENU;
    back = true;
    if (g.screen == SC_STATS) {
      // Fine mappa: i personaggi dove sono, che festeggiano rivolti verso la telecamera.
      float n = 0.0f;
      Vec3 s = v3(0, 0, 0);
      for (int i = 0; i < MAX_PLAYERS; ++i)
        if (g.pl[i].active) { s = s + g.pl[i].pos; n += 1.0f; }
      if (n > 0.0f) want = v3(s.x / n, 0.85f + s.y / n, s.z / n - 0.2f);
      dist = 7.0f;
      back = false;
    }
  } else {
    // Si inquadra chi e' in piedi; se sono tutti a terra chi ha ancora vite, se no tutti. In rete
    // (0.5.4) ogni console inquadra il suo eroe (anche a terra), finche' ha vite; poi gli altri.
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
      // Arena: il boss sempre tutto nell'inquadratura insieme ai giocatori.
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
    // Il punto inquadrato sta un po' a nord del gruppo (si vede di piu' davanti). Distanza: da
    // 15 m (si vedono circa 19 x 19 m) fino a 30 m quando il gruppo si allarga; se a 30 m non si
    // vedono ancora tutti, il punto inquadrato scende verso sud quanto basta (in alto
    // l'inquadratura e' piu' larga: una zona di 31 x 21 m con un giocatore per angolo ci sta).
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
    // Ai bordi della zona ci si ferma mostrando poco oltre, a ogni distanza, ma chi gioca resta
    // nell'inquadratura: prima nord-sud (se il punto fermato al bordo lascerebbe fuori qualcuno,
    // si torna verso quello voluto quanto basta), poi di lato con la riga in cui si trova ognuno.
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
