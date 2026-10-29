// Prove della mira (0.3.1): levetta destra con zona morta circolare, filtro, rilascio e rimbalzo,
// colpi a ventaglio di Astro e Marzio, voce "Mira" nella pausa. Usa il gioco senza frontend.
//   make build/native/test_aim && ./build/native/test_aim
#include "game_int.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "  ok    " : "  ERRORE", what);
  if (!ok) g_fail++;
}

static const float DEG = 180.0f / D_PI;

static void step(float rx, float ry, uint16_t btn = 0) {
  Pad pads[MAX_PLAYERS];
  memset(pads, 0, sizeof(pads));
  pads[0].rx = rx;
  pads[0].ry = ry;
  pads[0].btn = btn;
  game_update(pads);
  g.pl[0].invuln = 1000;              // nessun mostro deve interrompere le prove
  g.pl[0].hp = g.pl[0].hp_max;
}

static void stick_deg(float a_deg, float mag, uint16_t btn = 0) {
  const float a = a_deg / DEG;
  step(d_cos(a) * mag, d_sin(a) * mag, btn);
}

// Direzione nel mondo per la levetta inclinata di a_deg gradi (come la calcola il gioco).
static float want_yaw(float a_deg) {
  const float a = a_deg / DEG;
  return yaw_of(d_cos(a), d_sin(a) / AIM_FORESHORTEN);
}

static float err_deg(float yaw, float want) { return fabsf(d_wrap_pi(yaw - want)) * DEG; }

static void fresh(int level, const char* ch) {
  game_init();
  game_set_aim_level(0, level);
  game_quick_start(0, 1, ch);
  for (int k = 0; k < 30; ++k) step(0, 0);
}

// La mira di prima (0.3.0): zona morta per asse da 0,2 e soglia 0,35, senza filtro.
static bool old_aim(float x, float y, float* yaw) {
  auto ax = [](float v) {
    if (v > -0.2f && v < 0.2f) return 0.0f;
    return (v > 0.0f ? v - 0.2f : v + 0.2f) / 0.8f;
  };
  const float rx = ax(x), ry = ax(y);
  if (sqrtf(rx * rx + ry * ry) <= 0.35f) return false;
  *yaw = yaw_of(rx, ry / AIM_FORESHORTEN);
  return true;
}

static uint32_t lcg = 12345u;
static float noise(void) {   // [-1, 1)
  lcg = lcg * 1664525u + 1013904223u;
  return (float)(lcg >> 8) * (2.0f / 16777216.0f) - 1.0f;
}

// --- carte (0.4) ---------------------------------------------------------------------

static void pads_step(const Pad* p) {
  Pad pads[MAX_PLAYERS];
  if (p) memcpy(pads, p, sizeof(pads));
  else memset(pads, 0, sizeof(pads));
  game_update(pads);
}

// 0.5.4: sul banner delle carte si aspetta quanto serve e ognuno preme A per vedere le sue carte.
static void cards_reveal(void) {
  Pad pads[MAX_PLAYERS];
  memset(pads, 0, sizeof(pads));
  while (g.cards_open && g.cards_age <= CARDS_BANNER_MIN) pads_step(pads);
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (g.offer[i].on && !g.offer[i].done) pads[i].btn = 1u << PAD_A;
  pads_step(pads);
  memset(pads, 0, sizeof(pads));
  pads_step(pads);
}

// Mostro fermo (gia' spuntato) in pos; restituisce il suo indice.
static int test_monster(Vec3 pos) {
  if (!monster_spawn(MO_SCORPIONE, false, &pos)) return -1;
  for (int k = MAX_MONSTERS - 1; k >= 0; --k)
    if (g.mo[k].alive && g.mo[k].pos.x == pos.x && g.mo[k].pos.z == pos.z) {
      g.mo[k].rise = 0;
      g.mo[k].hp = g.mo[k].hp_max = 1000.0f;
      return k;
    }
  return -1;
}

