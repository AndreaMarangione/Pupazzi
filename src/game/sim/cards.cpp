// Cards (0.4): at the start of every map and after every cleared zone, if another one follows,
// each player picks 1 card out of 3, which lasts until the end of the map (it stays even if the
// zone is replayed; if the map is restarted from the start, everyone starts again without cards).
// Since 0.5.6 also when the gladiator loses a third of its health (at 2/3 and at 1/3), once per
// third in the map: replaying the zone does not give them again.
// While the choice is open the game is frozen as in the pause; everyone picks on their own panel
// with up/down and A (or a short START press); a player out of lives does not pick. This file also
// has the effects that don't fit better elsewhere: shield, extra life, generous special, burns and
// the vapor of the combos.
#include "game/sim/game_int.h"
#include "platform/plat.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const CardDef CARDS[NCARDS] = {
  { "", "", NULL, false },
  { N_("BOMBAROLO"), N_("porti fino a 4 bombe e ne hai subito una in più"), NULL, false },
  { N_("PASSO SVELTO"), N_("+15% di velocità"), NULL, false },
  { N_("PELLE DURA"), N_("+30 di vita massima, subito piena"), NULL, false },
  { N_("RICARICA LAMPO"), N_("la barra del colpo si ricarica il 40% più in fretta"), NULL, false },
  { N_("SUPER CARICA"), N_("lo speciale si carica il 35% più in fretta"), NULL, false },
  { N_("COLPI PESANTI"), N_("+25% di danni del colpo normale"), NULL, false },
  { N_("LUNGA GITTATA"), N_("+25% di gittata del colpo normale"), NULL, false },
  { N_("MERENDA"), N_("ogni mostro sconfitto ti ridà il doppio della vita"), NULL, false },
  { N_("SCUDO"), N_("a ogni zona uno scudo ferma il primo colpo che prendi"), NULL, false },
  { N_("VITA EXTRA"), N_("una vita in più per questa mappa"), NULL, false },
  { N_("CALAMITA"), N_("prendi talismani e bombe da 3 metri"), NULL, false },
  { N_("BOMBE GIGANTI"), N_("le tue bombe fanno il 50% di danni in più su un'area più larga"),
    N_("le tue bombe: +50% di danni, area più larga"), false },
  { N_("SPECIALE GENEROSO"), N_("quando usi lo speciale i compagni vicini recuperano 25 vita"),
    N_("il tuo speciale dà 25 di vita ai compagni vicini"), false },
  { N_("PERFORANTE"), N_("i colpi normali attraversano un mostro in più"), NULL, false },
  { N_("RIMBALZO"), N_("i colpi normali rimbalzano una volta su muri, massi e alberi"), NULL, false },
  { N_("COLPI INFUOCATI"), N_("con Zolfo: i tuoi colpi incendiano i mostri"), NULL, true },
  { N_("COLPI GELATI"), N_("con Marzio: i tuoi colpi rallentano i mostri"), NULL, true },
  { N_("SCHEGGE ROVENTI"), N_("con Zolfo: le tue schegge incendiano i mostri"), NULL, true },
  { N_("SCHEGGE GELATE"), N_("con Marzio: le tue schegge rallentano i mostri"), NULL, true },
  { N_("VAPORE"), N_("ghiaccio + fuoco: il mostro rallentato che brucia scoppia in vapore"),
    N_("ghiaccio + fuoco sullo stesso mostro: scoppia in vapore"), true },
};

namespace {


const float BURN_DMG = 1.0f;            // every BURN_EVERY frames: 4 per second for 3 seconds
const int BURN_FRAMES = 180, BURN_EVERY = 15;
const float ICE_SLOW = 0.7f;            // ice combo: -30% for 1.5 s (Marzio: -40%)
const int ICE_FRAMES = 90;
const float VAPOR_DMG = 30.0f, VAPOR_R = 1.6f;
const int VAPOR_CD = 90;                // the same monster bursts at most once every 1.5 s
const float GENEROUS_R = 6.0f, GENEROUS_HP = 25.0f;
const int SHIELD_INVULN = 45;           // after the shield: no second hit from the same attack

// --- offer ------------------------------------------------------------------------

// A teammate with character ch still in play (a player out of lives doesn't count: won't return).
bool partner(Game& g, int i, int ch) {
  for (int j = 0; j < MAX_PLAYERS; ++j)
    if (j != i && g.pl[j].active && !g.pl[j].out && g.pl[j].ch == ch) return true;
  return false;
}

// Who holds VAPORE (it counts for the whole team): the hitter, if they have it, else the first.
int vapor_owner(Game& g, int hitter) {
  if (hitter >= 0 && hitter < MAX_PLAYERS && g.pl[hitter].active && has_card(g.pl[hitter], CA_VAPORE)) return hitter;
  for (int j = 0; j < MAX_PLAYERS; ++j)
    if (g.pl[j].active && has_card(g.pl[j], CA_VAPORE)) return j;
  return -1;
}

bool eligible(Game& g, int i, int c) {
  const Player& p = g.pl[i];
  if (has_card(p, c)) return false;
  switch (c) {
    case CA_GENEROSO: return count_in_play(g) >= 2;
    case CA_PERFORANTE: case CA_RIMBALZO: return p.ch != CH_ZOLFO;   // flame already goes through
    case CA_COLPI_FUOCO: return p.ch == CH_ASTRO && partner(g, i, CH_ZOLFO);
    case CA_COLPI_GELO: return p.ch == CH_ASTRO && partner(g, i, CH_MARZIO);
    case CA_SCHEGGE_FUOCO: return p.ch == CH_MOCHI && partner(g, i, CH_ZOLFO);
    case CA_SCHEGGE_GELO: return p.ch == CH_MOCHI && partner(g, i, CH_MARZIO);
    case CA_VAPORE:
      if (vapor_owner(g, -1) >= 0) return false;                        // already there for all
      return (p.ch == CH_MARZIO && partner(g, i, CH_ZOLFO)) || (p.ch == CH_ZOLFO && partner(g, i, CH_MARZIO));
    default: return true;
  }
}

// Removes a random card from the list (a single call to the generator).
uint8_t draw_from(Game& g, uint8_t* list, int* n) {
  const int k = (int)(rng_next(g) % (uint32_t)*n);
  const uint8_t c = list[k];
  list[k] = list[--*n];
  return c;
}

void make_offer(Game& g, int i) {
  CardOffer& o = g.offer[i];
  memset(&o, 0, sizeof(o));
  uint8_t basic[NCARDS], combo[NCARDS];
  int nb = 0, nc = 0;
  for (int c = 1; c < NCARDS; ++c) {
    if (!eligible(g, i, c)) continue;
    if (CARDS[c].combo) combo[nc++] = (uint8_t)c;
    else basic[nb++] = (uint8_t)c;
  }
  // With at least one possible combo, half of the time one of the three is a combo.
  bool one_combo = false;
  if (nc > 0) one_combo = rng_f(g) < 0.5f;
  while (o.n < (one_combo ? 2 : 3) && nb > 0) o.card[o.n++] = draw_from(g, basic, &nb);
  bool got_combo = false;
  while (o.n < 3 && nc > 0 && (one_combo || nb == 0)) {
    o.card[o.n++] = draw_from(g, combo, &nc);
    got_combo = true;
  }
  if (got_combo && o.n > 1) {          // the combo not always last
    const int pos = (int)(rng_next(g) % (uint32_t)o.n);
    const uint8_t t = o.card[pos];
    o.card[pos] = o.card[o.n - 1];
    o.card[o.n - 1] = t;
  }
  o.on = o.n > 0;
  char buf[96];
  int len = 0;
  buf[0] = 0;
  for (int k = 0; k < o.n && len < (int)sizeof(buf); ++k)
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "%s%s", k ? ", " : "", CARDS[o.card[k]].name);
  plat_log("carte per G%d (%s): %s", i + 1, CHARS[g.pl[i].ch].name, o.n ? buf : "nessuna");
}

}  // namespace

void card_take(Game& g, int i, int c) {
  Player& p = g.pl[i];
  p.cards |= 1u << c;
  switch (c) {
    case CA_BOMBAROLO:
      if (p.bombs < pl_bombs_cap(p)) p.bombs++;
      break;
    case CA_PELLE:
      p.hp_max += 30.0f;
      if (p.alive) p.hp = p.hp_max;     // a player who is down finds it when they come back
      break;
    case CA_SUPER:
      if (p.charge > pl_special_cost(p)) p.charge = pl_special_cost(p);
      break;
    case CA_SCUDO:
      p.shield = true;
      break;
    case CA_VITA:
      p.lives++;                         // right away, for this map
      plat_log("G%d: una vita in piu' (vite %d)", i + 1, p.lives);
      break;
    default: break;
  }
  plat_log("G%d (%s) prende la carta %s", i + 1, CHARS[p.ch].name, CARDS[c].name);
}