static void test_cards(void) {
  printf("10) carte\n");
  game_set_cards_enabled(true);
  for (int l = 0; l < NLANGS; ++l) {                // in ogni lingua (0.5.3)
    lang_set(l);
    char why[160], what[200];
    const bool ok = cards_layout_ok(why, sizeof(why), getenv("PUPAZZI_CARTE_TUTTE") != NULL);
    snprintf(what, sizeof(what), "%s: %s", save_lang_id(l), ok ? "nomi e descrizioni stanno nei riquadri con 1, 2, 3 e 4 giocatori" : why);
    check(ok, what);
  }
  lang_set(LANG_IT);
  {
    game_init();
    game_quick_start(0, 4, "0123");
    bool ok = g.cards_open;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
      const CardOffer& o = g.offer[i];
      ok &= o.on && o.n == 3 && !o.done && g.pl[i].cards == 0;
      ok &= o.card[0] != o.card[1] && o.card[0] != o.card[2] && o.card[1] != o.card[2];
      for (int k = 0; k < o.n; ++k) {
        const int c = o.card[k];
        ok &= c > CA_NONE && c < NCARDS;
        if (g.pl[i].ch == CH_ZOLFO) ok &= c != CA_PERFORANTE && c != CA_RIMBALZO;
        if (g.pl[i].ch == CH_ASTRO) ok &= c != CA_SCHEGGE_FUOCO && c != CA_SCHEGGE_GELO;
      }
    }
    check(ok, "inizio mappa con 4 giocatori: 3 carte diverse e adatte per ognuno, scelta aperta");
    const unsigned t0 = g.tick;
    cards_reveal();
    ok = g.cards_open && g.tick == t0;
    for (int i = 0; i < MAX_PLAYERS; ++i) ok &= g.offer[i].shown && !g.offer[i].done && g.pl[i].cards == 0;
    check(ok, "A sul banner: ognuno vede le sue carte, nessuna presa; tutto fermo");
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    game_start_tap(0);                      // G1: START breve prende la prima carta, non apre la pausa
    pads_step(pads);
    ok = !g.paused && g.offer[0].done && has_card(g.pl[0], g.offer[0].card[0]);
    pads[1].btn = 1u << PAD_DOWN;           // G2: giu' e A, prende la seconda
    pads[3].btn = 1u << PAD_R2;             // G4 tiene premuto il salto mentre sceglie
    pads_step(pads);
    pads[1].btn = 0;
    pads_step(pads);
    ok &= g.offer[1].sel == 1 && !g.offer[1].done;
    pads[1].btn = pads[2].btn = 1u << PAD_A;
    pads[3].btn = (1u << PAD_R2) | (1u << PAD_A);
    pads_step(pads);
    ok &= g.offer[1].done && has_card(g.pl[1], g.offer[1].card[1]) && g.offer[2].done && g.offer[3].done;
    ok &= g.cards_open && g.tick == t0;
    check(ok, "START breve o A prendono la carta, su/giu' spostano la scelta; tutto fermo intanto");
    int frames = 0;
    while (g.cards_open && frames < 100) {
      pads_step(pads);
      frames++;
    }
    const float y0 = g.pl[3].pos.y;
    for (int k = 0; k < 10; ++k) pads_step(pads);
    char buf[160];
    snprintf(buf, sizeof(buf), "si chiude %d fotogrammi dopo l'ultima scelta; il salto tenuto non scatta (quota %.2f)", frames + 1,
             (double)(g.pl[3].pos.y - y0));
    check(!g.cards_open && frames + 1 == CARDS_CLOSE_FRAMES && g.tick == t0 + 11u && g.pl[3].pos.y - y0 < 0.01f, buf);
  }
  {
    // Un pad che non risponde: dopo un minuto prende la carta col cursore e si riparte. G1 preme
    // A a fotogrammi alterni (vede le carte e prende la prima).
    game_init();
    game_quick_start(0, 2, "01");
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    int frames = 0;
    while (g.cards_open && frames < 2 * CARDS_TIMEOUT) {
      pads[0].btn = (frames & 1) ? 1u << PAD_A : 0;
      pads_step(pads);
      frames++;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "G2 fermo: la scelta si chiude da sola dopo %d fotogrammi", frames);
    check(!g.cards_open && frames == CARDS_TIMEOUT + CARDS_CLOSE_FRAMES - 1 && has_card(g.pl[1], g.offer[1].card[0]) &&
              has_card(g.pl[0], g.offer[0].card[0]),
          buf);
  }
  {
    // 0.5.4: il banner. Una A tenuta da prima o premuta appena compare non conta; poi A fa vedere le
    // carte (senza prenderne una), START breve pure; l'A dopo prende.
    game_init();
    game_quick_start(0, 2, "01");
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    bool ok = g.cards_open && !g.offer[0].shown && !g.offer[1].shown;
    pads[0].btn = 1u << PAD_A;                     // G1: A tenuta dall'inizio
    for (int k = 0; k < CARDS_BANNER_MIN + 10; ++k) pads_step(pads);
    ok &= !g.offer[0].shown;
    pads[0].btn = 0;
    pads_step(pads);
    game_init();
    game_quick_start(0, 2, "01");
    memset(pads, 0, sizeof(pads));
    pads_step(pads);
    pads[1].btn = 1u << PAD_A;                     // G2: A appena compare il banner
    pads_step(pads);
    pads[1].btn = 0;
    pads_step(pads);
    ok &= !g.offer[1].shown;
    while (g.cards_age <= CARDS_BANNER_MIN) pads_step(pads);
    pads[1].btn = 1u << PAD_A;
    pads_step(pads);
    ok &= g.offer[1].shown && !g.offer[1].done && !g.offer[0].shown;
    pads[1].btn = 0;
    pads_step(pads);
    game_start_tap(0);                             // G1: START breve, vede le carte
    pads_step(pads);
    ok &= g.offer[0].shown && !g.offer[0].done;
    pads[0].btn = pads[1].btn = 1u << PAD_A;
    pads_step(pads);
    ok &= g.offer[0].done && g.offer[1].done && g.cards_open;
    check(ok, "banner: A tenuta o troppo presto non conta; A (o START) fa vedere le carte, l'A dopo prende");
  }
  {
    game_init();
    game_quick_start(0, 1, "0");
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    cards_reveal();
    pads[0].btn = 1u << PAD_A;
    pads_step(pads);
    for (int k = 0; k < 30; ++k) pads_step(NULL);
    Player& p = g.pl[0];
    p.cards |= 1u << CA_SCUDO;
    p.shield = true;
    p.invuln = 0;
    const float hp = p.hp;
    hurt_player(0, 30.0f, p.pos + v3(1, 0, 0), 3.0f);
    const bool stopped = p.hp == hp && !p.shield && p.invuln > 0 && p.alive;
    p.invuln = 0;
    hurt_player(0, 30.0f, p.pos + v3(1, 0, 0), 3.0f);
    check(stopped && p.hp == hp - 30.0f, "SCUDO: il primo colpo si ferma, il secondo no");
    // VITA EXTRA (dalla 0.5, al posto di SECONDA VITA): subito una vita in piu' per questa mappa.
    const int lives = p.lives;
    cards_offer();
    CardOffer& o = g.offer[0];
    o.card[0] = CA_VITA;
    o.sel = 0;
    cards_reveal();
    pads[0].btn = 0;
    pads_step(pads);
    pads[0].btn = 1u << PAD_A;
    pads_step(pads);
    const bool took = has_card(p, CA_VITA) && p.lives == lives + 1 && lives == START_LIVES;
    pads[0].btn = 0;
    for (int k = 0; k < CARDS_CLOSE_FRAMES + 2; ++k) pads_step(pads);
    p.invuln = 0;
    hurt_player(0, 500.0f, p.pos + v3(1, 0, 0), 3.0f);
    check(took && !g.cards_open && !p.alive && p.lives == START_LIVES && !p.out,
          "VITA EXTRA: subito una vita in piu' (4); cadendo se ne perde una, senza rialzarsi");
  }
  {
    // PERFORANTE e RIMBALZO con colpi messi a mano e mostri fermi (solo i colpi avanzano).
    game_init();
    game_quick_start(0, 1, "0");
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    pads[0].btn = 1u << PAD_A;
    pads_step(pads);
    monsters_clear();
    memset(g.sh, 0, sizeof(g.sh));
    const Vec3 at = g.pl[0].pos + v3(0, 0.85f, 0);
    int hits[2] = { 0, 0 };
    for (int pierce = 0; pierce < 2; ++pierce) {
      monsters_clear();
      const int a = test_monster(g.pl[0].pos + v3(0, 0, -2.0f)), b = test_monster(g.pl[0].pos + v3(0, 0, -3.2f));
      Shot* s = shot_new(SK_BULLET, 0, at, v3(0, 0, -18.0f));
      s->left = 8.0f;
      s->dmg = 5.0f;
      s->r = 0.12f;
      s->pierce = (uint8_t)pierce;
      for (int k = 0; k < 30; ++k) shots_update();
      hits[pierce] = (a >= 0 && g.mo[a].hp < 1000.0f) + (b >= 0 && g.mo[b].hp < 1000.0f);
    }
    check(hits[0] == 1 && hits[1] == 2, "PERFORANTE: il seme passa il primo mostro e prende anche il secondo");
    // Una siepe con prato a sud: il colpo che va verso nord deve tornare indietro una volta sola.
    int wx = -1, wz = -1;
    for (int cz = 1; cz + 3 < g.L.H && wx < 0; ++cz)
      for (int cx = 0; cx < g.L.W && wx < 0; ++cx) {
        const uint8_t* c = &g.L.cell[cz][cx];
        if (c[0] == C_WALL && c[LV_MAX_W] == C_FLOOR && c[2 * LV_MAX_W] == C_FLOOR && c[3 * LV_MAX_W] == C_FLOOR) wx = cx, wz = cz;
      }
    bool ok = wx >= 0;
    if (ok) {
      monsters_clear();
      memset(g.sh, 0, sizeof(g.sh));
      const Vec3 c = cell_center(&g.L, wx, wz + 2);
      Shot* s = shot_new(SK_BULLET, 0, v3(c.x + 0.1f, 0.85f, c.z), v3(0.0f, 0.0f, -17.0f));
      s->left = 30.0f;
      s->dmg = 5.0f;
      s->r = 0.12f;
      s->bounce = 1;
      int n = 0;
      while (s->alive && s->vel.z < 0.0f && n++ < 60) shots_update();
      ok = s->alive && s->vel.z > 0.0f && s->bounce == 0 && s->pos.z > (float)wz + 1.0f - (float)g.L.H;
      // Di nuovo contro la siepe (girato a mano): questa volta si spegne.
      s->vel.z = -17.0f;
      n = 0;
      while (s->alive && n++ < 60) shots_update();
      ok &= !s->alive;
    }
    check(ok, "RIMBALZO: il colpo sulla siepe torna indietro (una volta)");
  }
  {
    // Bruciatura: 4 al secondo per 3 secondi; VAPORE: 30 intorno al mostro rallentato, poi una pausa.
    game_init();
    game_quick_start(0, 2, "23");
    Pad pads[MAX_PLAYERS];
    memset(pads, 0, sizeof(pads));
    pads[0].btn = pads[1].btn = 1u << PAD_A;
    pads_step(pads);
    g.pl[0].cards = g.pl[1].cards = 0;     // le prime carte prese non contano qui
    monsters_clear();
    const int k = test_monster(g.pl[0].pos + v3(0, 0, -4.0f)), k2 = test_monster(g.pl[0].pos + v3(0.9f, 0, -4.0f));
    Monster& m = g.mo[k];
    m.burn_t = 180;
    m.burn_by = 1;
    int n = 0;
    while (m.burn_t > 0 && n++ < 400) card_burn_tick(k);
    const float burnt = 1000.0f - m.hp;
    char buf[128];
    snprintf(buf, sizeof(buf), "bruciatura: %.0f danni in %d fotogrammi", (double)burnt, n);
    check(burnt == 12.0f && n == 180 && g.pl[1].charge == 0.0f, buf);
    m.hp = 1000.0f;
    m.slow_t = 60;
    m.slow_k = 0.6f;
    card_elem_hit(k, 1, EL_FIRE);
    const bool none = m.hp == 1000.0f;     // nessuno ha VAPORE
    g.pl[0].cards |= 1u << CA_VAPORE;
    card_elem_hit(k, 1, EL_FIRE);
    const float d1 = 1000.0f - m.hp, d2 = 1000.0f - g.mo[k2].hp;
    card_elem_hit(k, 1, EL_FIRE);
    snprintf(buf, sizeof(buf), "VAPORE: %.0f al mostro e %.0f al vicino, poi aspetta (vapore %d)", (double)d1, (double)d2,
             m.vapor_t);
    check(none && d1 == 30.0f && d2 == 30.0f && 1000.0f - m.hp == 30.0f && m.vapor_t == 90 && g.pl[0].dmg_done >= 60.0f, buf);
  }
  {
    // Partita con 4 giocatori automatici: prendono le carte da soli e la partita si ripete uguale.
    uint64_t h[2] = { 0, 0 };
    int picks[2] = { 0, 0 };
    unsigned offers[2] = { 0, 0 };
    for (int run = 0; run < 2; ++run) {
      game_init();
      game_quick_start(0, 4, "0123");
      for (int f = 0; f < 4000; ++f) {
        Pad pads[MAX_PLAYERS];
        memset(pads, 0, sizeof(pads));
        game_bot_pads(pads, 4);
        const bool was = g.cards_open;
        uint32_t before[MAX_PLAYERS];
        for (int i = 0; i < MAX_PLAYERS; ++i) before[i] = g.pl[i].cards;
        game_update(pads);
        if (was)
          for (int i = 0; i < MAX_PLAYERS; ++i) picks[run] += g.pl[i].cards != before[i];
      }
      h[run] = game_state_hash();
      offers[run] = g.cards_seq;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "giocatori automatici: %u scelte aperte e %d carte prese in 4000 fotogrammi, partita ripetuta"
             " uguale", offers[0], picks[0]);
    check(offers[0] >= 2 && picks[0] >= 4 * ((int)offers[0] - 1) && picks[0] == picks[1] && h[0] == h[1], buf);
  }
  game_set_cards_enabled(false);
}