// --- interface ------------------------------------------------------------------------

void game_set_cards_enabled(Game& g, bool on) { g.session.cards_on = on; }
bool cards_enabled(Game& g) { return g.session.cards_on; }

void cards_map_start(Game& g) {
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    Player& p = g.pl[i];
    p.cards = 0;
    p.shield = false;
  }
  memset(g.offer, 0, sizeof(g.offer));
  g.cards_open = false;
  g.cards_t = g.cards_due = 0;
  g.boss_thirds = 0;
}

bool cards_boss_third(Game& g, int thirds) {
  if (!g.session.cards_on || thirds <= g.boss_thirds) return false;
  g.boss_thirds++;
  plat_log("boss: perso un terzo della vita (%d su 3), carte", g.boss_thirds);
  cards_offer(g);
  return g.cards_open;
}

void cards_offer(Game& g) {
  g.cards_due = 0;
  if (!g.session.cards_on || g.screen != SC_PLAY) return;
  bool any = false;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    if (!g.pl[i].active || g.pl[i].out) {     // out of lives: no cards, won't return in this map
      memset(&g.offer[i], 0, sizeof(CardOffer));
      continue;
    }
    make_offer(g, i);
    any |= g.offer[i].on;
  }
  if (!any) return;
  g.cards_open = true;
  g.cards_t = g.cards_age = 0;
  g.cards_seq++;
  ev_sound_ui(g, SND_CARD_OPEN);
  plat_log("carte: si sceglie (mappa %d, zona %d)", g.map_idx + 1, (g.phase == PH_CLEAR ? g.zone + 1 : g.zone) + 1);
}

void cards_zone_cleared(Game& g) {
  if (g.session.cards_on) g.cards_due = CARDS_CLEAR_DELAY;
}

void cards_zone_start(Game& g) {
  for (int i = 0; i < MAX_PLAYERS; ++i)
    if (g.pl[i].active && has_card(g.pl[i], CA_SCUDO)) g.pl[i].shield = true;
}

void card_lose_all(Game& g, int i) {
  Player& p = g.pl[i];
  p.cards = 0;
  p.shield = false;
}

uint8_t card_shot_fx(Game& g, int i, int kind) {
  if (i < 0 || i >= MAX_PLAYERS) return 0;
  const Player& p = g.pl[i];
  uint8_t fx = 0;
  if (kind == SK_BULLET) {
    if (has_card(p, CA_COLPI_FUOCO)) fx |= SFX_BURN;
    if (has_card(p, CA_COLPI_GELO)) fx |= SFX_SLOW;
  } else if (kind == SK_SHARD) {
    if (has_card(p, CA_SCHEGGE_FUOCO)) fx |= SFX_BURN;
    if (has_card(p, CA_SCHEGGE_GELO)) fx |= SFX_SLOW;
  }
  return fx;
}

void card_shot_mods(Game& g, int i, Shot* s) {
  const Player& p = g.pl[i];
  if (!p.cards) return;
  if (has_card(p, CA_PESANTI)) s->dmg *= 1.25f;
  if (s->kind != SK_FLAME) {
    if (has_card(p, CA_PERFORANTE)) s->pierce = 1;
    if (has_card(p, CA_RIMBALZO)) s->bounce = 1;
  }
  s->fx = card_shot_fx(g, i, s->kind);
}

void card_shot_hit(Game& g, int k, const Shot& s) {
  Monster& m = g.mo[k];
  // VAPORE: first look at how the monster was, then add what this shot brings.
  if (s.kind == SK_FLAME || (s.fx & SFX_BURN)) card_elem_hit(g, k, s.owner, EL_FIRE);
  if (m.alive && (s.kind == SK_FROST || (s.fx & SFX_SLOW))) card_elem_hit(g, k, s.owner, EL_SNOW);
  if (!m.alive) return;
  if (s.fx & SFX_BURN) {
    if (m.burn_t <= 0) puff(g, monster_center(m), PK_FLAME, PC_FIRE, v3(0, 1.5f, 0), 0.8f, 16);
    m.burn_t = BURN_FRAMES;            // renewed, not added up
    m.burn_by = s.owner;
  }
  if (s.fx & SFX_SLOW) {
    if (m.slow_t <= 0 || m.slow_k >= ICE_SLOW) {    // doesn't remove a stronger slowdown
      m.slow_k = ICE_SLOW;
      if (m.slow_t < ICE_FRAMES) m.slow_t = ICE_FRAMES;
    }
    puff(g, s.pos, PK_SNOW, PC_SNOW, v3(0, 0.6f, 0), 0.8f, 14);
  }
}

void card_elem_hit(Game& g, int k, int owner, int elem) {
  Monster& m = g.mo[k];
  if (!m.alive || m.rise > 0 || m.vapor_t > 0) return;
  if (elem == EL_FIRE ? m.slow_t <= 0 : (elem != EL_SNOW || m.burn_t <= 0)) return;
  const int by = vapor_owner(g, owner);
  if (by < 0) return;
  // The slowed, burning monster bursts into vapor: damage to those around it (itself included).
  m.vapor_t = VAPOR_CD;
  const Vec3 at = m.pos, c = monster_center(m);
  int hit = 0;
  for (int j = 0; j < MAX_MONSTERS; ++j) {
    Monster& o = g.mo[j];
    if (!o.alive || o.rise > 0 || dist2d(o.pos, at) - monster_r(o) > VAPOR_R) continue;
    Vec3 dir = v3(o.pos.x - at.x, 0.0f, o.pos.z - at.z);
    dir = length(dir) > 1e-3f ? normalize(dir) : v3(0, 0, 0);
    damage_monster(g, j, VAPOR_DMG, by, dir, 2.0f, false);
    hit++;
  }
  for (int q = 0; q < 10; ++q) {         // white cloud that spreads and rises
    const float a = D_TWO_PI * (float)q / 10.0f;
    puff(g, c, PK_PUFF, PC_WHITE, v3(d_cos(a) * 3.4f, 1.2f + 0.3f * (float)(q & 1), d_sin(a) * 3.4f), 2.4f, 28);
  }
  puff(g, c + v3(0, 0.3f, 0), PK_PUFF, PC_WHITE, v3(0, 2.6f, 0), 3.4f, 34);
  puff(g, c + v3(0, 0.1f, 0), PK_PUFF, PC_SNOW, v3(0, 1.6f, 0), 3.0f, 30);
  add_shake(g, 0.06f);
  ev_sound(g, SND_EXPLODE_SMALL, at);
  plat_log("vapore! un mostro scoppia (G%d): %d colpiti", by + 1, hit);
}

void card_burn_tick(Game& g, int k) {
  Monster& m = g.mo[k];
  m.burn_t--;
  if (m.burn_t % 5 == 0) {             // small flames rising from the monster
    const float a = (float)(g.tick + (unsigned)k * 7u) * 1.7f, r = monster_r(m) * 0.7f;
    puff(g, monster_center(m) + v3(d_cos(a) * r, 0.15f, d_sin(a) * r), PK_FLAME, PC_FIRE, v3(0, 1.6f, 0), 1.2f, 16);
  }
  if (m.burn_t % BURN_EVERY != 0) return;
  const int by = m.burn_by >= 0 && m.burn_by < MAX_PLAYERS && g.pl[m.burn_by].active ? m.burn_by : -1;
  damage_monster(g, k, BURN_DMG, by, v3(0, 0, 0), 0.0f, false);     // burns don't charge the special
  if (m.alive) card_elem_hit(g, k, by, EL_FIRE);
}

bool card_shield_hit(Game& g, int i) {
  Player& p = g.pl[i];
  if (!p.shield) return false;
  p.shield = false;
  p.invuln = SHIELD_INVULN;
  p.sq_v -= 1.5f;
  const Vec3 c = player_chest(p);
  burst(g, c, 12, i, 3.5f);
  burst(g, c, 6, PC_WHITE, 2.5f);
  ev_sound(g, SND_CAPSULE_HIT, p.pos);
  plat_log("giocatore %d: lo scudo ferma il colpo (mappa %d, zona %d)", i + 1, g.map_idx + 1, g.zone + 1);
  return true;
}

void card_special_used(Game& g, int i) {
  const Player& p = g.pl[i];
  if (!has_card(p, CA_GENEROSO)) return;
  int n = 0;
  for (int j = 0; j < MAX_PLAYERS; ++j) {
    const Player& q = g.pl[j];
    if (j == i || !q.active || !q.alive || q.hp >= q.hp_max || dist2d(q.pos, p.pos) > GENEROUS_R) continue;
    heal_player(g, j, GENEROUS_HP);
    burst(g, player_chest(q), 6, PC_GREEN, 2.0f);
    n++;
  }
  if (n) plat_log("giocatore %d: speciale generoso, %d compagni curati", i + 1, n);
}