int main() {
  plat_log_open("/dev/null");
  // Le prove della mira guidano il personaggio da subito: senza la scelta delle carte (come prima della 0.4).
  game_set_cards_enabled(false);
  static const char* const NAME[NAIMLEVELS] = { "extra morbida", "morbida", "media", "rapida" };

  printf("1) direzione fedele, niente scatti verso gli assi\n");
  for (int lv = 0; lv < NAIMLEVELS; ++lv) {
    static const float ANG[9] = { 0, 5, 10, 20, 45, 80, 135, 200, 300 };
    float worst = 0.0f;
    for (float a : ANG) {
      fresh(lv, "0");
      for (int k = 0; k < 40; ++k) stick_deg(a, 1.0f);
      worst = fmaxf(worst, err_deg(g.pl[0].aim_yaw, want_yaw(a)));
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "%s: errore massimo %.2f gradi", NAME[lv], (double)worst);
    check(worst < 0.5f, buf);
  }
  {
    float y5 = 0, y10 = 0;
    old_aim(d_cos(5 / DEG), d_sin(5 / DEG), &y5);
    old_aim(d_cos(10 / DEG), d_sin(10 / DEG), &y10);
    printf("  (prima: levetta a 5 e 10 gradi -> mira a %.1f e %.1f gradi dall'asse)\n",
           (double)(err_deg(y5, want_yaw(0))), (double)(err_deg(y10, want_yaw(0))));
  }

  printf("2) soglia circolare\n");
  for (int lv = 0; lv < NAIMLEVELS; ++lv) {
    const float on = AIM_TUNE[lv].on;
    fresh(lv, "0");
    for (int k = 0; k < 30; ++k) stick_deg(37.0f, on - 0.02f);
    const bool off = !g.pl[0].aiming;
    for (int k = 0; k < 30; ++k) stick_deg(37.0f, on + 0.02f);
    char buf[96];
    snprintf(buf, sizeof(buf), "%s: sotto %.2f non mira, sopra si'", NAME[lv], (double)on);
    check(off && g.pl[0].aiming, buf);
  }

  printf("3) tremolio di +-3 gradi a levetta tutta inclinata\n");
  float raw_rms = 0.0f;
  for (int lv = 0; lv < NAIMLEVELS; ++lv) {
    fresh(lv, "0");
    lcg = 777u;
    double s2 = 0.0, r2 = 0.0;
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < 180; ++k) {
      const float n = 3.0f * noise();
      stick_deg(30.0f + n, 1.0f);
      if (k >= 60) {
        const float e = d_wrap_pi(g.pl[0].aim_yaw - want_yaw(30.0f)) * DEG;
        s2 += (double)(e * e);
        const float r = d_wrap_pi(want_yaw(30.0f + n) - want_yaw(30.0f)) * DEG;
        r2 += (double)(r * r);
        lo = fminf(lo, e);
        hi = fmaxf(hi, e);
      }
    }
    const float rms = (float)sqrt(s2 / 120.0), rrms = (float)sqrt(r2 / 120.0);
    raw_rms = rrms;
    static const float MAXR[NAIMLEVELS] = { 0.4f, 0.55f, 0.7f, 0.95f };
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: tremolio %.2f gradi (efficace; levetta %.2f), escursione %.2f gradi", NAME[lv], (double)rms,
             (double)rrms, (double)(hi - lo));
    check(rms < MAXR[lv] && rms < 0.6f * rrms, buf);
  }
  (void)raw_rms;

  printf("4) scatto di 90 gradi: quanti fotogrammi per arrivare a meno di 5 gradi\n");
  for (int lv = 0; lv < NAIMLEVELS; ++lv) {
    fresh(lv, "0");
    for (int k = 0; k < 40; ++k) stick_deg(0.0f, 1.0f);
    int n = 0;
    float over = 0.0f;
    const float want = want_yaw(90.0f), from = want_yaw(0.0f), span = d_wrap_pi(want - from);
    for (int k = 0; k < 60; ++k) {
      stick_deg(90.0f, 1.0f);
      const float e = d_wrap_pi(g.pl[0].aim_yaw - want);
      if (span * e > 0.0f) over = fmaxf(over, fabsf(e) * DEG);   // oltre il bersaglio
      if (n == 0 && fabsf(e) * DEG < 5.0f) n = k + 1;
    }
    static const int MAXN[NAIMLEVELS] = { 10, 8, 6, 4 };
    char buf[96];
    snprintf(buf, sizeof(buf), "%s: %d fotogrammi (%.0f ms), oltre il bersaglio %.2f gradi", NAME[lv], n, (double)(n * 1000.0f / 60.0f), (double)over);
    check(n > 0 && n <= MAXN[lv] && over < 0.5f, buf);
  }

  printf("5) levetta lasciata: la mira resta dov'era\n");
  for (int lv = 0; lv < NAIMLEVELS; ++lv) {
    fresh(lv, "0");
    for (int k = 0; k < 60; ++k) stick_deg(40.0f, 1.0f);
    const float before = g.pl[0].aim_yaw;
    // Ritorno della molla: la levetta scivola di lato mentre torna al centro e lo supera.
    static const float PATH[6][2] = { { 48, 0.92f }, { 70, 0.7f }, { 120, 0.45f }, { 200, 0.2f }, { 0, 0.0f }, { 0, 0.0f } };
    float worst = 0.0f;
    for (auto& s : PATH) {
      stick_deg(s[0], s[1]);
      worst = fmaxf(worst, err_deg(g.pl[0].yaw, before));
    }
    const float after = err_deg(g.pl[0].yaw, before);
    // Rimbalzo oltre il centro, dalla parte opposta, per un solo fotogramma.
    stick_deg(220.0f, 0.65f);
    const bool bounced = g.pl[0].aiming;
    stick_deg(0.0f, 0.0f);
    const float after_bounce = err_deg(g.pl[0].yaw, before);
    char buf[160];
    snprintf(buf, sizeof(buf), "%s: durante il rilascio al massimo %.2f gradi, dopo %.2f; rimbalzo %s (%.2f gradi)", NAME[lv], (double)worst,
             (double)after, bounced ? "PRESO" : "ignorato", (double)after_bounce);
    check(worst < 3.0f && after < 0.01f && !bounced && after_bounce < 0.01f, buf);
    // Subito dopo si mira di nuovo dalla parte opposta: deve partire al secondo fotogramma.
    stick_deg(220.0f, 1.0f);
    const bool first = g.pl[0].aiming;
    stick_deg(220.0f, 1.0f);
    snprintf(buf, sizeof(buf), "%s: si rimira dopo il rilascio al secondo fotogramma", NAME[lv]);
    check(!first && g.pl[0].aiming && err_deg(g.pl[0].aim_yaw, want_yaw(220.0f)) < 0.01f, buf);
  }
  for (int lv = 0; lv < NAIMLEVELS; ++lv) {
    // Rilascio rapido: in due letture la levetta scivola di lato e arriva quasi al centro.
    fresh(lv, "0");
    for (int k = 0; k < 60; ++k) stick_deg(40.0f, 1.0f);
    const float before = g.pl[0].aim_yaw;
    stick_deg(70.0f, 0.85f);
    stick_deg(100.0f, 0.3f);
    stick_deg(0.0f, 0.0f);
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: rilascio rapido, poi guarda a %.2f gradi da dove mirava", NAME[lv], (double)err_deg(g.pl[0].yaw, before));
    check(!g.pl[0].aiming && err_deg(g.pl[0].yaw, before) < 0.01f, buf);
  }
  for (int lv = 0; lv < NAIMLEVELS; ++lv) {
    // Caso ambiguo: la prima lettura del rilascio e' ancora quasi tutta inclinata (0,97) ma gia'
    // scivolata di 30 gradi, come girare la levetta lungo il bordo e lasciarla subito dopo. Non
    // si torna indietro (si perderebbero i giri veri lungo il bordo): resta al massimo lo
    // spostamento di quel fotogramma, gia' filtrato.
    fresh(lv, "0");
    for (int k = 0; k < 60; ++k) stick_deg(40.0f, 1.0f);
    const float before = g.pl[0].aim_yaw;
    stick_deg(70.0f, 0.97f);
    stick_deg(100.0f, 0.3f);
    stick_deg(0.0f, 0.0f);
    const float e = err_deg(g.pl[0].yaw, before), slide = err_deg(want_yaw(70.0f), want_yaw(40.0f));
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: rilascio quasi tutto inclinato e scivolato di %.0f gradi: resta a %.1f gradi", NAME[lv], (double)slide, (double)e);
    check(!g.pl[0].aiming && e < 0.5f * slide, buf);
  }
  for (int lv = 0; lv < NAIMLEVELS; ++lv) {
    // Si cambia mira con la levetta un po' meno inclinata, la si tiene e poi la si lascia: deve
    // restare la mira nuova, non tornare alla vecchia.
    fresh(lv, "0");
    for (int k = 0; k < 60; ++k) stick_deg(0.0f, 1.0f);
    for (int k = 0; k < 40; ++k) stick_deg(80.0f, 0.85f);
    const float now = g.pl[0].aim_yaw;
    stick_deg(80.0f, 0.2f);
    stick_deg(0.0f, 0.0f);
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: mira cambiata e poi lasciata, resta quella nuova (%.2f gradi)", NAME[lv], (double)err_deg(g.pl[0].yaw, now));
    check(!g.pl[0].aiming && err_deg(g.pl[0].yaw, now) < 0.01f && err_deg(now, want_yaw(80.0f)) < 1.0f, buf);
  }
  {
    // Lo stesso rilascio con la mira di prima.
    float y = 0.0f, last = want_yaw(40.0f);
    static const float PATH[4][2] = { { 48, 0.92f }, { 70, 0.7f }, { 120, 0.45f }, { 200, 0.2f } };
    for (auto& s : PATH)
      if (old_aim(d_cos(s[0] / DEG) * s[1], d_sin(s[0] / DEG) * s[1], &y)) last = y;
    printf("  (prima: dopo lo stesso rilascio il personaggio guardava a %.0f gradi da dove mirava)\n", (double)err_deg(last, want_yaw(40.0f)));
  }

  printf("6) colpi a ventaglio\n");
  for (int c = 0; c < 2; ++c) {
    const int ch = c == 0 ? CH_ASTRO : CH_MARZIO;
    const int kind = c == 0 ? SK_BULLET : SK_FROST;
    fresh(AIM_MEDIUM, c == 0 ? "0" : "2");
    for (int k = 0; k < 40; ++k) stick_deg(-90.0f, 1.0f);    // levetta in alto: verso nord
    const float aim = g.pl[0].aim_yaw, w = CHARS[ch].aim_w;
    bool was[MAX_SHOTS];
    for (int s = 0; s < MAX_SHOTS; ++s) was[s] = g.sh[s].alive;
    float got[8];
    int frame_of[8], n = 0;
    for (int k = 0; k < 14; ++k) {
      stick_deg(-90.0f, 1.0f, k == 0 ? (uint16_t)(1u << PAD_R) : 0);
      for (int s = 0; s < MAX_SHOTS; ++s) {
        const Shot& sh = g.sh[s];
        if (sh.alive && !was[s] && sh.owner == 0 && sh.kind == kind && n < 8) {
          got[n] = yaw_of(sh.vel.x, sh.vel.z);
          frame_of[n++] = k;
        }
        was[s] = sh.alive;
      }
    }
    bool ok = n == 3;
    if (ok) {
      // Astro: sinistra, centro, destra uno dopo l'altro; Marzio: tutti e tre insieme.
      const float want[3] = { aim + 0.5f * w, aim, aim - 0.5f * w };
      if (c == 0) {
        for (int k = 0; k < 3; ++k) ok &= err_deg(got[k], want[k]) < 0.05f;
        ok &= frame_of[0] < frame_of[1] && frame_of[1] < frame_of[2];
      } else {
        for (int k = 0; k < 3; ++k) {
          bool found = false;
          for (int j = 0; j < 3; ++j) found |= err_deg(got[j], want[k]) < 0.05f;
          ok &= found;
        }
        ok &= frame_of[0] == frame_of[2];
      }
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: %d colpi, ventaglio di %.0f gradi", CHARS[ch].name, n, (double)(w * DEG));
    check(ok, buf);
  }

  printf("7) voce Mira nella pausa\n");
  {
    fresh(AIM_MEDIUM, "0");
    auto press = [](int b) {
      step(0, 0, (uint16_t)(1u << b));
      step(0, 0);
    };
    game_start_tap(0);
    step(0, 0);
    bool ok = g.paused && g.pause_sel == PS_RESUME;
    press(PAD_DOWN);
    ok &= g.pause_sel == PS_CONTROLS;
    press(PAD_A);
    ok &= g.paused && g.pause_help;                     // A su Comandi apre il riquadro dei tasti
    press(PAD_DOWN);
    ok &= g.pause_help && g.pause_sel == PS_CONTROLS;   // intanto le frecce non spostano la scelta
    press(PAD_B);
    ok &= g.paused && !g.pause_help;                    // B chiude il riquadro, la pausa resta
    press(PAD_DOWN);
    ok &= g.pause_sel == PS_AIM;
    press(PAD_RIGHT);
    ok &= g.aim_level[0] == AIM_QUICK;
    press(PAD_RIGHT);
    ok &= g.aim_level[0] == AIM_XSOFT;
    press(PAD_LEFT);
    ok &= g.aim_level[0] == AIM_QUICK;
    press(PAD_A);
    ok &= g.aim_level[0] == AIM_XSOFT && g.paused;     // A sulla voce Mira cambia, non chiude la pausa
    press(PAD_UP);
    ok &= g.pause_sel == PS_CONTROLS;
    press(PAD_UP);
    ok &= g.pause_sel == PS_RESUME;
    press(PAD_UP);
    ok &= g.pause_sel == PAUSE_ITEMS - 1;
    press(PAD_B);
    ok &= !g.paused && g.screen == SC_PLAY && g.pl[0].bombs == 0;
    check(ok, "pausa: A su Comandi apre i tasti e B li chiude; giu', destra/sinistra/A cambiano la mira; B riprende");
    check(!strcmp(game_aim_name(0), "extra morbida"), "il nome della mira scelta arriva al registro");
    game_init();
    check(g.aim_level[0] == AIM_SOFT && !strcmp(game_aim_name(0), "morbida"), "all'avvio la mira e' morbida");
  }

  printf("8) inclinazione a meta' che trema (+-0,04) mentre si gira a 90 gradi al secondo\n");
  for (int lv = 0; lv < NAIMLEVELS; ++lv) {
    float lag[2] = { 0, 0 };
    int back = 0;
    for (int run = 0; run < 2; ++run) {
      fresh(lv, "0");
      lcg = 4242u;
      float prev = 0.0f;
      for (int k = 0; k < 180; ++k) {
        const float a = 1.5f * (float)k, m = 0.7f + (run ? 0.04f * noise() : 0.0f);
        stick_deg(a, m);
        const float y = g.pl[0].aim_yaw;
        if (k >= 60) {
          lag[run] = fmaxf(lag[run], err_deg(y, want_yaw(a)));
          if (run && d_wrap_pi(y - prev) * DEG > 0.5f) back++;   // la mira torna indietro
        }
        prev = y;
      }
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: ritardo %.2f gradi (senza tremolio %.2f), passi indietro %d", NAME[lv], (double)lag[1], (double)lag[0], back);
    check(lag[1] < lag[0] + 0.5f && back == 0, buf);
  }
  for (int lv = 0; lv < NAIMLEVELS; ++lv) {
    fresh(lv, "0");
    for (int k = 0; k < 40; ++k) stick_deg(0.0f, 1.0f);
    for (int k = 0; k < 40; ++k) stick_deg(90.0f, (k & 1) ? 0.73f : 0.80f);
    char buf[128];
    snprintf(buf, sizeof(buf), "%s: inclinazione che salta tra 0,80 e 0,73: la mira arriva comunque (%.2f gradi)", NAME[lv],
             (double)err_deg(g.pl[0].aim_yaw, want_yaw(90.0f)));
    check(g.pl[0].aiming && err_deg(g.pl[0].aim_yaw, want_yaw(90.0f)) < 1.0f, buf);
  }

  printf("9) dopo la pausa e dopo il salto di Mochi si riparte da dove punta la levetta\n");
  {
    fresh(AIM_MEDIUM, "0");
    for (int k = 0; k < 40; ++k) stick_deg(0.0f, 1.0f);
    game_start_tap(0);
    stick_deg(0.0f, 1.0f);
    const bool paused = g.paused;
    for (int k = 0; k < 30; ++k) stick_deg(120.0f, 0.7f);
    game_start_tap(0);
    stick_deg(120.0f, 0.7f);
    float worst = 0.0f;
    for (int k = 0; k < 3; ++k) {
      stick_deg(120.0f, 0.7f);
      worst = fmaxf(worst, err_deg(g.pl[0].aim_yaw, want_yaw(120.0f)));
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "pausa: dopo, la mira e' a %.2f gradi dalla levetta", (double)worst);
    check(paused && !g.paused && g.pl[0].aiming && worst < 0.01f, buf);
  }
  {
    fresh(AIM_MEDIUM, "1");
    for (int k = 0; k < 40; ++k) stick_deg(0.0f, 1.0f);
    g.pl[0].charge = CHARS[CH_MOCHI].special_cost;
    stick_deg(0.0f, 1.0f, (uint16_t)(1u << PAD_L));
    const bool leaping = g.pl[0].leap_n > 0;
    int n = 0;
    while (g.pl[0].leap_n > 0 && n++ < 120) stick_deg(120.0f, 0.7f);
    stick_deg(120.0f, 0.7f);
    char buf[128];
    snprintf(buf, sizeof(buf), "salto di Mochi: atterrato, la mira e' a %.2f gradi dalla levetta", (double)err_deg(g.pl[0].aim_yaw, want_yaw(120.0f)));
    check(leaping && g.pl[0].aiming && err_deg(g.pl[0].aim_yaw, want_yaw(120.0f)) < 0.01f, buf);
  }

  test_cards();
  game_shutdown();
  printf(g_fail ? "\n%d prove fallite\n" : "\ntutto bene\n", g_fail);
  return g_fail ? 1 : 0;
}
