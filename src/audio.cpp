// Sonoro di Pupazzi: effetti sintetizzati e musica, tutto generato dal codice (nessun campione).
//
// - Sintesi: un suono e' fatto di strati (fino a 10). Uno strato e' un oscillatore (seno, triangolo,
//   onda quadra o a dente di sega senza aliasing grazie a PolyBLEP, rumore bianco o morbido, FM,
//   crepitio, granelli) seguito da un filtro a variabili di stato (passa basso, banda o alto) e da un
//   inviluppo attacco-tenuta-calo; altezza e taglio del filtro scivolano nel tempo. Lo stesso motore
//   suona gli strumenti della musica: per loro la nota resta finche' il sequencer tiene la porta.
// - Voci: 32 per gli effetti, al massimo 24 udibili insieme. Ogni suono ha un limite di copie, una
//   priorita' e una finestra in cui le richieste ravvicinate si fondono in una voce sola; se non c'e'
//   posto si ruba la voce meno importante, piu' bassa e piu' vecchia, sfumandola in 10 ms. La fiamma
//   e' una voce sola che resta accesa finche' arrivano richieste.
// - Musica: un piccolo tracker. I brani sono testi (accordi, melodia, modelli di basso, accordi,
//   tappeto, arpeggi e batteria scelti battuta per battuta) letti da audio_init; due lettori per la
//   dissolvenza tra un brano e l'altro, ognuno con la sua eco a tempo. Dalla 0.5 ogni brano ha i
//   suoi strumenti a percussione (con i rulli), due strumenti che si danno il cambio sulla melodia,
//   abbellimenti (note legate, scivolate, acciaccature, trilli, accenti), una seconda voce a intervallo
//   scelto battuta per battuta, accordi arpeggiati come su una chitarra, note che si lasciano suonare
//   (due voci che si danno il cambio) e un bordone fermo sulla tonica.
// - Uscita: musica + effetti, filtro contro la continua (20 Hz), limitatore con un blocco di anticipo
//   e soglia a -1 dBFS: non si arriva mai al fondo scala.
// Si lavora a blocchi di 64 campioni: inviluppi, altezze e filtri si calcolano a ogni blocco e si
// interpolano dentro il blocco; le fasi degli oscillatori sono in forma chiusa (niente catene di
// dipendenze campione per campione). Niente memoria allocata, niente interi a 64 bit, solo float.
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BLK 64            // campioni per blocco
#define MAXL 10           // strati per suono
#define NSFXV 32          // voci degli effetti: al massimo VLIMIT udibili, le altre finiscono di sfumare
#define VLIMIT 24
#define NPLAYER 2         // lettori dei brani
#define NPV 16            // voci di un lettore
#define DLY_N 32768       // eco della musica (potenza di 2): 0,68 s a 48 kHz
#define MAXBARS 64
#define MAXSTEPS (MAXBARS * 16)
#define MAXCH 256
#define ND 7              // strumenti della batteria
#define LIM_THR 0.891f    // soglia del limitatore (-1 dBFS)
#define RNG_SEED 0x9E3779B9u

enum { W_SINE, W_TRI, W_PULSE, W_SAW, W_NOISE, W_SNOISE, W_FM, W_CRACK, W_GRAIN };
enum { F_NONE, F_LP, F_BP, F_HP };
enum { P_LOG, P_OUT, P_IN, P_FAST };   // curve: lineare in ottave, veloce poi lenta, lenta poi veloce, molto veloce
// LF_REL: altezza in multipli della nota (strumenti); LF_CENV: il taglio segue l'inviluppo ("wah");
// LF_VIBENV: il vibrato cala con l'inviluppo; LF_ONESHOT: lo strato ignora la porta e finisce da solo.
enum { LF_REL = 1, LF_CENV = 2, LF_VIBENV = 4, LF_ONESHOT = 8 };
// SF_LOOP: voce che resta accesa finche' arrivano richieste; SF_STEP: ripetizioni ravvicinate salgono di nota.
enum { SF_LOOP = 1, SF_STEP = 2 };
enum { ST_RUN, ST_REL, ST_DONE };
enum { PR_LOW, PR_MID, PR_HIGH, PR_TOP };
enum { I_LEAD, I_LEAD_SOFT, I_LEAD_BOSS, I_HARM, I_BASS, I_BASS_BOSS, I_STAB, I_PAD, I_ARP,
       I_KICK, I_SNARE, I_HAT, I_OHAT, I_CRASH, I_TOM, I_CLAP,
       // 0.5, la Terra. Egitto: liuto, flauto di canna, cetra, bordone, tamburo a calice (colpo
       // grave, colpo sul bordo, colpo leggero), tamburello a sonagli, cimbalini, tamburo a cornice
       I_OUD, I_OUD_BASS, I_NEY, I_QANUN, I_DRONE, I_DOUM, I_TEK, I_KA, I_RIQ, I_ZILLS, I_BENDIR,
       // colpo sul bordo di legno (titolo e Muraglia; nella 0.5 era del tamburo grande delle Ande)
       I_BOMBO_RIM,
       // 0.5.1, Chichen Itza (al posto delle Ande): ocarina di terracotta, marimba (melodia, arpeggi e accordi), marimba
       // bassa, tamburo profondo, guscio di tartaruga, sonaglio, conchiglia, bastone della pioggia, tamburo a fessura (il
       // tunkul: due lingue di legno), battimani con l'eco della scalinata del Castillo
       I_OCARINA, I_MARIMBA, I_MARIMBA_BASS, I_HUEHUETL, I_TURTLE, I_SONAJA, I_CONCH, I_RAINSTICK, I_TUNKUL, I_CLAP_ECO,
       // Cina: flauto, violino a due corde, cetra, blocchi di legno, gong piccolo e grande, piatti, tamburo
       I_DIZI, I_ERHU, I_GUZHENG, I_BLOCK, I_GONG, I_GONG_BIG, I_BO, I_TANGGU,
       // Roma: corno, corni gravi, tuba, archi staccati, coro, tamburo da guerra, rullante, timpani, incudine, piatto
       I_BUCCINA, I_HORN, I_TUBA, I_STRINGS, I_CHOIR, I_WARDRUM, I_SNARE_MIL, I_TIMPANI, I_ANVIL, I_CYMBAL,
       // titolo e vittoria: campana, tappeto morbido, arpeggio di cristallo, basso morbido
       I_BELL, I_WARM, I_GLASS, I_BASS_SOFT,
       NINST };
enum { DK_KICK, DK_SNARE, DK_HAT, DK_OHAT, DK_CRASH, DK_TOM, DK_CLAP };
enum { T_LEAD, T_HARM, T_BASS, T_CHORD, T_PAD, T_ARP, NTRACK };
// voci di un lettore: V_DRUM.. 7 voci della batteria, poi il secondo strumento della melodia e le voci di
// ricambio di accordi e arpeggio (TrackCfg.ring: la nota vecchia sfuma mentre parte la nuova)
enum { V_LEAD = 0, V_HARM, V_BASS, V_ARP, V_CHORD, V_PAD, V_DRUM, V_LEAD2 = V_DRUM + ND, V_CHORD2, V_ARP2 };
static_assert(V_ARP2 < NPV, "troppe voci per un lettore");
// modi: maggiore, misolidio, minore, hijaz (frigio con la terza maggiore), dorico, minore armonica, frigio
enum { M_MAJ, M_MIX, M_MIN, M_HIJAZ, M_DOR, M_HMIN, M_PHR, NMODES };
// abbellimenti della melodia, davanti alla nota nel testo: _ legata (scivola dalla nota prima), / scivolata
// dal basso, ^ acciaccatura (la nota di sopra per un attimo), ~ trillo, ! accento, ' piano
enum { O_SLIDE = 1, O_SCOOP = 2, O_GRACE = 4, O_TRILL = 8, O_ACC = 16, O_SOFT = 32 };
// seconda voce, battuta per battuta: spenta, armonizzata (accordo o terza della scala), o un intervallo fisso
enum { HV_OFF = 127, HV_HARM = 100 };
#define DR_ROLL 16        // batteria: colpo doppio (il secondo mezzo sedicesimo dopo, piu' piano)
#define STRUM_RAMP 24     // campioni di rampa all'inizio di ogni nota arpeggiata

// --- matematica veloce -------------------------------------------------------------------------

static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline int pcm(int x) { return ((x % 12) + 12) % 12; }

// sin(2 pi p) per p in [0, 1]: parabola corretta, errore massimo ~0,1% (armoniche sotto -60 dB).
static inline float fsin01(float p) {
  const float x = p + p - 1.0f;
  const float y = 4.0f * x * (1.0f - fabsf(x));
  return -y * (0.775f + 0.225f * fabsf(y));
}

// 2^x: parte intera nell'esponente, frazione in [-0,5, 0,5] con Taylor (errore relativo ~3e-6).
static inline float fexp2(float x) {
  x = clampf(x, -60.0f, 60.0f);
  const int i = (int)(x + (x >= 0.0f ? 0.5f : -0.5f));
  const float f = x - (float)i;
  const float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
  union { uint32_t u; float v; } b;
  b.u = (uint32_t)(i + 127) << 23;
  return p * b.v;
}

static inline float shape(int k, float u) {
  float w = 1.0f - u;
  switch (k) {
  case P_OUT: return 1.0f - w * w;
  case P_IN: return u * u;
  case P_FAST: w *= w; return 1.0f - w * w;
  default: return u;
  }
}

static inline uint32_t xs32(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static inline float u2f(uint32_t x) { return (float)(int32_t)x * (1.0f / 2147483648.0f); }    // [-1, 1)
static inline float rnd01(uint32_t& s) { return (float)(xs32(s) >> 8) * (1.0f / 16777216.0f); }

// Correzione PolyBLEP del gradino: t fase in [0, 1), dt incremento per campione.
static inline float blep(float t, float dt, float idt) {
  if (t < dt) { const float x = t * idt; return x + x - x * x - 1.0f; }
  if (t > 1.0f - dt) { const float x = (t - 1.0f) * idt; return x * x + x + x + 1.0f; }
  return 0.0f;
}

static inline float midi_hz(int m) { return 440.0f * fexp2((float)(m - 69) * (1.0f / 12.0f)); }
static inline float vcurve(int v) { const float x = (float)v * 0.1f; return x * x; }   // volume 0..10 -> guadagno

// --- definizioni dei suoni -----------------------------------------------------------------------

struct LDef {
  uint8_t wave, filt, pc, cc, dc, fl;   // forma, filtro, curve di altezza e taglio, curva del calo (1-4), opzioni LF_*
  float t0, a, h, d, s, r;              // partenza; attacco, tenuta, calo (s); livello di tenuta (0 = finisce da solo); rilascio
  float f0, f1, pt;                     // altezza da f0 a f1 (Hz, o multipli della nota con LF_REL) in pt s (0 = tutta la durata)
  float c0, c1, ct, q, kt;              // taglio da c0 a c1 in ct s (LF_CENV: c0 a inviluppo 0, c1 a 1); risonanza; inseguimento
  float pw, m0, m1;                     // larghezza dell'impulso / rapporto FM / granelli al secondo; indice FM (o durata granello)
  float vr, vd, vdel;                   // vibrato: frequenza, profondita' (semitoni), ritardo
  float g;                              // guadagno
  float len, ia, idd, ir, ipt, ict, lf, lc, k, gdec, gatk;   // derivati (finish)
  LDef& at(float t) { t0 = t; return *this; }
  LDef& env(float a_, float h_, float d_, int k_ = 2) { a = a_; h = h_; d = d_; dc = (uint8_t)k_; return *this; }
  LDef& sus(float s_, float r_) { s = s_; r = r_; return *this; }
  LDef& hz(float x) { f0 = f1 = x; return *this; }
  LDef& hz(float x, float y, float t = 0.0f, int sh = P_LOG) { f0 = x; f1 = y; pt = t; pc = (uint8_t)sh; return *this; }
  LDef& filter(int ty, float x, float y, float q_, float t) { filt = (uint8_t)ty; c0 = x; c1 = y; q = q_; ct = t; return *this; }
  LDef& lp(float x, float y, float q_ = 0.707f, float t = 0.0f) { return filter(F_LP, x, y, q_, t); }
  LDef& bp(float x, float y, float q_ = 1.0f, float t = 0.0f) { return filter(F_BP, x, y, q_, t); }
  LDef& hp(float x, float y, float q_ = 0.707f, float t = 0.0f) { return filter(F_HP, x, y, q_, t); }
  LDef& ccurve(int sh) { cc = (uint8_t)sh; return *this; }
  LDef& fm(float ratio, float i0, float i1) { pw = ratio; m0 = i0; m1 = i1; return *this; }
  LDef& width(float w) { pw = w; return *this; }
  LDef& grain(float per_s, float dur) { pw = per_s; m0 = dur; return *this; }
  LDef& vib(float rate, float depth, float delay = 0.0f) { vr = rate; vd = depth; vdel = delay; return *this; }
  LDef& key(float x) { kt = x; return *this; }
  LDef& gain(float x) { g = x; return *this; }
  LDef& opt(int f) { fl = (uint8_t)(fl | f); return *this; }
};

struct SDef {
  const char* name;
  uint8_t nl, prio, maxi, flags;
  float peak_db;          // picco voluto per canale, al centro, con gli effetti a 8 e vol = 1
  float norm;             // guadagno che lo ottiene (audio_init)
  float jitter;           // variazione casuale dell'altezza a ogni richiesta (frazione)
  float merge;            // s: richieste piu' vicine di cosi' si fondono in una voce
  float keep;             // SF_LOOP: s di silenzio dopo l'ultima richiesta prima del rilascio
  float duck;             // la musica scende a questo fattore mentre il suono e' acceso
  float step[3];          // SF_STEP: altezza della prima, seconda e terza ripetizione ravvicinata
  LDef L[MAXL];
};

// --- stato --------------------------------------------------------------------------------------

struct LRun {
  float ph[4], ph2[4];    // fasi delle note (fino a 4 negli accordi) e del modulante FM
  float mprev;            // moltiplicatore dell'altezza alla fine del blocco precedente (0 = nessuno)
  float env, e0;          // inviluppo alla fine del blocco precedente; livello da cui riparte l'attacco
  float rel_t, rel_l;     // rilascio: tempo trascorso, livello iniziale
  float z1, z2;           // filtro
  float n0, n1, c;        // rumore morbido (estremi), crepitio
  float ge, gi, mi;       // granelli: inviluppo e incremento; indice FM del blocco precedente
  uint32_t rng;
  uint8_t st, ga;
};

struct Voice {
  const SDef* def;
  int16_t id;
  uint8_t on, gate, kill, nn;
  int age, last_trig, gate_left;     // campioni dall'inizio; eta' all'ultima richiesta; porta: campioni rimasti (-1 = sempre)
  float vol, pan, pl, pr, gl, gr;    // volume, posizione, guadagni di posizione, guadagni applicati
  float kill_g, peak, pitch, send;
  float nf[4], nl2;                  // note (Hz) per gli strati LF_REL; log2(nota / do centrale)
  float glo, glk;                    // musica: scarto dell'altezza in ottave che scivola a zero (fattore per blocco)
  float tro, trr, trp;               // trillo: scarto della nota di sopra in ottave, frequenza, fase
  float gro;                         // acciaccatura: scarto della nota di sopra in ottave ...
  int grn;                           // ... per quanti blocchi ancora
  int strum;                         // accordi arpeggiati: campioni tra una nota e l'altra
  uint8_t sdir;                      // ... dall'alto (1) o dal basso (0)
  LRun lr[MAXL];
};

struct Chord { int8_t root, bass, n, third, fifth, sev; int8_t iv[4]; char name[8]; };
struct Pat { char c[16]; uint8_t len[16]; };
struct DPat { uint8_t v[16][ND]; };
// lo: nota piu' bassa della traccia; per la seconda voce l'intervallo in semitoni sotto la melodia quando lo
// schema dice 'x' (0 = armonizzata). strum: s tra le note di un accordo arpeggiato. glide: s in cui una nota
// legata scivola dalla nota prima (e la scivolata dal basso). ring: accordi e arpeggio su due voci che si danno
// il cambio, la nota vecchia sfuma nel suo rilascio (sempre cosi' con strum). pedal: il tappeto e' un bordone
// sulla tonica e la quinta del brano, fermo sotto gli accordi.
struct TrackCfg { uint8_t inst; float vol, pan, send, gate; int lo; float strum, glide; uint8_t ring, pedal; };

struct Song {
  const char* name;
  float bpm, swing, gain, dly_fb, dly_mix;
  int nbars, loop, bar_len, off[17], dly1, dly2, key;
  int8_t scale[7];
  int nch;
  Chord ch[MAXCH];
  uint8_t cat[MAXSTEPS], note[MAXSTEPS], nlen[MAXSTEPS];   // accordo di ogni sedicesimo; melodia: nota e durata
  uint8_t orn[MAXSTEPS];         // abbellimenti O_* della nota
  TrackCfg tc[NTRACK], tl2;      // tracce; secondo strumento della melodia
  float dvol[ND], dpan[ND], tom[3];
  uint8_t dk[ND];                // strumento di ogni riga della batteria
  bool choke;                    // il charleston aperto e chiuso si zittiscono a vicenda
  Pat pat[4][6];                 // modelli di basso, accordi, tappeto, arpeggio
  DPat dp[8];
  uint8_t arr[NTRACK][MAXBARS];  // per battuta: modello da usare (255 = traccia muta)
  int8_t hiv[MAXBARS];           // per battuta: seconda voce (HV_OFF, HV_HARM o semitoni sotto la melodia)
  uint8_t larr[MAXBARS];         // per battuta: 1 = la melodia la suona il secondo strumento
  uint8_t darr[MAXBARS];
};

struct SongSrc {
  const char* name;
  float bpm, swing, gain, dly_fb, dly_mix;
  int loop, key, mode;
  const char* chords;
  const char* lead;
  const char* pat[4][6];
  const char* arr[4];
  const char* harm;
  const char* larr;
  const char* drum[8][ND];
  const char* darr;
  TrackCfg tc[NTRACK], tl2;
  float dvol[ND], dpan[ND], tom[3];
  int dk[ND];
  bool choke;
};

struct Player {
  int song;
  uint8_t on, ending;
  int wait;                  // blocchi da aspettare prima di partire (dissolvenza)
  int pending;               // brano da far partire appena questo lettore tace (-1 nessuno)
  int bar, step, pos, next;  // posizione: battuta, sedicesimo, campioni dall'inizio della battuta, prossimo passo
  uint8_t calt, aalt;        // voce in uso di accordi e arpeggio quando si danno il cambio (TrackCfg.ring)
  int roll[ND];              // rulli: campioni che mancano al secondo colpo di ogni riga (0 = nessuno) ...
  uint8_t rollv[ND];         // ... e il suo codice
  float g, gt, gstep;        // guadagno della dissolvenza, valore voluto, passo per blocco
  Voice v[NPV];
  float send[BLK];
  float dly[DLY_N];
  int dw;
  float dlp, dlvl;
};

static struct {
  bool ready;
  float fs, ifs;
  float ii[BLK + 1], tt[BLK + 1];   // i e i*(i-1)/2: fasi in forma chiusa dentro il blocco
  uint32_t rng;
  unsigned now;
  Voice sv[NSFXV];
  float vb[BLK], tmp[BLK];
  int vol_m, vol_s;
  float vm, vs, duck_g, auto_g, gm, gs;   // rampe: volumi, abbassamento della pausa e dei suoni, guadagni del blocco
  float k_fast, k_duck, k_auto_dn, k_auto_up, k_rel;
  bool duck;
  int mus_cur;
  float dc_x[2], dc_y[2], dc_r;
  float lim_g, lim_pk, lim_buf[2][BLK];
  int16_t out[BLK * 2];
  int out_pos;
  unsigned step_last[NSOUNDS];
  uint8_t step_n[NSOUNDS];
  AudioStats st;
  int nerr;
  char err[600];
  uint32_t cal_hash[NSOUNDS];   // taratura: impronta e picco di ogni suono (dalla tabella o misurati)
  float cal_peak[NSOUNDS];
} G;

static SDef g_snd[NSOUNDS];
static SDef g_ins[NINST];
static Song g_song[NMUSIC];
static Player g_pl[NPLAYER];

static const char* const SOUND_NAMES[NSOUNDS] = {
  "nessuno", "blaster", "cristalli", "ghiaccio_secco", "fiamma", "lava", "razzi", "balzo_lunare", "schianto",
  "tempesta_gelata", "eruzione", "speciale_pronto", "salto", "atterraggio", "trampolino", "bomba_giu", "esplosione",
  "esplosione_piccola", "talismano", "bomba_presa", "colpito", "a_terra", "rientro",
  "colpo", "sconfitto", "scorpione", "arciere", "scarabei", "guerriero", "gladiatore", "giavellotto", "spadata",
  "cassa_colpita", "cassa_rotta", "capsula", "esploratore_salvato", "aiuto", "cancello", "zona_finita", "ondata", "stella",
  "tutti_giu", "menu_sposta", "menu_ok", "menu_indietro", "carte", "carta_scelta", "pausa", "freccia_a_terra", "cenote",
  "passo_gladiatore", "spada_alzata", "fendente", "carica_scudo", "scudo_contro_muro",
};
static const char* const MUSIC_NAMES[NMUSIC] = { "nessuno", "titolo", "piramidi", "chichenitza", "muraglia", "colosseo", "vittoria" };

// --- sintesi di uno strato -----------------------------------------------------------------------

// Oscillatori con altezza: fasi in forma chiusa p_i = p0 + i*inc0 + i(i-1)/2*dinc, una per nota.
// Accordi arpeggiati (v.strum): la nota k parte k intervalli dopo la prima (dall'alto se v.sdir) e i suoi
// primi STRUM_RAMP campioni salgono in rampa, cosi' l'attacco non scatta.
static void osc_tonal(const LDef& d, LRun& r, const Voice& v, float mp, float m, float t, float* o) {
  const int nn = (d.fl & LF_REL) ? v.nn : 1;
  float mi0 = 0.0f, dmi = 0.0f;
  if (d.wave == W_FM) {
    const float mi1 = d.m0 + (d.m1 - d.m0) * minf(t / d.len, 1.0f);
    mi0 = r.mi;
    dmi = (mi1 - mi0) * (1.0f / BLK);
    r.mi = mi1;
  }
  float tb[BLK];
  for (int k = 0; k < nn; ++k) {
    int i0 = 0, start = -STRUM_RAMP;
    float* dst = o;
    if (v.strum > 0 && nn > 1) {
      start = (v.sdir ? nn - 1 - k : k) * v.strum - v.age;   // primo campione della nota k in questo blocco
      if (start >= BLK) continue;                            // non e' ancora il suo momento
      if (start > -STRUM_RAMP) { i0 = start > 0 ? start : 0; memset(tb, 0, sizeof(tb)); dst = tb; }
    }
    const float base = ((d.fl & LF_REL) ? v.nf[k] : 1.0f) * G.ifs;
    const float inc0 = minf(mp * base, 0.45f), inc1 = minf(m * base, 0.45f);
    const float dinc = (inc1 - inc0) * (1.0f / BLK);
    const float ph = r.ph[k];
    const float dt = maxf(0.5f * (inc0 + inc1), 1e-6f), idt = 1.0f / dt;
    switch (d.wave) {
    case W_SINE:
      for (int i = i0; i < BLK; ++i) {
        float p = ph + G.ii[i] * inc0 + G.tt[i] * dinc;
        p -= (float)(int)p;
        dst[i] += fsin01(p);
      }
      break;
    case W_TRI:
      for (int i = i0; i < BLK; ++i) {
        float p = ph + G.ii[i] * inc0 + G.tt[i] * dinc;
        p -= (float)(int)p;
        dst[i] += 1.0f - 4.0f * fabsf(p - 0.5f);
      }
      break;
    case W_PULSE: {
      const float w = d.pw, dcoff = w + w - 1.0f;
      for (int i = i0; i < BLK; ++i) {
        float p = ph + G.ii[i] * inc0 + G.tt[i] * dinc;
        p -= (float)(int)p;
        float q = p - w;
        if (q < 0.0f) q += 1.0f;
        dst[i] += (p < w ? 1.0f : -1.0f) + blep(p, dt, idt) - blep(q, dt, idt) - dcoff;
      }
      break;
    }
    case W_SAW:
      for (int i = i0; i < BLK; ++i) {
        float p = ph + G.ii[i] * inc0 + G.tt[i] * dinc;
        p -= (float)(int)p;
        dst[i] += p + p - 1.0f - blep(p, dt, idt);
      }
      break;
    case W_FM: {
      const float ratio = d.pw, p2 = r.ph2[k];
      float mi = mi0 + dmi * (float)i0;
      for (int i = i0; i < BLK; ++i) {
        const float s = G.ii[i] * inc0 + G.tt[i] * dinc;
        float p = ph + s, q = p2 + ratio * s;
        p -= (float)(int)p;
        q -= (float)(int)q;
        float c = p + mi * fsin01(q);
        c -= (float)(int)c;
        if (c < 0.0f) c += 1.0f;
        dst[i] += fsin01(c);
        mi += dmi;
      }
      const float q = p2 + ratio * (G.ii[BLK] * inc0 + G.tt[BLK] * dinc);
      r.ph2[k] = q - (float)(int)q;
      break;
    }
    default: break;
    }
    if (dst != o)
      for (int i = i0; i < BLK; ++i) o[i] += tb[i] * minf((float)(i - start) * (1.0f / STRUM_RAMP), 1.0f);
    const float e = ph + G.ii[BLK] * inc0 + G.tt[BLK] * dinc;
    r.ph[k] = e - (float)(int)e;
  }
}

// Rumori: bianco, morbido (valori a caso alla frequenza dello strato, raccordati), crepitio, granelli.
static void osc_noise(const LDef& d, LRun& r, const Voice& v, float mp, float m, float* o) {
  uint32_t s = r.rng;
  switch (d.wave) {
  case W_NOISE:
    for (int i = 0; i < BLK; ++i) o[i] = u2f(xs32(s));
    break;
  case W_SNOISE: {
    float ph = r.ph[0], a = r.n0, b = r.n1;
    float inc = minf(mp * G.ifs, 0.45f);
    const float dinc = (minf(m * G.ifs, 0.45f) - inc) * (1.0f / BLK);
    for (int i = 0; i < BLK; ++i) {
      ph += inc;
      inc += dinc;
      if (ph >= 1.0f) { ph -= 1.0f; a = b; b = u2f(xs32(s)); }
      o[i] = a + (b - a) * (ph * ph * (3.0f - 2.0f * ph));
    }
    r.ph[0] = ph; r.n0 = a; r.n1 = b;
    break;
  }
  case W_CRACK: {
    // impulsi radi (m al secondo) di ampiezza casuale, ammorbiditi su qualche campione
    const uint32_t thr = (uint32_t)(minf(m * G.ifs, 1.0f) * 4294967000.0f);
    float c = r.c;
    for (int i = 0; i < BLK; ++i) {
      const uint32_t x = xs32(s);
      c *= 0.55f;
      if (x < thr) c += u2f(x * 2654435761u) * 2.0f;
      o[i] = c;
    }
    r.c = fabsf(c) < 1e-20f ? 0.0f : c;
    break;
  }
  case W_GRAIN: {
    // granelli di seno ad altezza casuale tra f0 e f1, pw al secondo, ognuno con il suo piccolo inviluppo
    const uint32_t thr = (uint32_t)(minf(d.pw * G.ifs, 1.0f) * 4294967000.0f);
    float ge = r.ge, gi = r.gi, ph = r.ph[0];
    uint8_t ga = r.ga;
    for (int i = 0; i < BLK; ++i) {
      if (xs32(s) < thr) { gi = (d.f0 + (d.f1 - d.f0) * rnd01(s)) * v.pitch * G.ifs; ga = 1; }
      if (ga) { ge += d.gatk; if (ge >= 1.0f) { ge = 1.0f; ga = 0; } }
      else ge *= d.gdec;
      o[i] = fsin01(ph) * ge;
      ph += gi;
      if (ph >= 1.0f) ph -= 1.0f;
    }
    r.ge = ge < 1e-20f ? 0.0f : ge; r.gi = gi; r.ph[0] = ph; r.ga = ga;
    break;
  }
  default: break;
  }
  r.rng = s;
}

static void filter_block(const LDef& d, LRun& r, const Voice& v, float t, float e1, float* o) {
  float c = d.c0;
  if (d.fl & LF_CENV) c *= fexp2(d.lc * e1);
  else if (d.lc != 0.0f) c *= fexp2(d.lc * shape(d.cc, minf(t * d.ict, 1.0f)));
  if (d.kt > 0.0f) c *= fexp2(d.kt * v.nl2);
  c = clampf(c, 20.0f, 0.45f * G.fs);
  // filtro a variabili di stato in forma TPT (stabile anche con il taglio che si muove): g = tan(pi c / fs)
  const float x = c * G.ifs * 0.5f;
  const float g = fsin01(x) / fsin01(x + 0.25f);
  const float k = d.k, a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
  float z1 = r.z1, z2 = r.z2;
  switch (d.filt) {
  case F_LP:
    for (int i = 0; i < BLK; ++i) {
      const float v3 = o[i] - z2, v1 = a1 * z1 + a2 * v3, v2 = z2 + a2 * z1 + a3 * v3;
      z1 = v1 + v1 - z1; z2 = v2 + v2 - z2;
      o[i] = v2;
    }
    break;
  case F_BP:
    for (int i = 0; i < BLK; ++i) {
      const float v3 = o[i] - z2, v1 = a1 * z1 + a2 * v3, v2 = z2 + a2 * z1 + a3 * v3;
      z1 = v1 + v1 - z1; z2 = v2 + v2 - z2;
      o[i] = k * v1;
    }
    break;
  default:
    for (int i = 0; i < BLK; ++i) {
      const float v3 = o[i] - z2, v1 = a1 * z1 + a2 * v3, v2 = z2 + a2 * z1 + a3 * v3;
      z1 = v1 + v1 - z1; z2 = v2 + v2 - z2;
      o[i] = o[i] - k * v1 - v2;
    }
    break;
  }
  r.z1 = fabsf(z1) < 1e-20f ? 0.0f : z1;
  r.z2 = fabsf(z2) < 1e-20f ? 0.0f : z2;
}

// Un blocco di uno strato, sommato in G.vb. false quando lo strato ha finito.
static bool layer_block(const LDef& d, LRun& r, Voice& v) {
  if (r.st == ST_DONE) return false;
  const float t = (float)(v.age + BLK) * G.ifs - d.t0;   // tempo dello strato alla fine del blocco
  if (t <= 0.0f) return true;                          // non ancora partito
  const float e0 = r.env;
  float e1;
  bool last = false;
  if (r.st == ST_RUN && !v.gate && !(d.fl & LF_ONESHOT)) { r.st = ST_REL; r.rel_l = e0; r.rel_t = 0.0f; }
  if (r.st == ST_REL) {
    r.rel_t += (float)BLK * G.ifs;
    const float u = r.rel_t * d.ir;
    if (u >= 1.0f) { e1 = 0.0f; last = true; }
    else { const float w = 1.0f - u; e1 = r.rel_l * w * w; }
  } else if (t < d.a) {
    e1 = r.e0 + (1.0f - r.e0) * t * d.ia;
  } else if (t < d.a + d.h) {
    e1 = 1.0f;
  } else {
    const float u = (t - d.a - d.h) * d.idd;
    if (u >= 1.0f) { e1 = d.s; last = d.s <= 0.0f; }
    else {
      const float w = 1.0f - u;
      float wk = w;
      for (int k = 1; k < d.dc; ++k) wk *= w;
      e1 = d.s + (1.0f - d.s) * wk;
    }
  }
  r.env = e1;
  if (last) r.st = ST_DONE;
  if (e0 <= 0.0f && e1 <= 0.0f) return !last;

  // altezza (moltiplicatore della nota o frequenza), con vibrato
  float m = d.f0 * v.pitch;
  if (d.lf != 0.0f) m *= fexp2(d.lf * shape(d.pc, minf(t * d.ipt, 1.0f)));
  if (d.vd > 0.0f && t > d.vdel) {
    float dep = d.vd * minf((t - d.vdel) * 5.0f, 1.0f);
    if (d.fl & LF_VIBENV) dep *= e1;
    const float vp = t * d.vr;
    m *= fexp2(dep * (1.0f / 12.0f) * fsin01(vp - (float)(int)vp));
  }
  const float mp = r.mprev > 0.0f ? r.mprev : m;
  r.mprev = m;

  float* o = G.tmp;
  if (d.wave >= W_NOISE && d.wave != W_FM) osc_noise(d, r, v, mp, m, o);
  else { memset(o, 0, sizeof(G.tmp)); osc_tonal(d, r, v, mp, m, t, o); }
  if (d.filt != F_NONE) filter_block(d, r, v, t, e1, o);
  const float de = (e1 - e0) * (1.0f / BLK), g = d.g;
  float* vb = G.vb;
  for (int i = 0; i < BLK; ++i) vb[i] += o[i] * (e0 + de * G.ii[i + 1]) * g;
  return !last;
}

// Un blocco di una voce in G.vb; false quando la voce ha finito.
static bool voice_block(Voice& v) {
  memset(G.vb, 0, sizeof(G.vb));
  const SDef& d = *v.def;
  bool alive = false;
  for (int i = 0; i < d.nl; ++i)
    if (layer_block(d.L[i], v.lr[i], v)) alive = true;
  v.age += BLK;
  float s = 0.0f;
  for (int i = 0; i < BLK; ++i) s += fabsf(G.vb[i]);
  if (!(s < 1e6f)) {   // NaN o valori assurdi: non dovrebbe succedere mai; la voce si spegne
    memset(G.vb, 0, sizeof(G.vb));
    for (int i = 0; i < d.nl; ++i) memset(&v.lr[i], 0, sizeof(LRun));
    G.st.bad++;
    return false;
  }
  return alive;
}

static float voice_level(const Voice& v) {
  float e = 0.0f;
  for (int i = 0; i < v.def->nl; ++i) e = maxf(e, v.lr[i].env);
  return e * (v.kill ? v.kill_g : 1.0f);
}

static void voice_off(Voice& v) {
  if (v.on) G.st.end_max = maxf(G.st.end_max, voice_level(v));
  v.on = 0;
}

// Somma la voce (G.vb) nel bus stereo con guadagni in rampa; send (se c'e') riceve la mandata all'eco.
static void voice_mix(Voice& v, float target, float* L, float* R, float* send) {
  const float tl = target * v.pl, tr = target * v.pr;
  const float dl = (tl - v.gl) * (1.0f / BLK), dr = (tr - v.gr) * (1.0f / BLK);
  const float gl = v.gl, gr = v.gr;
  float pk = 0.0f;
  for (int i = 0; i < BLK; ++i) {
    const float x = G.vb[i], a = G.ii[i + 1];
    L[i] += x * (gl + dl * a);
    R[i] += x * (gr + dr * a);
    pk = maxf(pk, fabsf(x));
  }
  if (send && v.send > 0.0f)
    for (int i = 0; i < BLK; ++i) send[i] += G.vb[i] * v.send * target;
  v.gl = tl; v.gr = tr;
  v.peak = pk * target;
}

static void set_pan(Voice& v, float pan, float width) {
  v.pan = pan;
  const float a = (clampf(pan, -1.0f, 1.0f) * width + 1.0f) * 0.125f;   // angolo in giri: 0 .. 1/4
  v.pl = fsin01(a + 0.25f);
  v.pr = fsin01(a);
}

// --- effetti: voci ------------------------------------------------------------------------------

static void sfx_start(Voice& v, int id, float pan, float vol, float pitch) {
  const SDef& d = g_snd[id];
  memset(&v, 0, sizeof(v));
  v.def = &d; v.id = (int16_t)id; v.on = 1; v.gate = 1; v.gate_left = -1; v.nn = 1; v.nf[0] = 1.0f;
  v.vol = vol; v.pitch = pitch; v.kill_g = 1.0f;
  set_pan(v, pan, 0.75f);
  v.gl = vol * d.norm * v.pl;
  v.gr = vol * d.norm * v.pr;
  for (int i = 0; i < d.nl; ++i) v.lr[i].rng = xs32(G.rng) | 1u;
}

void audio_play(int id, float pan, float vol) {
  if (!G.ready || id <= SND_NONE || id >= NSOUNDS) return;
  if (!(vol > 0.0f)) return;   // anche NaN
  if (vol > 1.0f) vol = 1.0f;
  if (!(pan == pan)) pan = 0.0f;
  pan = clampf(pan, -1.0f, 1.0f);
  const SDef& d = g_snd[id];
  if (!d.nl || d.norm <= 0.0f) return;
  if (d.flags & SF_LOOP) {
    // la voce gia' accesa piu' vicina resta accesa: nessuna nuova partenza, nessun effetto mitragliatrice
    Voice* best = 0;
    float bd = 0.6f;
    for (int k = 0; k < NSFXV; ++k) {
      Voice& v = G.sv[k];
      if (v.on && !v.kill && v.id == id && v.gate && fabsf(v.pan - pan) < bd) { bd = fabsf(v.pan - pan); best = &v; }
    }
    if (best) {
      best->last_trig = best->age;
      best->vol += (vol - best->vol) * 0.5f;
      set_pan(*best, best->pan + (pan - best->pan) * 0.5f, 0.75f);
      G.st.merged++;
      return;
    }
  } else if (d.merge > 0.0f) {
    // richieste ravvicinate (piu' colpi nello stesso fotogramma): una voce sola, un po' piu' forte
    const int win = (int)(d.merge * G.fs);
    Voice* y = 0;
    for (int k = 0; k < NSFXV; ++k) {
      Voice& v = G.sv[k];
      if (v.on && !v.kill && v.id == id && v.age < win && (!y || v.age < y->age)) y = &v;
    }
    if (y) {
      y->vol = minf(1.3f, maxf(y->vol, vol) + 0.3f * minf(y->vol, vol));
      set_pan(*y, (y->pan + pan) * 0.5f, 0.75f);
      G.st.merged++;
      return;
    }
  }
  // serve un posto libero: se sono tutti occupati (anche da voci che stanno sfumando) la richiesta si scarta,
  // cosi' nessuna voce viene mai interrotta di colpo
  Voice* slot = 0;
  for (int k = 0; k < NSFXV && !slot; ++k)
    if (!G.sv[k].on) slot = &G.sv[k];
  if (!slot) { G.st.dropped++; return; }
  // limite di copie dello stesso suono: si sfuma la piu' vecchia
  int cnt = 0, act = 0;
  Voice* old = 0;
  for (int k = 0; k < NSFXV; ++k) {
    Voice& v = G.sv[k];
    if (!v.on || v.kill) continue;
    act++;
    if (v.id == id) { cnt++; if (!old || v.age > old->age) old = &v; }
  }
  if (cnt >= d.maxi && old) { old->kill = 1; G.st.stolen++; act--; }
  // limite di voci: si ruba la meno importante, poi la piu' bassa e la piu' vecchia
  if (act >= VLIMIT) {
    Voice* vic = 0;
    float best = 1e30f;
    for (int k = 0; k < NSFXV; ++k) {
      Voice& v = G.sv[k];
      if (!v.on || v.kill) continue;
      const float score = (float)v.def->prio * 1000.0f + v.peak * 1000.0f - (float)v.age * G.ifs * 10.0f;
      if (score < best) { best = score; vic = &v; }
    }
    if (!vic || vic->def->prio > d.prio) { G.st.dropped++; return; }
    vic->kill = 1;
    G.st.stolen++;
  }
  float pitch = 1.0f + d.jitter * u2f(xs32(G.rng));
  if (d.flags & SF_STEP) {
    const bool near = G.now - G.step_last[id] < (unsigned)(0.75f * G.fs);
    G.step_n[id] = (uint8_t)(near ? (G.step_n[id] < 2 ? G.step_n[id] + 1 : 2) : 0);
    G.step_last[id] = G.now;
    pitch = d.step[G.step_n[id]];
  }
  sfx_start(*slot, id, pan, vol, pitch);
  G.st.plays++;
}

// --- musica: sequencer ---------------------------------------------------------------------------

static bool chord_has(const Chord& c, int pc) {
  for (int k = 0; k < c.n; ++k)
    if (pcm(c.root + c.iv[k]) == pcm(pc)) return true;
  return false;
}
static bool in_scale(const Song& s, int n) {
  for (int k = 0; k < 7; ++k)
    if (s.scale[k] == pcm(n)) return true;
  return false;
}
static int place(int pc, int lo) { return lo + pcm(pc - lo); }   // la nota di classe pc in [lo, lo + 12)

// Basso dell'accordo che viene dopo quello del passo gs (per le note di avvicinamento).
static int next_bass(const Song& s, int gs) {
  const uint8_t c0 = s.cat[gs];
  int g = gs;
  for (int n = 0; n < 32; ++n) {
    if (++g >= s.nbars * 16) {
      if (s.loop < 0) break;
      g = s.loop * 16;
    }
    if (s.cat[g] != c0) return s.ch[s.cat[g]].bass;
  }
  return s.ch[c0].bass;
}

static int bass_note(const Song& s, int gs, char code) {
  const Chord& c = s.ch[s.cat[gs]];
  const int lo = s.tc[T_BASS].lo, r = place(c.root, lo);
  switch (code) {
  case '1': return place(c.bass, lo);
  case 'b': return r + 1;
  case '2': return r + 2;
  case '3': return r + c.third;
  case '4': return r + 5;
  case '5': return r + c.fifth;
  case '6': return r + (c.third == 3 ? 8 : 9);
  case '7': return r + (c.sev >= 0 ? c.sev : 10);
  case '8': return r + 12;
  case '^': return place(next_bass(s, gs), lo) - 1;
  case '<': case '>': {
    const int n = place(next_bass(s, gs), lo), dir = code == '<' ? -1 : 1;
    for (int k = 1; k <= 2; ++k)
      if (in_scale(s, n + dir * k)) return n + dir * k;
    return n + dir;
  }
  default: return -1;
  }
}

// Note dell'accordo nell'ottava [lo, lo + 12), in ordine; con 3 voci e accordi di 4 note si toglie la quinta.
static int voicing(const Chord& c, int lo, int maxn, int* out) {
  int n = 0;
  for (int k = 0; k < c.n; ++k) {
    if (c.n == 4 && maxn == 3 && c.iv[k] == c.fifth) continue;
    out[n++] = place(c.root + c.iv[k], lo);
  }
  for (int i = 1; i < n; ++i)
    for (int j = i; j > 0 && out[j] < out[j - 1]; --j) { const int x = out[j]; out[j] = out[j - 1]; out[j - 1] = x; }
  return n;
}

// Seconda voce: la nota dell'accordo sotto la melodia (almeno una terza minore), o la terza sotto nella scala.
static int harm_note(const Song& s, const Chord& c, int lead) {
  if (chord_has(c, lead)) {
    for (int n = lead - 3; n > lead - 12; --n)
      if (chord_has(c, n)) return n;
  } else if (in_scale(s, lead)) {
    int idx = 0;
    for (int k = 0; k < 7; ++k)
      if (s.scale[k] == pcm(lead)) idx = k;
    return lead - pcm(lead - s.scale[(idx + 5) % 7]);
  }
  return lead - 4;
}

static void voice_retrig(Voice& v, const SDef* d) {
  if (!v.on || v.def != d) {
    if (!v.on) {
      memset(&v, 0, sizeof(v));
      for (int i = 0; i < MAXL; ++i) v.lr[i].rng = xs32(G.rng) | 1u;
    }
    v.def = d;
    v.on = 1;
  }
  v.age = 0;
  v.gate = 1;
  v.kill_g = 1.0f;
  for (int i = 0; i < d->nl; ++i) {
    LRun& r = v.lr[i];
    r.st = ST_RUN;
    r.e0 = r.env;      // l'attacco riparte da dove e' l'inviluppo: niente scatti
    r.mprev = 0.0f;
  }
}

static void note_on(Voice& v, const TrackCfg& tc, const int* notes, int nn, int gate, float vel) {
  voice_retrig(v, &g_ins[tc.inst]);
  v.gate_left = gate > BLK ? gate : BLK;
  v.nn = (uint8_t)nn;
  for (int k = 0; k < nn; ++k) v.nf[k] = midi_hz(notes[k]);
  v.nl2 = (float)(notes[0] - 60) * (1.0f / 12.0f);
  v.vol = tc.vol * vel;
  v.pitch = 1.0f;
  v.glo = v.tro = 0.0f;
  v.grn = 0;
  v.strum = nn > 1 ? (int)(tc.strum * G.fs) : 0;
  v.send = tc.send;
  set_pan(v, tc.pan, 1.0f);
}

static void note_off(Voice& v) { if (v.on) v.gate = 0; }

// Nota della melodia (o della seconda voce) con il suo abbellimento. Legata: se la voce tiene ancora lo
// stesso strumento non riparte l'attacco, l'altezza scivola dalla nota di prima in tc.glide secondi.
static void melody_on(Voice& v, const TrackCfg& tc, const Song& s, int n, int o, int gate) {
  const SDef* d = &g_ins[tc.inst];
  const bool held = v.on && v.gate && v.def == d;
  const float hz = midi_hz(n), was = held ? v.nf[0] * v.pitch : 0.0f;   // altezza che suona adesso
  const float vel = (o & O_ACC) ? 1.25f : ((o & O_SOFT) ? 0.6f : 1.0f);
  const bool slide = (o & O_SLIDE) && held && was > 0.0f;
  if (slide) {
    v.gate_left = gate > BLK ? gate : BLK;
    v.vol = tc.vol * vel;
    // le fasi sono in multipli della nota: il blocco dopo deve ripartire dall'altezza di adesso
    const float k = v.nf[0] / hz;
    for (int i = 0; i < d->nl; ++i) v.lr[i].mprev *= k;
  } else {
    note_on(v, tc, &n, 1, gate, vel);
  }
  v.nf[0] = hz;
  v.nl2 = (float)(n - 60) * (1.0f / 12.0f);
  v.tro = 0.0f;
  v.grn = 0;
  float tau = 0.0f;
  v.glo = 0.0f;
  if (slide) { v.glo = log2f(was / hz); tau = maxf(tc.glide, 0.03f) * (1.0f / 3.0f); }
  else if (o & O_SCOOP) { v.glo = -2.0f / 12.0f; tau = maxf(tc.glide, 0.06f) * (1.0f / 3.0f); }   // da un tono sotto
  v.glk = tau > 0.0f ? expf(-(float)BLK / (tau * G.fs)) : 0.0f;
  if (o & (O_TRILL | O_GRACE)) {   // con la nota di sopra della scala
    int up = 2;
    for (int k = 1; k <= 3; ++k)
      if (in_scale(s, n + k)) { up = k; break; }
    if (o & O_GRACE) { v.gro = (float)up * (1.0f / 12.0f); v.grn = (int)(0.05f * G.fs) / BLK; }   // 50 ms sopra
    if (o & O_TRILL) { v.tro = (float)up * (1.0f / 12.0f); v.trr = 7.5f; v.trp = -0.6f; }        // dopo 80 ms sulla nota
  }
  v.pitch = fexp2(v.glo + (v.grn > 0 ? v.gro : 0.0f));
}

// Colpo di batteria: code = forza (1-3) | tom (1-3) << 2 | DR_ROLL.
static void drum_hit(Player& p, const Song& s, int k, int code) {
  static const int SLOT[ND] = { 0, 1, 2, 2, 3, 4, 5 };   // il charleston aperto e chiuso si zittiscono a vicenda
  static const float VEL[4] = { 0.0f, 0.45f, 0.75f, 1.0f };
  Voice& v = p.v[V_DRUM + (s.choke ? SLOT[k] : k)];
  voice_retrig(v, &g_ins[s.dk[k]]);
  v.gate_left = -1;
  v.nn = 1;
  v.nf[0] = 1.0f;
  v.pitch = 1.0f;
  v.glo = v.tro = 0.0f;
  v.grn = 0;
  v.strum = 0;
  int vel = code & 3;
  if (k == DK_TOM) {   // toms (o tamburi accordati): 1 acuto, 2 medio, 3 grave
    const int t = (code >> 2) & 3;
    v.pitch = s.tom[t >= 1 && t <= 3 ? t - 1 : 1];
  }
  if (code & DR_ROLL) {   // il secondo colpo del rullo, piu' piano, mezzo sedicesimo dopo
    p.roll[k] = s.bar_len / 32;
    p.rollv[k] = (uint8_t)((code & 12) | (vel > 1 ? vel - 1 : 1));
  }
  v.vol = s.dvol[k] * VEL[vel];
  v.send = 0.0f;
  set_pan(v, k == DK_TOM ? (v.pitch - 1.0f) + s.dpan[k] : s.dpan[k], 1.0f);
}

static void seq_step(Player& p, const Song& s) {
  const int bar = p.bar, st = p.step, gs = bar * 16 + st;
  const Chord& c = s.ch[s.cat[gs]];
  const bool chg = st > 0 && s.cat[gs] != s.cat[gs - 1];
  const float step_len = (float)s.bar_len * (1.0f / 16.0f);
  // melodia (uno dei due strumenti, battuta per battuta) e seconda voce
  if (s.note[gs]) {
    const int n = s.note[gs], o = s.orn[gs];
    const bool b2 = s.larr[bar] == 1;
    const TrackCfg& tl = b2 ? s.tl2 : s.tc[T_LEAD];
    // se la nota dopo e' legata a questa, questa si tiene fino al suo arrivo
    const int nx = gs + s.nlen[gs];
    const bool tie = nx < s.nbars * 16 && s.note[nx] && (s.orn[nx] & O_SLIDE);
    const float len = (float)s.nlen[gs] * step_len;
    melody_on(p.v[b2 ? V_LEAD2 : V_LEAD], tl, s, n, o, (int)(len * (tie ? 1.05f : tl.gate)));
    const int hv = s.hiv[bar];
    if (hv != HV_OFF) {
      const int h = hv == HV_HARM ? harm_note(s, c, n) : n - hv;
      melody_on(p.v[V_HARM], s.tc[T_HARM], s, h, o, (int)(len * (tie ? 1.05f : s.tc[T_HARM].gate)));
    }
  }
  // tracce a modelli
  for (int t = T_BASS; t <= T_ARP; ++t) {
    const uint8_t pi = s.arr[t][bar];
    const TrackCfg& tc = s.tc[t];
    const int v0 = t == T_BASS ? V_BASS : (t == T_CHORD ? V_CHORD : (t == T_PAD ? V_PAD : V_ARP));
    const int v1 = t == T_CHORD ? V_CHORD2 : (t == T_ARP ? V_ARP2 : v0);
    const bool ring = v1 != v0 && (tc.ring || tc.strum > 0.0f);
    uint8_t& alt = t == T_CHORD ? p.calt : p.aalt;
    Voice& cur = p.v[ring && alt ? v1 : v0];
    if (pi == 255) {
      if (st == 0) { note_off(p.v[v0]); note_off(p.v[v1]); }
      continue;
    }
    const Pat& pt = s.pat[t - T_BASS][pi];
    const char ch = pt.c[st];
    const int gate = (int)((float)pt.len[st] * step_len * tc.gate);
    if (ch == '.') { note_off(p.v[v0]); note_off(p.v[v1]); continue; }
    int notes[4], nn = 0;
    bool held = false;   // '-': continua la nota di prima sulle note del nuovo accordo
    if (t == T_BASS) {
      if (ch == '-') continue;
      notes[0] = bass_note(s, gs, ch);
      nn = notes[0] >= 0 ? 1 : 0;
    } else if (t == T_ARP) {
      if (ch < '0' || ch > '9') continue;
      int tones[4];
      const int n = voicing(c, tc.lo, 4, tones), i = ch - '0';
      notes[0] = tones[i % n] + 12 * (i / n);
      nn = 1;
    } else if (t == T_PAD && tc.pedal) {
      // bordone: tonica e quinta del brano; 'x' lo accende se tace, poi resta fino a '.' o alla traccia muta
      if ((ch != 'x' && ch != 'o') || (cur.on && cur.gate)) continue;
      notes[0] = place(s.key, tc.lo);
      notes[1] = place(s.key + 7, tc.lo);
      if (notes[1] < notes[0]) { const int x = notes[0]; notes[0] = notes[1]; notes[1] = x; }
      nn = 2;
    } else {
      // accordi e tappeto: 'x' suona, 'o' suona piano, '-' tiene ma cambia con l'accordo
      held = ch == '-' && chg && cur.on && cur.gate;
      if (ch == 'x' || ch == 'o' || held) {
        nn = voicing(c, tc.lo, tc.strum > 0.0f ? 4 : 3, notes);
        if (tc.strum > 0.0f && nn == 3) notes[nn++] = notes[0] + 12;   // chitarrina: quattro corde
      }
    }
    if (nn > 0) {
      int g = gate;
      float vel = ch == 'o' ? 0.55f : 1.0f;
      if (held) {   // resto della nota tenuta, con la forza di prima
        int left = 1;
        for (int j = st + 1; j < 16 && pt.c[j] == '-'; ++j) left++;
        g = (int)((float)left * step_len * tc.gate);
        vel = tc.vol > 0.0f ? cur.vol / tc.vol : 1.0f;
      }
      Voice* v = &cur;
      if (ring && (!held || tc.strum > 0.0f)) {   // la voce vecchia sfuma, la nuova parte da zitta
        note_off(cur);
        alt ^= 1;
        v = &p.v[alt ? v1 : v0];
      }
      note_on(*v, tc, notes, nn, g, vel);
      if (t == T_PAD && tc.pedal) v->gate_left = -1;
      v->sdir = (uint8_t)(st & 1);   // arpeggiati: in battere dal basso, in levare dall'alto
    }
  }
  // batteria
  const uint8_t di = s.darr[bar];
  if (di != 255)
    for (int k = 0; k < ND; ++k)
      if (s.dp[di].v[st][k]) drum_hit(p, s, k, s.dp[di].v[st][k]);
}

static void player_release(Player& p) {
  for (int k = 0; k < NPV; ++k) note_off(p.v[k]);
}

static void seq_block(Player& p, const Song& s) {
  while (p.pos >= p.next) {
    seq_step(p, s);
    if (++p.step >= 16) {
      p.step = 0;
      p.pos -= s.bar_len;
      if (++p.bar >= s.nbars) {
        if (s.loop < 0) { p.ending = 1; player_release(p); p.pos += BLK; return; }
        p.bar = s.loop;
      }
    }
    p.next = s.off[p.step];
  }
  p.pos += BLK;
}

static void player_start(Player& p, int song, int wait) {
  memset(p.v, 0, sizeof(p.v));
  memset(p.dly, 0, sizeof(p.dly));
  p.song = song; p.on = 1; p.ending = 0; p.wait = wait; p.pending = -1;
  p.bar = p.step = p.pos = p.next = 0;
  p.calt = p.aalt = 0;
  memset(p.roll, 0, sizeof(p.roll));
  p.g = p.gt = 1.0f; p.gstep = 0.0f;
  p.dw = 0; p.dlp = 0.0f; p.dlvl = 0.0f;
}

static void player_stop(Player& p) {
  // qui il lettore e' gia' muto (guadagno a zero, o brano finito senza voci accese): le voci si spengono e basta
  for (int k = 0; k < NPV; ++k) p.v[k].on = 0;
  p.on = 0;
  if (p.pending > 0) player_start(p, p.pending, 0);
}

static void player_fade(Player& p, float seconds) {
  if (!p.on) return;
  p.gt = 0.0f;
  const float st = p.g * (float)BLK / (maxf(seconds, 0.01f) * G.fs);
  if (p.gstep <= 0.0f || st > p.gstep) p.gstep = st;
  if (p.wait > 0) p.g = 0.0f;   // non ancora partito: si ferma subito
}

// Un blocco di un lettore, sommato nel bus della musica. Restituisce le voci attive.
static int player_block(Player& p, float* ml, float* mr) {
  if (!p.on) return 0;
  if (p.wait > 0) {
    p.wait--;
    if (p.g <= 0.0f && p.gt <= 0.0f) player_stop(p);
    return 0;
  }
  const Song& s = g_song[p.song];
  if (!p.ending) {
    seq_block(p, s);
    for (int k = 0; k < ND; ++k)   // secondi colpi dei rulli
      if (p.roll[k] > 0 && (p.roll[k] -= BLK) <= 0) { p.roll[k] = 0; drum_hit(p, s, k, p.rollv[k]); }
  }
  float bl[BLK], br[BLK];
  memset(bl, 0, sizeof(bl));
  memset(br, 0, sizeof(br));
  memset(p.send, 0, sizeof(p.send));
  int act = 0;
  for (int k = 0; k < NPV; ++k) {
    Voice& v = p.v[k];
    if (!v.on) continue;
    if (v.gate && v.gate_left > 0 && (v.gate_left -= BLK) <= 0) v.gate = 0;
    if (v.glo != 0.0f || v.tro != 0.0f || v.grn > 0) {   // note legate e scivolate, acciaccature, trilli
      float o = v.glo;
      if (v.grn > 0) { o += v.gro; v.grn--; }
      else if (v.tro != 0.0f) {
        v.trp += v.trr * (float)BLK * G.ifs;
        if (v.trp >= 0.0f && v.trp - (float)(int)v.trp >= 0.5f) o += v.tro;
      }
      v.pitch = fexp2(o);
      v.glo *= v.glk;
      if (fabsf(v.glo) < 2e-4f) v.glo = 0.0f;
      if (v.glo == 0.0f && v.tro == 0.0f && v.grn <= 0) v.pitch = 1.0f;
    }
    const bool alive = voice_block(v);
    voice_mix(v, v.vol, bl, br, p.send);
    if (!alive) voice_off(v);
    else act++;
  }
  // eco a tempo (ottavo puntato), la destra mezzo ritardo dopo; filtrata nel giro di ritorno
  if (s.dly_mix > 0.0f) {
    float* line = p.dly;
    int w = p.dw;
    float lp = p.dlp, lvl = 0.0f;
    const int m = DLY_N - 1, d1 = s.dly1, d2 = s.dly2;
    const float fb = s.dly_fb, mix = s.dly_mix;
    for (int i = 0; i < BLK; ++i) {
      const float a = line[(w - d1) & m], b = line[(w - d2) & m];
      lp += (a - lp) * 0.35f;
      float x = p.send[i] + lp * fb;
      if (fabsf(x) < 1e-20f) x = 0.0f;
      line[w] = x;
      w = (w + 1) & m;
      bl[i] += a * mix;
      br[i] += b * mix;
      lvl = maxf(lvl, fabsf(a));
    }
    p.dw = w;
    p.dlp = fabsf(lp) < 1e-20f ? 0.0f : lp;
    p.dlvl = lvl;
    if (!(lvl < 1e6f)) { memset(p.dly, 0, sizeof(p.dly)); p.dlp = 0.0f; G.st.bad++; }
  }
  // dissolvenza
  const float g0 = p.g;
  float g1 = g0;
  if (p.gstep > 0.0f) {
    g1 = g0 < p.gt ? minf(p.gt, g0 + p.gstep) : maxf(p.gt, g0 - p.gstep);
    if (g1 == p.gt) p.gstep = 0.0f;
  }
  const float dg = (g1 - g0) * (1.0f / BLK), sg = s.gain;
  for (int i = 0; i < BLK; ++i) {
    const float g = (g0 + dg * G.ii[i + 1]) * sg;
    ml[i] += bl[i] * g;
    mr[i] += br[i] * g;
  }
  p.g = g1;
  if ((g1 <= 0.0f && p.gt <= 0.0f) || (p.ending && act == 0 && p.dlvl < 1e-4f)) player_stop(p);
  return act;
}

// --- uscita -------------------------------------------------------------------------------------

static void render_block(void) {
  float ml[BLK], mr[BLK], sl[BLK], sr[BLK];
  memset(ml, 0, sizeof(ml)); memset(mr, 0, sizeof(mr));
  memset(sl, 0, sizeof(sl)); memset(sr, 0, sizeof(sr));
  float duck_t = 1.0f;
  int nact = 0;
  for (int k = 0; k < NSFXV; ++k) {
    Voice& v = G.sv[k];
    if (!v.on) continue;
    const SDef& d = *v.def;
    if ((d.flags & SF_LOOP) && v.gate && (float)(v.age - v.last_trig) > d.keep * G.fs) v.gate = 0;
    const bool alive = voice_block(v);
    float target = v.vol * d.norm;
    if (v.kill) {
      v.kill_g = maxf(0.0f, v.kill_g - (float)BLK / (0.010f * G.fs));
      target *= v.kill_g;
    }
    voice_mix(v, target, sl, sr, 0);
    if (!alive || (v.kill && v.kill_g <= 0.0f)) voice_off(v);
    else if (!v.kill) {
      nact++;
      if (d.duck < duck_t) duck_t = d.duck;
    }
  }
  int mact = 0;
  for (int p = 0; p < NPLAYER; ++p) mact += player_block(g_pl[p], ml, mr);
  G.st.voices = nact;
  if (nact > G.st.voices_peak) G.st.voices_peak = nact;
  G.st.music_voices = mact;

  // rampe dei guadagni: volumi, pausa, abbassamento automatico (veloce a scendere, lento a risalire)
  G.vm += (vcurve(G.vol_m) - G.vm) * G.k_fast;
  G.vs += (vcurve(G.vol_s) - G.vs) * G.k_fast;
  G.duck_g += ((G.duck ? 0.4f : 1.0f) - G.duck_g) * G.k_duck;
  G.auto_g += (duck_t - G.auto_g) * (duck_t < G.auto_g ? G.k_auto_dn : G.k_auto_up);
  const float gm0 = G.gm, gs0 = G.gs, gm1 = G.vm * G.duck_g * G.auto_g, gs1 = G.vs;
  const float dgm = (gm1 - gm0) * (1.0f / BLK), dgs = (gs1 - gs0) * (1.0f / BLK);
  G.gm = gm1; G.gs = gs1;

  // somma e filtro contro la continua
  float xl[BLK], xr[BLK], pk = 0.0f;
  float x0 = G.dc_x[0], y0 = G.dc_y[0], x1 = G.dc_x[1], y1 = G.dc_y[1];
  const float R = G.dc_r;
  for (int i = 0; i < BLK; ++i) {
    const float a = G.ii[i + 1], gm = gm0 + dgm * a, gs = gs0 + dgs * a;
    const float l = ml[i] * gm + sl[i] * gs, r = mr[i] * gm + sr[i] * gs;
    y0 = l - x0 + R * y0; x0 = l;
    y1 = r - x1 + R * y1; x1 = r;
    xl[i] = y0; xr[i] = y1;
    pk = maxf(pk, maxf(fabsf(y0), fabsf(y1)));
  }
  if (!(pk < 1e4f)) {   // non dovrebbe mai succedere: si azzera il blocco e lo stato
    memset(xl, 0, sizeof(xl)); memset(xr, 0, sizeof(xr));
    x0 = y0 = x1 = y1 = 0.0f;
    pk = 0.0f;
    G.st.bad++;
  }
  G.dc_x[0] = x0; G.dc_x[1] = x1;
  G.dc_y[0] = fabsf(y0) < 1e-20f ? 0.0f : y0;
  G.dc_y[1] = fabsf(y1) < 1e-20f ? 0.0f : y1;

  // limitatore: esce il blocco precedente, con un guadagno che conosce anche il blocco nuovo
  const float gnew = pk > LIM_THR ? LIM_THR / pk : 1.0f, gout = G.lim_pk > LIM_THR ? LIM_THR / G.lim_pk : 1.0f;
  const float g0 = G.lim_g;
  const float g1 = minf(minf(g0 + (1.0f - g0) * G.k_rel, gout), gnew);
  const float dg = (g1 - g0) * (1.0f / BLK);
  float opk = 0.0f;
  for (int i = 0; i < BLK; ++i) {
    const float g = g0 + dg * G.ii[i + 1];
    const float l = G.lim_buf[0][i] * g, r = G.lim_buf[1][i] * g;
    opk = maxf(opk, maxf(fabsf(l), fabsf(r)));
    const int il = (int)(l * 32767.0f + (l >= 0.0f ? 0.5f : -0.5f)), ir = (int)(r * 32767.0f + (r >= 0.0f ? 0.5f : -0.5f));
    G.out[2 * i] = (int16_t)(il > 32767 ? 32767 : (il < -32767 ? -32767 : il));
    G.out[2 * i + 1] = (int16_t)(ir > 32767 ? 32767 : (ir < -32767 ? -32767 : ir));
  }
  memcpy(G.lim_buf[0], xl, sizeof(xl));
  memcpy(G.lim_buf[1], xr, sizeof(xr));
  G.lim_pk = pk;
  G.lim_g = g1;
  G.st.blocks++;
  if (g1 < 0.999f || g0 < 0.999f) G.st.limited++;
  G.st.lim_min = minf(G.st.lim_min, g1);
  G.st.peak = maxf(G.st.peak, opk);
  G.now += BLK;
}

void audio_render(int16_t* out, int frames) {
  if (!out || frames <= 0) return;
  if (!G.ready) { memset(out, 0, (size_t)frames * 4u); return; }
  while (frames > 0) {
    if (G.out_pos >= BLK) { render_block(); G.out_pos = 0; }
    int n = BLK - G.out_pos;
    if (n > frames) n = frames;
    memcpy(out, G.out + G.out_pos * 2, (size_t)n * 4u);
    out += n * 2;
    frames -= n;
    G.out_pos += n;
  }
}

// --- controlli ----------------------------------------------------------------------------------

void audio_reset(void) {
  if (!G.ready) return;
  for (int k = 0; k < NSFXV; ++k)
    if (G.sv[k].on) G.sv[k].kill = 1;
  for (int p = 0; p < NPLAYER; ++p) { g_pl[p].pending = -1; player_fade(g_pl[p], 0.06f); }
  G.mus_cur = MUS_NONE;
  G.duck = false;
}

void audio_music(int id) {
  if (!G.ready || id < 0 || id >= NMUSIC || id == G.mus_cur) return;
  if (id != MUS_NONE && g_song[id].nbars <= 0) return;
  G.mus_cur = id;
  bool busy = false;
  for (int p = 0; p < NPLAYER; ++p) {
    Player& pl = g_pl[p];
    pl.pending = -1;
    if (pl.on) { player_fade(pl, id == MUS_NONE ? 0.5f : 0.3f); busy = true; }
  }
  if (id == MUS_NONE) return;
  Player* free_p = 0;
  for (int p = 0; p < NPLAYER && !free_p; ++p)
    if (!g_pl[p].on) free_p = &g_pl[p];
  // il brano nuovo parte quando quello vecchio e' gia' sceso un po' (0,15 s)
  const int wait = busy ? (int)(0.15f * G.fs / BLK) : 0;
  if (free_p) { player_start(*free_p, id, wait); return; }
  Player* q = g_pl[0].g <= g_pl[1].g ? &g_pl[0] : &g_pl[1];
  q->pending = id;   // parte appena il lettore piu' basso ha finito di sfumare
}

void audio_set_volume(int music, int sfx) {
  G.vol_m = music < 0 ? 0 : (music > 10 ? 10 : music);
  G.vol_s = sfx < 0 ? 0 : (sfx > 10 ? 10 : sfx);
}

void audio_duck(bool on) { G.duck = on; }

const char* audio_sound_name(int id) { return id >= 0 && id < NSOUNDS ? SOUND_NAMES[id] : "?"; }
const char* audio_music_name(int id) { return id >= 0 && id < NMUSIC ? MUSIC_NAMES[id] : "?"; }

void audio_get_stats(AudioStats* st) {
  if (!st) return;
  *st = G.st;
  st->parse_errors = G.nerr;
}

void audio_clear_stats(void) {
  const int init = G.st.init_samples;
  memset(&G.st, 0, sizeof(G.st));
  G.st.lim_min = 1.0f;
  G.st.init_samples = init;
}

float audio_music_length(int id, float* loop_start) {
  if (id <= MUS_NONE || id >= NMUSIC || !G.ready) { if (loop_start) *loop_start = -1.0f; return 0.0f; }
  const Song& s = g_song[id];
  if (loop_start) *loop_start = s.loop < 0 ? -1.0f : (float)(s.loop * s.bar_len) * G.ifs;
  return (float)(s.nbars * s.bar_len) * G.ifs;
}

// --- suoni -------------------------------------------------------------------------------------

static SDef* g_def;

static SDef& defsnd(SDef& s, const char* name, float peak_db, int prio, int maxi, float jitter, float merge) {
  memset(&s, 0, sizeof(s));
  s.name = name; s.peak_db = peak_db; s.prio = (uint8_t)prio; s.maxi = (uint8_t)maxi;
  s.jitter = jitter; s.merge = merge; s.duck = 1.0f; s.norm = 1.0f;
  s.step[0] = s.step[1] = s.step[2] = 1.0f;
  g_def = &s;
  return s;
}

static LDef& lay(int wave) {
  SDef& s = *g_def;
  LDef& l = s.L[s.nl < MAXL ? s.nl++ : MAXL - 1];
  memset(&l, 0, sizeof(l));
  l.wave = (uint8_t)wave; l.dc = 2; l.g = 1.0f; l.q = 0.707f; l.pw = 0.5f;
  l.a = 0.002f; l.d = 0.1f; l.r = 0.02f; l.f0 = l.f1 = 440.0f; l.c0 = l.c1 = 1000.0f;
  return l;
}

static void finish(SDef& s) {
  for (int i = 0; i < s.nl; ++i) {
    LDef& l = s.L[i];
    l.a = maxf(l.a, 0.001f);
    l.d = maxf(l.d, 0.005f);
    l.r = maxf(l.r, 0.012f);
    l.len = l.a + l.h + l.d;
    l.ia = 1.0f / l.a; l.idd = 1.0f / l.d; l.ir = 1.0f / l.r;
    l.ipt = 1.0f / (l.pt > 0.0f ? l.pt : l.len);
    l.ict = 1.0f / (l.ct > 0.0f ? l.ct : l.len);
    l.lf = (l.f0 > 0.0f && l.f1 > 0.0f) ? log2f(l.f1 / l.f0) : 0.0f;
    l.lc = (l.c0 > 0.0f && l.c1 > 0.0f) ? log2f(l.c1 / l.c0) : 0.0f;
    l.k = 1.0f / maxf(l.q, 0.3f);
    if (l.wave == W_FM) { l.m0 *= 0.15915494f; l.m1 *= 0.15915494f; }   // indice in giri
    if (l.wave == W_GRAIN) {
      l.gdec = expf(-1.0f / (maxf(l.m0, 0.005f) * G.fs));
      l.gatk = 1.0f / (0.0015f * G.fs);
      l.lf = 0.0f;
    }
    if (l.wave == W_NOISE) l.lf = 0.0f;
  }
}

static void define_sounds(void) {
  SDef* S = g_snd;
  const float D5 = 587.33f, FS5 = 739.99f, A5 = 880.0f, D6 = 1174.66f, A4 = 440.0f, FS4 = 369.99f, D4 = 293.66f;

  // --- giocatori: colpi normali (frequenti: bassi e corti) ---
  // ASTRO, il fucilone: raffiche di tre colpi a 4 fotogrammi l'uno dall'altro: colpo d'energia corto e pieno
  defsnd(S[SND_BULLET], "blaster", -15.0f, PR_LOW, 4, 0.04f, 0.025f);
  lay(W_PULSE).width(0.3f).env(0.001f, 0.005f, 0.036f, 2).hz(900.0f, 230.0f, 0.04f, P_FAST).lp(3600.0f, 900.0f, 1.1f, 0.036f)
      .gain(0.7f);
  lay(W_SINE).env(0.001f, 0.008f, 0.032f, 2).hz(190.0f, 75.0f, 0.036f, P_OUT).gain(0.85f);

  // MOCHI, cinque schegge di cristallo di luna: un "tin", altri tin a caso (granelli: ogni lancio suona un po' diverso) e
  // un fruscio
  defsnd(S[SND_SHARD], "cristalli", -15.0f, PR_LOW, 3, 0.05f, 0.03f);
  lay(W_NOISE).env(0.008f, 0.0f, 0.08f, 2).bp(1400.0f, 3000.0f, 1.5f, 0.08f).gain(0.6f);
  lay(W_SINE).env(0.001f, 0.0f, 0.08f, 3).hz(2350.0f).gain(0.35f);
  lay(W_GRAIN).at(0.012f).hz(2400.0f, 3400.0f).grain(70.0f, 0.03f).env(0.002f, 0.04f, 0.045f, 2).gain(0.3f);

  // MARZIO, tre palle di ghiaccio secco: sbuffo freddo, un piccolo tonfo, sfrigolio di brina
  defsnd(S[SND_FROST], "ghiaccio_secco", -15.0f, PR_LOW, 3, 0.05f, 0.03f);
  lay(W_NOISE).env(0.006f, 0.01f, 0.065f, 2).lp(5000.0f, 1600.0f, 0.8f, 0.06f).gain(0.75f);
  lay(W_SINE).env(0.002f, 0.005f, 0.06f, 2).hz(320.0f, 150.0f, 0.06f, P_OUT).gain(0.4f);
  lay(W_CRACK).hz(500.0f).env(0.003f, 0.01f, 0.06f, 2).bp(3400.0f, 3000.0f, 1.5f).gain(0.8f);

  // fiamma: una voce sola che resta accesa finche' arrivano richieste (ogni 4-6 fotogrammi)
  defsnd(S[SND_FLAME], "fiamma", -17.0f, PR_LOW, 3, 0.0f, 0.0f);
  S[SND_FLAME].flags = SF_LOOP; S[SND_FLAME].keep = 0.13f;
  lay(W_NOISE).env(0.03f, 0.0f, 0.05f, 2).sus(1.0f, 0.16f).lp(1400.0f, 1400.0f, 0.6f);
  lay(W_NOISE).env(0.04f, 0.0f, 0.05f, 2).sus(1.0f, 0.18f).bp(520.0f, 520.0f, 0.7f).gain(1.3f);
  lay(W_CRACK).hz(45.0f).env(0.02f, 0.0f, 0.05f, 2).sus(1.0f, 0.12f).bp(2600.0f, 2600.0f, 1.5f).gain(2.2f);
  lay(W_NOISE).opt(LF_ONESHOT).env(0.01f, 0.02f, 0.14f, 2).bp(500.0f, 1600.0f, 1.2f, 0.12f).gain(0.9f);

  // --- giocatori: speciali ---
  defsnd(S[SND_LAVA], "lava", -14.0f, PR_MID, 4, 0.06f, 0.03f);
  lay(W_SINE).env(0.003f, 0.03f, 0.05f, 2).hz(170.0f, 520.0f, 0.07f, P_OUT).gain(0.9f);
  lay(W_SINE).at(0.07f).env(0.004f, 0.01f, 0.12f, 2).hz(500.0f, 240.0f, 0.12f, P_OUT).gain(0.8f);
  lay(W_FM).env(0.003f, 0.03f, 0.12f, 2).hz(300.0f, 200.0f).fm(0.5f, 2.5f, 0.2f).gain(0.25f);
  lay(W_NOISE).env(0.002f, 0.0f, 0.04f, 2).lp(1500.0f, 800.0f).gain(0.2f);

  // ASTRO, tre mini-razzi: accensione, tre fruscii che partono uno dopo l'altro, lo sfrigolio dei motori
  defsnd(S[SND_ROCKET], "razzi", -12.0f, PR_MID, 2, 0.04f, 0.05f);
  lay(W_SINE).env(0.002f, 0.01f, 0.09f, 2).hz(140.0f, 55.0f, 0.06f, P_OUT).gain(0.6f);
  lay(W_NOISE).env(0.001f, 0.01f, 0.07f, 2).lp(3500.0f, 700.0f, 0.8f, 0.06f).gain(0.8f);
  for (int k = 0; k < 3; ++k)
    lay(W_NOISE).at(0.02f + 0.05f * (float)k).env(0.03f, 0.02f, 0.25f, 2)
        .bp(700.0f + 60.0f * (float)k, 2600.0f - 150.0f * (float)k, 2.2f, 0.25f).ccurve(P_OUT).gain(1.4f);
  lay(W_CRACK).hz(220.0f, 90.0f).env(0.01f, 0.1f, 0.25f, 2).bp(2400.0f, 1500.0f, 1.2f).gain(1.2f);

  // MOCHI, il balzo lunare: una molla che sale, un fruscio e un "tin" in cima
  defsnd(S[SND_LEAP], "balzo_lunare", -11.0f, PR_MID, 2, 0.02f, 0.05f);
  lay(W_TRI).env(0.004f, 0.1f, 0.22f, 2).hz(180.0f, 760.0f, 0.3f, P_IN).vib(11.0f, 1.5f).opt(LF_VIBENV);
  lay(W_SINE).env(0.004f, 0.1f, 0.22f, 2).hz(360.0f, 1520.0f, 0.3f, P_IN).vib(11.0f, 1.5f).opt(LF_VIBENV).gain(0.25f);
  lay(W_NOISE).env(0.05f, 0.05f, 0.2f, 2).bp(500.0f, 2500.0f, 1.4f, 0.3f).gain(0.35f);
  lay(W_FM).at(0.22f).env(0.002f, 0.0f, 0.2f, 3).hz(1760.0f).fm(2.0f, 0.6f, 0.0f).gain(0.25f);

  defsnd(S[SND_SLAM], "schianto", -8.0f, PR_HIGH, 2, 0.03f, 0.05f);
  lay(W_SINE).env(0.001f, 0.02f, 0.36f, 2).hz(140.0f, 40.0f, 0.12f, P_FAST).gain(0.55f);
  lay(W_NOISE).env(0.001f, 0.005f, 0.13f, 2).lp(3500.0f, 700.0f, 0.8f, 0.1f).gain(1.4f);
  lay(W_CRACK).hz(320.0f, 120.0f).env(0.002f, 0.03f, 0.24f, 2).bp(2400.0f, 1800.0f, 1.2f).gain(2.6f);
  lay(W_SNOISE).hz(130.0f, 80.0f).env(0.005f, 0.05f, 0.34f, 2).gain(0.25f);
  lay(W_SAW).env(0.001f, 0.02f, 0.3f, 2).hz(130.0f, 50.0f, 0.15f, P_OUT).lp(1000.0f, 300.0f, 1.0f, 0.25f).gain(0.4f);
  lay(W_NOISE).env(0.002f, 0.04f, 0.25f, 2).bp(420.0f, 300.0f, 0.8f).gain(1.4f);
  lay(W_FM).at(0.03f).env(0.002f, 0.0f, 0.3f, 3).hz(1567.98f).fm(2.76f, 0.6f, 0.0f).gain(0.2f);   // l'anello di schegge

  // MARZIO, la tempesta di sabbia gelata attorno a lui: raffiche di vento (il filtro segue l'inviluppo) e granelli di
  // ghiaccio
  defsnd(S[SND_BLIZZARD], "tempesta_gelata", -14.0f, PR_MID, 2, 0.0f, 0.1f);
  lay(W_NOISE).env(0.35f, 0.3f, 0.9f, 2).bp(450.0f, 1500.0f, 3.5f).opt(LF_CENV).gain(1.4f);
  lay(W_NOISE).at(0.15f).env(0.5f, 0.2f, 0.8f, 2).bp(900.0f, 2600.0f, 5.0f).opt(LF_CENV).gain(0.6f);
  lay(W_GRAIN).hz(1800.0f, 4200.0f).grain(60.0f, 0.012f).env(0.2f, 0.5f, 0.6f, 2).gain(0.2f);

  defsnd(S[SND_ERUPT], "eruzione", -10.0f, PR_HIGH, 2, 0.02f, 0.1f);
  lay(W_SNOISE).hz(95.0f, 60.0f).env(0.04f, 0.15f, 0.4f, 2).gain(0.45f);
  lay(W_SINE).env(0.02f, 0.1f, 0.45f, 2).hz(70.0f, 45.0f).gain(0.3f);
  lay(W_NOISE).env(0.03f, 0.12f, 0.4f, 2).bp(420.0f, 300.0f, 0.8f).gain(1.8f);
  lay(W_CRACK).hz(70.0f, 40.0f).env(0.05f, 0.15f, 0.35f, 2).bp(1400.0f, 1000.0f, 1.2f).gain(2.5f);
  lay(W_SAW).env(0.03f, 0.15f, 0.4f, 2).hz(85.0f, 60.0f).lp(800.0f, 500.0f, 1.2f).vib(7.0f, 0.5f).gain(0.4f);

  defsnd(S[SND_SPECIAL_READY], "speciale_pronto", -11.0f, PR_HIGH, 2, 0.0f, 0.1f);
  lay(W_FM).env(0.002f, 0.01f, 0.16f, 3).hz(A5).fm(2.0f, 1.2f, 0.0f);
  lay(W_FM).at(0.09f).env(0.002f, 0.01f, 0.2f, 3).hz(D6).fm(2.0f, 1.2f, 0.0f);

  // --- giocatori: movimento, bombe, vita ---
  defsnd(S[SND_JUMP], "salto", -16.0f, PR_LOW, 3, 0.04f, 0.03f);
  lay(W_PULSE).width(0.5f).env(0.003f, 0.02f, 0.09f, 2).hz(260.0f, 560.0f, 0.08f, P_OUT).lp(1800.0f, 1800.0f, 0.7f).gain(0.55f);
  lay(W_SINE).env(0.003f, 0.02f, 0.09f, 2).hz(260.0f, 560.0f, 0.08f, P_OUT).gain(0.6f);

  defsnd(S[SND_LAND], "atterraggio", -22.0f, PR_LOW, 3, 0.05f, 0.03f);
  lay(W_SINE).env(0.001f, 0.005f, 0.05f, 3).hz(150.0f, 70.0f, 0.04f, P_OUT);
  lay(W_NOISE).env(0.001f, 0.0f, 0.03f, 2).lp(900.0f, 400.0f).gain(0.9f);

  defsnd(S[SND_TRAMP], "trampolino", -10.0f, PR_MID, 3, 0.03f, 0.05f);
  lay(W_TRI).env(0.002f, 0.05f, 0.38f, 2).hz(160.0f, 520.0f, 0.22f, P_OUT).vib(13.0f, 1.3f).opt(LF_VIBENV);
  lay(W_FM).env(0.002f, 0.02f, 0.25f, 2).hz(160.0f, 520.0f, 0.22f, P_OUT).fm(3.01f, 1.6f, 0.0f).gain(0.35f);
  lay(W_SINE).env(0.001f, 0.0f, 0.06f, 2).hz(120.0f, 70.0f).gain(0.5f);

  defsnd(S[SND_BOMB_DROP], "bomba_giu", -13.0f, PR_MID, 3, 0.03f, 0.05f);
  lay(W_FM).env(0.001f, 0.005f, 0.12f, 3).hz(330.0f, 300.0f).fm(1.41f, 3.0f, 0.5f).gain(0.9f);
  lay(W_SINE).env(0.001f, 0.005f, 0.08f, 2).hz(180.0f, 90.0f, 0.06f, P_OUT).gain(0.6f);
  lay(W_NOISE).at(0.03f).env(0.02f, 0.1f, 0.35f, 2).bp(4000.0f, 3500.0f, 1.5f).gain(0.2f);
  lay(W_CRACK).at(0.03f).hz(50.0f, 30.0f).env(0.02f, 0.1f, 0.35f, 2).bp(4200.0f, 4200.0f, 2.0f).gain(0.6f);

  defsnd(S[SND_EXPLODE], "esplosione", -6.0f, PR_TOP, 3, 0.04f, 0.05f);
  lay(W_NOISE).env(0.001f, 0.02f, 0.55f, 2).lp(5000.0f, 350.0f, 0.8f, 0.45f).ccurve(P_OUT).gain(1.6f);
  lay(W_SINE).env(0.002f, 0.03f, 0.6f, 2).hz(110.0f, 35.0f, 0.35f, P_OUT).gain(0.45f);
  lay(W_SNOISE).hz(70.0f, 30.0f).env(0.03f, 0.1f, 0.75f, 2).gain(0.3f);
  lay(W_CRACK).at(0.04f).hz(140.0f, 30.0f).env(0.01f, 0.05f, 0.5f, 2).bp(1800.0f, 900.0f, 1.0f).gain(2.2f);
  lay(W_SAW).env(0.002f, 0.03f, 0.5f, 2).hz(120.0f, 40.0f, 0.4f, P_OUT).lp(1200.0f, 300.0f, 1.0f, 0.4f).gain(0.45f);
  lay(W_NOISE).env(0.002f, 0.08f, 0.6f, 2).bp(400.0f, 250.0f, 0.8f).gain(1.6f);

  defsnd(S[SND_EXPLODE_SMALL], "esplosione_piccola", -11.0f, PR_HIGH, 3, 0.05f, 0.04f);
  lay(W_NOISE).env(0.001f, 0.01f, 0.26f, 2).lp(3500.0f, 450.0f, 0.8f).ccurve(P_OUT).gain(1.5f);
  lay(W_SINE).env(0.002f, 0.01f, 0.26f, 2).hz(160.0f, 55.0f, 0.2f, P_OUT).gain(0.45f);
  lay(W_SNOISE).hz(90.0f, 50.0f).env(0.01f, 0.05f, 0.28f, 2).gain(0.25f);
  lay(W_NOISE).env(0.002f, 0.04f, 0.25f, 2).bp(450.0f, 300.0f, 0.8f).gain(1.2f);

  defsnd(S[SND_PICK_TALISMAN], "talismano", -11.0f, PR_HIGH, 2, 0.0f, 0.08f);
  {
    const float f[4] = { D5, FS5, A5, D6 };
    for (int k = 0; k < 4; ++k) lay(W_FM).at(0.06f * (float)k).env(0.002f, 0.01f, 0.17f, 3).hz(f[k]).fm(2.0f, 1.0f, 0.0f)
        .gain(0.8f);
  }
  lay(W_GRAIN).hz(3000.0f, 6000.0f).grain(35.0f, 0.035f).env(0.01f, 0.2f, 0.12f, 2).gain(0.18f);

  defsnd(S[SND_PICK_BOMB], "bomba_presa", -13.0f, PR_MID, 2, 0.02f, 0.05f);
  lay(W_NOISE).env(0.001f, 0.005f, 0.04f, 2).bp(900.0f, 700.0f, 2.0f).gain(0.9f);
  lay(W_SINE).env(0.001f, 0.005f, 0.06f, 2).hz(230.0f, 150.0f, 0.05f, P_OUT).gain(0.7f);
  lay(W_PULSE).at(0.07f).width(0.5f).env(0.001f, 0.0f, 0.012f, 2).hz(1900.0f).lp(4000.0f, 4000.0f).gain(0.4f);
  lay(W_NOISE).at(0.07f).env(0.001f, 0.0f, 0.01f, 2).bp(3000.0f, 3000.0f, 1.5f).gain(0.5f);

  defsnd(S[SND_HURT], "colpito", -11.0f, PR_HIGH, 2, 0.03f, 0.06f);
  lay(W_PULSE).width(0.35f).env(0.002f, 0.02f, 0.12f, 2).hz(400.0f, 190.0f, 0.12f, P_OUT).lp(2200.0f, 900.0f, 1.5f);
  lay(W_SINE).env(0.002f, 0.02f, 0.12f, 2).hz(200.0f, 95.0f, 0.12f, P_OUT).gain(0.5f);

  defsnd(S[SND_DOWN], "a_terra", -10.0f, PR_TOP, 2, 0.0f, 0.1f);
  lay(W_SAW).env(0.025f, 0.08f, 0.07f, 2).hz(466.16f).lp(350.0f, 2200.0f, 2.5f).opt(LF_CENV);
  lay(W_SAW).at(0.19f).env(0.025f, 0.08f, 0.07f, 2).hz(440.0f).lp(350.0f, 2200.0f, 2.5f).opt(LF_CENV);
  lay(W_SAW).at(0.38f).env(0.03f, 0.15f, 0.16f, 2).hz(415.3f, 392.0f, 0.3f, P_IN).vib(6.0f, 0.45f, 0.06f)
      .lp(350.0f, 2200.0f, 2.5f).opt(LF_CENV);

  defsnd(S[SND_REVIVE], "rientro", -11.0f, PR_HIGH, 2, 0.0f, 0.1f);
  lay(W_SINE).env(0.01f, 0.1f, 0.15f, 2).hz(300.0f, 1200.0f, 0.25f, P_IN).vib(7.0f, 0.3f).gain(0.6f);
  lay(W_FM).at(0.2f).env(0.002f, 0.02f, 0.2f, 3).hz(D6).fm(3.0f, 1.5f, 0.0f).gain(0.8f);
  lay(W_FM).at(0.2f).env(0.002f, 0.02f, 0.2f, 3).hz(A5).fm(2.0f, 1.0f, 0.0f).gain(0.5f);
  lay(W_GRAIN).at(0.15f).hz(3000.0f, 5500.0f).grain(30.0f, 0.04f).env(0.01f, 0.15f, 0.1f, 2).gain(0.15f);

  // --- mostri ---
  defsnd(S[SND_HIT], "colpo", -18.0f, PR_LOW, 4, 0.06f, 0.03f);
  lay(W_SINE).env(0.001f, 0.0f, 0.04f, 3).hz(560.0f, 400.0f, 0.015f, P_FAST);
  lay(W_NOISE).env(0.001f, 0.0f, 0.012f, 2).bp(1600.0f, 1400.0f, 1.5f).gain(0.4f);

  // mostro sconfitto: "pop" che scende, sbuffo di fumo e due scintille
  defsnd(S[SND_DEFEAT], "sconfitto", -15.0f, PR_MID, 3, 0.03f, 0.04f);
  lay(W_SINE).env(0.001f, 0.005f, 0.055f, 2).hz(1100.0f, 200.0f, 0.04f, P_FAST).gain(0.9f);
  lay(W_NOISE).env(0.01f, 0.02f, 0.18f, 2).lp(2400.0f, 500.0f, 0.7f, 0.17f).ccurve(P_OUT).gain(0.9f);
  lay(W_FM).at(0.04f).env(0.002f, 0.0f, 0.12f, 3).hz(1174.66f).fm(2.0f, 0.6f, 0.0f).gain(0.25f);
  lay(W_FM).at(0.09f).env(0.002f, 0.0f, 0.15f, 3).hz(1567.98f).fm(2.0f, 0.6f, 0.0f).gain(0.2f);

  // scorpione (0.5.1, al posto dello sciacallo): si ferma e alza la coda prima della carica. Due schiocchi secchi
  // delle chele (chitina: gli spigoli di un'onda stretta a 20 Hz, uno ogni 50 ms, nel filtro a banda), un sibilo
  // raspato e il trillo della coda. Secco e corto, nessuna nota: con 3-4 scorpioni non diventa una cantilena (e il
  // jitter cambia un po' ogni copia). Quattro strati come lo sciacallo: la taratura degli altri suoni non cambia
  defsnd(S[SND_SCORPION], "scorpione", -12.0f, PR_MID, 2, 0.05f, 0.08f);
  lay(W_PULSE).width(0.06f).hz(20.0f).env(0.001f, 0.06f, 0.01f, 2).bp(2400.0f, 2400.0f, 3.0f).gain(1.0f);
  lay(W_SNOISE).at(0.07f).hz(8000.0f).env(0.05f, 0.06f, 0.12f, 2).bp(2000.0f, 3000.0f, 1.6f, 0.2f).gain(0.55f);
  lay(W_CRACK).at(0.07f).hz(300.0f).env(0.05f, 0.06f, 0.12f, 2).bp(1700.0f, 1700.0f, 2.2f).gain(0.8f);
  lay(W_PULSE).at(0.2f).width(0.12f).hz(44.0f, 36.0f).env(0.005f, 0.13f, 0.08f, 2).bp(2300.0f, 2300.0f, 4.0f).gain(1.4f);

  // arciere di terracotta (0.5.1, al posto del lama): l'arco che scricchiola (corto: il suono parte quando la
  // freccia esce), lo schiocco e la corda che vibra, i flettenti, una freccia lenta che fischia piano
  defsnd(S[SND_BOW], "arciere", -12.0f, PR_MID, 3, 0.04f, 0.05f);
  lay(W_CRACK).hz(150.0f, 320.0f).env(0.008f, 0.022f, 0.012f, 2).bp(900.0f, 1300.0f, 3.0f).gain(4.5f);
  lay(W_NOISE).at(0.04f).env(0.001f, 0.0f, 0.01f, 2).bp(1500.0f, 1500.0f, 1.5f).gain(0.8f);
  lay(W_SAW).at(0.04f).env(0.001f, 0.0f, 0.16f, 2).hz(262.0f, 247.0f, 0.12f).lp(3500.0f, 700.0f, 1.6f, 0.12f).vib(19.0f, 0.5f)
      .opt(LF_VIBENV).gain(0.75f);
  lay(W_SINE).at(0.04f).env(0.001f, 0.0f, 0.035f, 2).hz(320.0f, 160.0f, 0.025f, P_FAST).gain(0.4f);
  lay(W_NOISE).at(0.05f).env(0.03f, 0.02f, 0.12f, 2).bp(2800.0f, 2000.0f, 6.0f, 0.17f).gain(1.6f);

  // la freccia dell'arciere che si pianta a terra vicino a un giocatore: colpo sordo leggero, la punta nella terra,
  // l'asta che vibra appena e un filo di polvere
  defsnd(S[SND_ARROW_LAND], "freccia_a_terra", -15.0f, PR_LOW, 3, 0.05f, 0.04f);
  lay(W_SINE).env(0.001f, 0.005f, 0.07f, 2).hz(340.0f, 150.0f, 0.04f, P_OUT).gain(0.8f);
  lay(W_SNOISE).hz(4000.0f).env(0.001f, 0.0f, 0.035f, 2).lp(2200.0f, 600.0f, 0.8f, 0.035f).gain(0.9f);
  lay(W_TRI).at(0.006f).env(0.002f, 0.0f, 0.13f, 2).hz(440.0f, 410.0f, 0.12f).vib(26.0f, 1.0f).opt(LF_VIBENV).gain(0.22f);
  lay(W_SNOISE).at(0.012f).hz(5000.0f).env(0.02f, 0.03f, 0.14f, 2).bp(1400.0f, 800.0f, 0.9f, 0.16f).gain(0.45f);

  // scarabei: ronzio delle ali e il morso
  defsnd(S[SND_SCARAB], "scarabei", -14.0f, PR_MID, 3, 0.06f, 0.05f);
  lay(W_PULSE).width(0.18f).env(0.012f, 0.12f, 0.08f, 2).hz(150.0f, 175.0f, 0.15f, P_OUT).vib(32.0f, 0.9f)
      .bp(1000.0f, 1000.0f, 2.0f);
  lay(W_SAW).env(0.012f, 0.12f, 0.08f, 2).hz(300.0f, 350.0f, 0.15f, P_OUT).vib(32.0f, 0.9f).bp(2100.0f, 2100.0f, 3.0f)
      .gain(0.35f);
  lay(W_NOISE).at(0.17f).env(0.001f, 0.0f, 0.015f, 2).bp(2400.0f, 2400.0f, 2.0f).gain(0.5f);

  // guerriero di terracotta: la terracotta stride e lui grugnisce mentre alza l'alabarda
  defsnd(S[SND_WARRIOR], "guerriero", -10.0f, PR_HIGH, 2, 0.03f, 0.1f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.02f, 2).bp(1500.0f, 1500.0f, 2.0f).gain(0.5f);
  lay(W_CRACK).hz(380.0f, 220.0f).env(0.08f, 0.25f, 0.15f, 2).bp(900.0f, 700.0f, 1.2f).gain(2.2f);
  lay(W_NOISE).env(0.08f, 0.25f, 0.15f, 2).lp(500.0f, 400.0f).gain(0.8f);
  lay(W_FM).at(0.05f).env(0.05f, 0.2f, 0.2f, 2).hz(88.0f, 72.0f).fm(1.0f, 5.0f, 3.0f).lp(1300.0f, 900.0f, 1.2f).vib(9.0f, 0.6f)
      .gain(0.7f);
  lay(W_SAW).at(0.05f).env(0.05f, 0.2f, 0.2f, 2).hz(88.0f, 72.0f).bp(450.0f, 450.0f, 2.0f).vib(9.0f, 0.6f).gain(0.6f);

  // gladiatore gigante: ruggito (formanti di una "a"), il corno dell'arena e il pubblico
  defsnd(S[SND_BOSS_ROAR], "gladiatore", -8.0f, PR_TOP, 2, 0.02f, 0.2f);
  S[SND_BOSS_ROAR].duck = 0.75f;
  lay(W_SAW).env(0.12f, 0.5f, 0.5f, 2).hz(92.0f, 70.0f, 1.0f).bp(700.0f, 700.0f, 2.5f).vib(6.0f, 0.7f).gain(1.4f);
  lay(W_SAW).env(0.12f, 0.5f, 0.5f, 2).hz(92.0f, 70.0f, 1.0f).bp(1150.0f, 1150.0f, 3.5f).vib(6.0f, 0.7f).gain(0.7f);
  lay(W_SAW).at(0.25f).env(0.06f, 0.5f, 0.4f, 2).hz(146.83f).lp(300.0f, 1800.0f, 1.0f).opt(LF_CENV).gain(0.5f);
  lay(W_SAW).at(0.25f).env(0.06f, 0.5f, 0.4f, 2).hz(220.0f).lp(300.0f, 1800.0f, 1.0f).opt(LF_CENV).gain(0.35f);
  lay(W_GRAIN).at(0.2f).hz(250.0f, 900.0f).grain(90.0f, 0.06f).env(0.3f, 0.6f, 0.5f, 2).gain(0.35f);
  lay(W_NOISE).at(0.2f).env(0.3f, 0.6f, 0.5f, 2).bp(900.0f, 900.0f, 0.8f).gain(0.4f);

  // giavellotto del gladiatore che arriva a terra (combat.cpp, alla caduta): fischio, colpo sordo, terra e l'asta che
  // vibra
  defsnd(S[SND_JAVELIN], "giavellotto", -10.0f, PR_HIGH, 3, 0.04f, 0.05f);
  lay(W_NOISE).env(0.03f, 0.0f, 0.03f, 2).bp(2600.0f, 700.0f, 2.0f, 0.06f).gain(1.0f);
  lay(W_SINE).at(0.03f).env(0.002f, 0.01f, 0.2f, 2).hz(160.0f, 55.0f, 0.08f, P_OUT).gain(0.8f);
  lay(W_NOISE).at(0.03f).env(0.002f, 0.0f, 0.15f, 2).lp(2000.0f, 400.0f, 0.7f, 0.15f).gain(1.0f);
  lay(W_NOISE).at(0.03f).env(0.001f, 0.0f, 0.03f, 2).bp(900.0f, 900.0f, 3.0f).gain(0.6f);
  lay(W_TRI).at(0.04f).env(0.003f, 0.0f, 0.35f, 2).hz(210.0f, 190.0f, 0.3f).vib(17.0f, 1.0f).opt(LF_VIBENV).gain(0.45f);

  // spadata del gladiatore: fendente, la lama che suona, la botta e la terra che si spacca in linea
  defsnd(S[SND_SLASH], "spadata", -10.0f, PR_HIGH, 2, 0.04f, 0.1f);
  lay(W_NOISE).env(0.05f, 0.0f, 0.04f, 2).bp(800.0f, 3000.0f, 1.5f, 0.09f).ccurve(P_IN).gain(1.0f);
  lay(W_FM).at(0.09f).env(0.001f, 0.0f, 0.35f, 2).hz(680.0f).fm(2.76f, 2.5f, 0.5f).gain(0.35f);
  lay(W_SINE).at(0.09f).env(0.002f, 0.02f, 0.35f, 2).hz(120.0f, 40.0f, 0.15f, P_OUT).gain(0.7f);
  lay(W_CRACK).at(0.09f).hz(420.0f, 140.0f).env(0.005f, 0.15f, 0.45f, 2).bp(1300.0f, 800.0f, 1.0f).gain(2.4f);
  lay(W_NOISE).at(0.09f).env(0.003f, 0.05f, 0.35f, 2).bp(450.0f, 300.0f, 0.8f).gain(1.2f);
  lay(W_CRACK).at(0.75f).hz(160.0f, 80.0f).env(0.05f, 0.3f, 0.4f, 2).bp(1100.0f, 1100.0f, 1.0f).gain(1.5f);

  // 0.5.1, il gladiatore cammina e attacca. Passo: un tonfo (quasi tutto sopra i 190 Hz, che l'altoparlantino
  // suona, con un po' di grave sotto) e la sabbia dell'arena che scricchiola. Corto (0,13 s) e leggero: torna ogni
  // 0,4-0,5 s mentre cammina e ogni 0,15 s nella corsa della carica, senza mai accavallarsi; il jitter cambia ogni passo
  defsnd(S[SND_BOSS_STEP], "passo_gladiatore", -16.0f, PR_LOW, 2, 0.06f, 0.04f);
  lay(W_SINE).env(0.002f, 0.01f, 0.12f, 2).hz(110.0f, 55.0f, 0.08f, P_OUT).gain(0.3f);
  lay(W_SINE).env(0.002f, 0.01f, 0.08f, 2).hz(380.0f, 190.0f, 0.06f, P_OUT).gain(0.75f);
  lay(W_SNOISE).hz(4000.0f).env(0.003f, 0.01f, 0.07f, 2).lp(2200.0f, 600.0f, 0.8f, 0.07f).gain(1.0f);

  // il gladiatore alza la spada per il fendente: la lama che striscia e "canta" salendo un poco. Finisce in 0,38 s,
  // prima del colpo anche con la preparazione piu' corta (0,4 s nella terza fase)
  defsnd(S[SND_BOSS_SWEEP_WARN], "spada_alzata", -12.0f, PR_HIGH, 2, 0.02f, 0.1f);
  lay(W_SNOISE).hz(9000.0f).env(0.02f, 0.02f, 0.07f, 2).bp(2600.0f, 4200.0f, 2.5f, 0.1f).gain(0.8f);
  lay(W_FM).at(0.03f).env(0.003f, 0.05f, 0.3f, 2).hz(1240.0f, 1300.0f, 0.2f, P_OUT).fm(1.41f, 0.9f, 0.1f).gain(0.5f);
  lay(W_SINE).at(0.03f).env(0.003f, 0.03f, 0.22f, 2).hz(3170.0f, 3260.0f, 0.2f, P_OUT).gain(0.12f);

  // il fendente (al colpo): l'aria spostata dalla spada enorme che sale e scende (come se passasse davanti), un
  // "vum" grave e la lama che risuona dopo
  defsnd(S[SND_BOSS_SWEEP], "fendente", -10.0f, PR_HIGH, 2, 0.03f, 0.1f);
  lay(W_NOISE).env(0.04f, 0.0f, 0.04f, 2).bp(600.0f, 2600.0f, 1.6f, 0.08f).ccurve(P_IN).gain(1.0f);
  lay(W_NOISE).at(0.06f).env(0.005f, 0.02f, 0.14f, 2).bp(2400.0f, 700.0f, 1.6f, 0.16f).ccurve(P_OUT).gain(0.9f);
  lay(W_SINE).at(0.02f).env(0.03f, 0.02f, 0.12f, 2).hz(170.0f, 95.0f, 0.15f, P_OUT).gain(0.5f);
  lay(W_FM).at(0.08f).env(0.002f, 0.0f, 0.45f, 2).hz(720.0f).fm(2.76f, 1.4f, 0.1f).gain(0.25f);
  lay(W_SINE).at(0.08f).env(0.002f, 0.0f, 0.35f, 2).hz(1880.0f).gain(0.08f);

  // la preparazione della carica con lo scudo: due colpi di spada sullo scudo di bronzo, il grugnito e il pubblico
  // che sale. I colpi e il grugnito stanno nella preparazione (0,58-0,8 s secondo la fase), il pubblico sfuma
  // durante la corsa
  defsnd(S[SND_BOSS_RUSH], "carica_scudo", -9.0f, PR_TOP, 1, 0.02f, 0.2f);
  S[SND_BOSS_RUSH].duck = 0.85f;
  for (int k = 0; k < 2; ++k) {
    lay(W_FM).at(0.22f * (float)k).env(0.001f, 0.0f, 0.4f, 3).hz(232.0f).fm(1.41f, 3.0f, 0.4f).gain(0.6f);
    lay(W_NOISE).at(0.22f * (float)k).env(0.001f, 0.0f, 0.03f, 2).bp(1500.0f, 1100.0f, 1.2f).gain(0.9f);
  }
  lay(W_SINE).env(0.002f, 0.01f, 0.15f, 2).hz(150.0f, 70.0f, 0.1f, P_OUT).gain(0.5f);
  lay(W_SAW).at(0.3f).env(0.04f, 0.15f, 0.15f, 2).hz(98.0f, 84.0f).bp(650.0f, 650.0f, 2.5f).vib(8.0f, 0.6f).gain(1.2f);
  lay(W_SAW).at(0.3f).env(0.04f, 0.15f, 0.15f, 2).hz(98.0f, 84.0f).bp(1100.0f, 1100.0f, 3.5f).vib(8.0f, 0.6f).gain(0.6f);
  lay(W_GRAIN).at(0.2f).hz(250.0f, 900.0f).grain(90.0f, 0.06f).env(0.35f, 0.25f, 0.5f, 2).gain(0.3f);
  lay(W_NOISE).at(0.2f).env(0.35f, 0.25f, 0.5f, 2).bp(900.0f, 900.0f, 0.8f).gain(0.35f);

  // la carica finisce contro un muro: lo scudo suona come una campana ("bong", e ondeggia: il gladiatore e'
  // stordito), il tonfo e i sassi che cadono
  defsnd(S[SND_BOSS_IMPACT], "scudo_contro_muro", -8.0f, PR_TOP, 1, 0.02f, 0.2f);
  S[SND_BOSS_IMPACT].duck = 0.85f;
  lay(W_FM).env(0.001f, 0.0f, 0.9f, 2).hz(165.0f).fm(1.41f, 3.5f, 0.3f).vib(4.5f, 0.35f, 0.25f).opt(LF_VIBENV).gain(0.6f);
  lay(W_SINE).env(0.001f, 0.0f, 0.7f, 2).hz(330.0f, 326.0f, 0.7f).vib(4.5f, 0.35f, 0.25f).opt(LF_VIBENV).gain(0.25f);
  lay(W_SINE).env(0.002f, 0.02f, 0.3f, 2).hz(110.0f, 40.0f, 0.15f, P_OUT).gain(0.7f);
  lay(W_SINE).env(0.002f, 0.0f, 0.12f, 2).hz(260.0f, 120.0f, 0.08f, P_OUT).gain(0.5f);
  lay(W_NOISE).env(0.001f, 0.01f, 0.12f, 2).lp(3000.0f, 600.0f, 0.8f, 0.12f).gain(1.2f);
  lay(W_CRACK).at(0.03f).hz(260.0f, 60.0f).env(0.01f, 0.08f, 0.4f, 2).bp(1300.0f, 900.0f, 1.2f).gain(1.8f);

  // --- mondo ---
  defsnd(S[SND_CRATE_HIT], "cassa_colpita", -15.0f, PR_LOW, 3, 0.05f, 0.03f);
  lay(W_SINE).env(0.001f, 0.0f, 0.07f, 3).hz(360.0f, 300.0f, 0.03f, P_OUT);
  lay(W_SINE).env(0.001f, 0.0f, 0.04f, 3).hz(830.0f, 700.0f, 0.03f, P_OUT).gain(0.35f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.01f, 2).bp(2000.0f, 1800.0f, 1.5f).gain(0.35f);

  defsnd(S[SND_CRATE_BREAK], "cassa_rotta", -11.0f, PR_MID, 3, 0.05f, 0.04f);
  lay(W_NOISE).env(0.001f, 0.01f, 0.12f, 2).bp(1500.0f, 900.0f, 1.0f).gain(0.8f);
  lay(W_CRACK).hz(400.0f, 150.0f).env(0.002f, 0.03f, 0.25f, 2).bp(2200.0f, 1600.0f, 1.5f).gain(1.5f);
  lay(W_SINE).env(0.001f, 0.0f, 0.09f, 3).hz(260.0f, 180.0f, 0.05f, P_OUT).gain(0.7f);
  lay(W_CRACK).at(0.08f).hz(90.0f, 40.0f).env(0.01f, 0.03f, 0.18f, 2).bp(3000.0f, 2500.0f, 1.5f).gain(1.0f);

  // capsula dell'esploratore colpita: metallo cavo
  defsnd(S[SND_CAPSULE_HIT], "capsula", -13.0f, PR_MID, 3, 0.03f, 0.04f);
  lay(W_FM).env(0.001f, 0.0f, 0.16f, 3).hz(560.0f).fm(2.76f, 2.0f, 0.3f).gain(0.8f);
  lay(W_FM).env(0.001f, 0.0f, 0.1f, 3).hz(1270.0f).fm(1.41f, 1.5f, 0.2f).gain(0.35f);
  lay(W_SINE).env(0.001f, 0.0f, 0.12f, 3).hz(310.0f, 290.0f, 0.05f).gain(0.4f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.01f, 2).bp(2600.0f, 2600.0f, 1.5f).gain(0.4f);

  // la capsula si apre: lo sfiato dell'aria e un carillon felice (re maggiore, come le altre fanfare)
  defsnd(S[SND_RESCUE], "esploratore_salvato", -9.0f, PR_TOP, 2, 0.0f, 0.2f);
  S[SND_RESCUE].duck = 0.7f;
  lay(W_NOISE).env(0.005f, 0.08f, 0.3f, 2).bp(3500.0f, 1800.0f, 1.2f, 0.35f).gain(0.7f);
  lay(W_NOISE).env(0.005f, 0.05f, 0.2f, 2).lp(1500.0f, 900.0f).gain(0.4f);
  {
    const float f[4] = { D5, FS5, A5, D6 };
    for (int k = 0; k < 4; ++k)
      lay(W_FM).at(0.25f + 0.08f * (float)k).env(0.002f, 0.0f, k == 3 ? 0.6f : 0.35f, 3).hz(f[k]).fm(2.0f, 0.9f, 0.0f).gain(0.6f);
  }
  lay(W_TRI).at(0.49f).env(0.01f, 0.2f, 0.4f, 2).hz(D4).gain(0.4f);
  lay(W_TRI).at(0.49f).env(0.01f, 0.2f, 0.4f, 2).hz(A4).gain(0.3f);
  lay(W_GRAIN).at(0.5f).hz(2500.0f, 5000.0f).grain(30.0f, 0.03f).env(0.01f, 0.1f, 0.3f, 2).gain(0.12f);

  // l'esploratore chiama aiuto con la radio: fruscio, bip-bip, fruscio
  defsnd(S[SND_HELP], "aiuto", -18.0f, PR_MID, 2, 0.02f, 0.2f);
  lay(W_NOISE).env(0.002f, 0.0f, 0.05f, 2).bp(2000.0f, 2000.0f, 1.2f).gain(0.4f);
  for (int k = 0; k < 2; ++k) {
    lay(W_PULSE).width(0.5f).at(0.05f + 0.12f * (float)k).env(0.004f, 0.06f, 0.02f, 2).hz(1250.0f).bp(1500.0f, 1500.0f, 1.5f)
        .gain(0.8f);
    lay(W_SINE).at(0.05f + 0.12f * (float)k).env(0.004f, 0.06f, 0.02f, 2).hz(1250.0f).gain(0.4f);
  }
  lay(W_NOISE).at(0.27f).env(0.002f, 0.0f, 0.06f, 2).bp(1800.0f, 1800.0f, 1.5f).gain(0.25f);

  defsnd(S[SND_GATE], "cancello", -11.0f, PR_HIGH, 2, 0.02f, 0.2f);
  lay(W_SNOISE).hz(60.0f, 40.0f).env(0.1f, 0.5f, 0.3f, 2).gain(0.45f);
  lay(W_NOISE).env(0.1f, 0.5f, 0.3f, 2).bp(320.0f, 180.0f, 1.5f).gain(1.8f);
  lay(W_CRACK).hz(60.0f, 30.0f).env(0.1f, 0.5f, 0.3f, 2).bp(800.0f, 700.0f, 1.2f).gain(2.2f);
  lay(W_SAW).env(0.1f, 0.5f, 0.3f, 2).hz(55.0f, 45.0f).lp(600.0f, 450.0f, 2.0f).vib(3.0f, 0.4f).gain(0.3f);
  lay(W_SINE).at(0.78f).env(0.002f, 0.01f, 0.15f, 2).hz(90.0f, 50.0f, 0.1f, P_OUT).gain(0.5f);
  lay(W_NOISE).at(0.78f).env(0.002f, 0.0f, 0.06f, 2).lp(900.0f, 400.0f).gain(0.8f);

  // caduta nel cenote di Chichen Itza (si perde una vita): l'aria che corre e si allontana (con il fischio della
  // caduta), un attimo di niente, poi laggiu' il tonfo nell'acqua ("plum", la bolla, lo spruzzo) e due echi tra le
  // pareti di roccia, sempre piu' cupi. Profondo ma nella banda che l'altoparlantino suona (sopra i 200 Hz)
  defsnd(S[SND_SPLASH], "cenote", -10.0f, PR_TOP, 2, 0.02f, 0.3f);
  S[SND_SPLASH].duck = 0.8f;
  lay(W_NOISE).env(0.25f, 0.15f, 0.3f, 2).bp(1600.0f, 420.0f, 1.6f, 0.7f).ccurve(P_IN).gain(2.0f);
  lay(W_SINE).env(0.2f, 0.1f, 0.4f, 2).hz(1250.0f, 380.0f, 0.7f, P_IN).vib(7.0f, 0.3f).gain(0.35f);
  lay(W_SINE).at(0.75f).env(0.002f, 0.02f, 0.3f, 2).hz(300.0f, 110.0f, 0.15f, P_OUT).gain(0.8f);
  lay(W_SINE).at(0.79f).env(0.004f, 0.0f, 0.09f, 2).hz(180.0f, 420.0f, 0.08f, P_OUT).gain(0.45f);
  lay(W_SNOISE).at(0.75f).hz(6000.0f).env(0.004f, 0.03f, 0.45f, 2).lp(3000.0f, 700.0f, 0.8f, 0.4f).ccurve(P_OUT).gain(1.1f);
  lay(W_NOISE).at(0.75f).env(0.003f, 0.02f, 0.35f, 2).bp(900.0f, 500.0f, 0.8f, 0.3f).gain(0.9f);
  lay(W_SNOISE).at(0.75f).hz(160.0f, 70.0f).env(0.01f, 0.05f, 0.45f, 2).gain(0.3f);
  lay(W_SINE).at(0.97f).env(0.004f, 0.02f, 0.25f, 2).hz(260.0f, 110.0f, 0.15f, P_OUT).gain(0.4f);
  lay(W_SNOISE).at(0.97f).hz(4000.0f).env(0.006f, 0.03f, 0.35f, 2).lp(1200.0f, 400.0f, 0.8f, 0.3f).gain(0.6f);
  lay(W_SNOISE).at(1.2f).hz(3000.0f).env(0.008f, 0.03f, 0.35f, 2).lp(700.0f, 300.0f, 0.8f, 0.3f).gain(0.4f);

  defsnd(S[SND_ZONE_CLEAR], "zona_finita", -9.0f, PR_TOP, 1, 0.0f, 0.3f);
  S[SND_ZONE_CLEAR].duck = 0.6f;
  {
    const float f[4] = { A4, D5, FS5, A5 }, h[4] = { FS4, A4, D5, FS5 }, t[4] = { 0.0f, 0.1f, 0.2f, 0.32f };
    for (int k = 0; k < 4; ++k) {
      const bool lastn = k == 3;
      lay(W_PULSE).width(0.25f).at(t[k]).env(0.004f, lastn ? 0.2f : 0.05f, lastn ? 0.18f : 0.04f, 2).hz(f[k])
          .lp(3200.0f, 2200.0f).vib(6.0f, lastn ? 0.15f : 0.0f, 0.1f).gain(0.7f);
      lay(W_TRI).at(t[k]).env(0.004f, lastn ? 0.2f : 0.05f, lastn ? 0.18f : 0.04f, 2).hz(h[k]).gain(0.5f);
    }
    lay(W_SINE).at(0.32f).env(0.004f, 0.15f, 0.25f, 2).hz(146.83f).gain(0.6f);
  }

  defsnd(S[SND_WAVE], "ondata", -10.0f, PR_TOP, 1, 0.0f, 0.3f);
  S[SND_WAVE].duck = 0.75f;
  lay(W_SAW).env(0.015f, 0.12f, 0.05f, 2).hz(A4).lp(1600.0f, 1600.0f, 1.0f).vib(5.0f, 0.1f, 0.08f).gain(0.8f);
  lay(W_SAW).env(0.015f, 0.12f, 0.05f, 2).hz(220.0f).lp(1000.0f, 1000.0f).gain(0.5f);
  lay(W_SAW).at(0.2f).env(0.015f, 0.2f, 0.1f, 2).hz(D5).lp(1800.0f, 1400.0f).vib(5.5f, 0.18f, 0.1f).gain(0.8f);
  lay(W_SAW).at(0.2f).env(0.015f, 0.2f, 0.1f, 2).hz(293.66f).lp(1100.0f, 1100.0f).gain(0.5f);

  // stella: 1-3 di fila a 0,4 s; ogni ripetizione sale (re, fa diesis, la)
  defsnd(S[SND_STAR], "stella", -11.0f, PR_TOP, 3, 0.0f, 0.05f);
  S[SND_STAR].flags = SF_STEP;
  S[SND_STAR].step[1] = 1.2599f; S[SND_STAR].step[2] = 1.4983f;
  lay(W_FM).env(0.002f, 0.01f, 0.38f, 3).hz(D6).fm(2.0f, 1.0f, 0.0f).gain(0.8f);
  lay(W_SINE).env(0.002f, 0.02f, 0.35f, 3).hz(2349.3f).gain(0.18f);
  lay(W_GRAIN).hz(2500.0f, 5000.0f).grain(30.0f, 0.03f).env(0.01f, 0.15f, 0.2f, 2).gain(0.1f);

  // tutti a terra: discesa triste in re minore, la musica continua piu' bassa
  defsnd(S[SND_WIPE], "tutti_giu", -9.0f, PR_TOP, 1, 0.0f, 0.5f);
  S[SND_WIPE].duck = 0.35f;
  {
    const float f[4] = { A4, 392.0f, 349.23f, 329.63f };
    for (int k = 0; k < 4; ++k)
      lay(W_PULSE).width(0.5f).at(0.22f * (float)k).env(0.01f, 0.1f, 0.1f, 2).hz(f[k]).lp(1600.0f, 1200.0f).gain(0.6f);
    const float c[3] = { D4, 349.23f, A4 };
    for (int k = 0; k < 3; ++k)
      lay(W_TRI).at(0.88f).env(0.02f, 0.25f, 0.35f, 2).hz(c[k]).vib(5.0f, 0.2f, 0.15f).gain(0.45f);
    lay(W_SINE).at(0.88f).env(0.02f, 0.25f, 0.35f, 2).hz(146.83f).gain(0.5f);
  }

  // --- menu ---
  defsnd(S[SND_UI_MOVE], "menu_sposta", -18.0f, PR_TOP, 2, 0.0f, 0.02f);
  lay(W_SINE).env(0.001f, 0.0f, 0.025f, 3).hz(1400.0f, 1200.0f, 0.02f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.004f, 2).bp(3000.0f, 3000.0f, 1.0f).gain(0.3f);

  defsnd(S[SND_UI_OK], "menu_ok", -12.0f, PR_TOP, 2, 0.0f, 0.03f);
  lay(W_PULSE).width(0.5f).env(0.002f, 0.03f, 0.01f, 2).hz(A5).lp(3000.0f, 3000.0f).gain(0.6f);
  lay(W_SINE).env(0.002f, 0.03f, 0.01f, 2).hz(A5).gain(0.4f);
  lay(W_PULSE).width(0.5f).at(0.04f).env(0.002f, 0.02f, 0.04f, 2).hz(D6).lp(3000.0f, 3000.0f).gain(0.6f);
  lay(W_SINE).at(0.04f).env(0.002f, 0.02f, 0.04f, 2).hz(D6).gain(0.4f);

  defsnd(S[SND_UI_BACK], "menu_indietro", -13.0f, PR_TOP, 2, 0.0f, 0.03f);
  lay(W_PULSE).width(0.5f).env(0.002f, 0.03f, 0.01f, 2).hz(D6).lp(3000.0f, 3000.0f).gain(0.6f);
  lay(W_SINE).env(0.002f, 0.03f, 0.01f, 2).hz(D6).gain(0.4f);
  lay(W_PULSE).width(0.5f).at(0.04f).env(0.002f, 0.02f, 0.04f, 2).hz(784.0f).lp(3000.0f, 3000.0f).gain(0.6f);
  lay(W_SINE).at(0.04f).env(0.002f, 0.02f, 0.04f, 2).hz(784.0f).gain(0.4f);

  defsnd(S[SND_CARD_OPEN], "carte", -12.0f, PR_TOP, 1, 0.0f, 0.1f);
  lay(W_NOISE).env(0.15f, 0.05f, 0.2f, 2).bp(500.0f, 2600.0f, 1.2f, 0.35f).gain(0.6f);
  lay(W_CRACK).hz(140.0f, 60.0f).env(0.05f, 0.2f, 0.15f, 2).bp(2500.0f, 2500.0f, 1.5f).gain(1.0f);
  lay(W_SINE).env(0.05f, 0.1f, 0.25f, 2).hz(400.0f, 900.0f, 0.35f, P_IN).gain(0.3f);

  defsnd(S[SND_CARD_PICK], "carta_scelta", -11.0f, PR_TOP, 2, 0.0f, 0.05f);
  lay(W_NOISE).env(0.001f, 0.005f, 0.03f, 2).bp(2200.0f, 1800.0f, 1.2f).gain(0.7f);
  lay(W_FM).at(0.03f).env(0.002f, 0.01f, 0.25f, 3).hz(D6).fm(3.0f, 1.2f, 0.0f).gain(0.8f);
  lay(W_SINE).at(0.03f).env(0.002f, 0.01f, 0.2f, 3).hz(D5).gain(0.4f);

  defsnd(S[SND_PAUSE], "pausa", -13.0f, PR_TOP, 1, 0.0f, 0.1f);
  lay(W_TRI).env(0.003f, 0.05f, 0.03f, 2).hz(A5).gain(0.8f);
  lay(W_TRI).at(0.085f).env(0.003f, 0.04f, 0.08f, 2).hz(D5).gain(0.8f);

  for (int i = 1; i < NSOUNDS; ++i) finish(S[i]);
}

// Strumenti della musica: altezze in multipli della nota (LF_REL); batteria ad altezza fissa.
static void define_instruments(void) {
  SDef* I = g_ins;
  defsnd(I[I_LEAD], "melodia", 0, 0, 1, 0, 0);
  lay(W_PULSE).width(0.3f).opt(LF_REL).hz(1.0f).env(0.006f, 0.03f, 0.3f, 2).sus(0.65f, 0.1f).lp(3200.0f, 1800.0f, 0.9f, 0.25f)
      .key(0.4f).vib(5.6f, 0.14f, 0.2f).gain(0.8f);
  lay(W_TRI).opt(LF_REL).hz(1.0f).env(0.006f, 0.03f, 0.3f, 2).sus(0.65f, 0.1f).vib(5.6f, 0.14f, 0.2f).gain(0.45f);

  defsnd(I[I_LEAD_SOFT], "melodia_morbida", 0, 0, 1, 0, 0);
  lay(W_PULSE).width(0.5f).opt(LF_REL).hz(1.0f).env(0.005f, 0.02f, 0.25f, 2).sus(0.55f, 0.09f).lp(2400.0f, 1500.0f, 0.8f, 0.2f)
      .key(0.3f).vib(5.2f, 0.12f, 0.22f).gain(0.7f);
  lay(W_SINE).opt(LF_REL).hz(1.0f).env(0.005f, 0.02f, 0.25f, 2).sus(0.55f, 0.09f).vib(5.2f, 0.12f, 0.22f).gain(0.5f);

  defsnd(I[I_LEAD_BOSS], "melodia_boss", 0, 0, 1, 0, 0);
  lay(W_SAW).opt(LF_REL).hz(1.0f).env(0.008f, 0.02f, 0.3f, 2).sus(0.7f, 0.12f).lp(2600.0f, 1400.0f, 1.2f, 0.3f)
      .key(0.3f).vib(6.0f, 0.15f, 0.18f).gain(0.6f);
  lay(W_PULSE).width(0.25f).opt(LF_REL).hz(0.5f).env(0.008f, 0.02f, 0.3f, 2).sus(0.7f, 0.12f).lp(1500.0f, 1000.0f, 0.7f)
      .vib(6.0f, 0.15f, 0.18f).gain(0.3f);

  defsnd(I[I_HARM], "seconda_voce", 0, 0, 1, 0, 0);
  lay(W_PULSE).width(0.5f).opt(LF_REL).hz(1.0f).env(0.008f, 0.02f, 0.25f, 2).sus(0.6f, 0.1f).lp(2200.0f, 1400.0f, 0.7f, 0.25f)
      .vib(5.6f, 0.1f, 0.25f).gain(0.6f);

  defsnd(I[I_BASS], "basso", 0, 0, 1, 0, 0);
  lay(W_PULSE).width(0.5f).opt(LF_REL).hz(1.0f).env(0.003f, 0.02f, 0.25f, 2).sus(0.55f, 0.05f).lp(1400.0f, 380.0f, 1.1f, 0.15f)
      .key(0.5f).gain(0.7f);
  lay(W_SINE).opt(LF_REL).hz(1.0f).env(0.003f, 0.05f, 0.3f, 2).sus(0.7f, 0.05f).gain(0.55f);

  defsnd(I[I_BASS_BOSS], "basso_boss", 0, 0, 1, 0, 0);
  lay(W_SAW).opt(LF_REL).hz(1.0f).env(0.002f, 0.01f, 0.2f, 2).sus(0.5f, 0.04f).lp(1800.0f, 450.0f, 1.4f, 0.12f).key(0.5f)
      .gain(0.75f);
  lay(W_SINE).opt(LF_REL).hz(1.0f).env(0.002f, 0.03f, 0.25f, 2).sus(0.7f, 0.04f).gain(0.7f);

  defsnd(I[I_STAB], "accordi", 0, 0, 1, 0, 0);
  lay(W_PULSE).width(0.5f).opt(LF_REL).hz(1.0f).env(0.002f, 0.0f, 0.16f, 2).sus(0.0f, 0.03f).lp(2400.0f, 800.0f, 0.8f, 0.12f)
      .gain(0.8f);

  defsnd(I[I_PAD], "tappeto", 0, 0, 1, 0, 0);
  lay(W_SAW).opt(LF_REL).hz(1.0f).env(0.15f, 0.1f, 0.6f, 2).sus(0.75f, 0.4f).lp(1000.0f, 1000.0f, 0.6f).key(0.3f)
      .vib(4.5f, 0.06f).gain(0.6f);
  lay(W_TRI).opt(LF_REL).hz(1.003f).env(0.15f, 0.1f, 0.6f, 2).sus(0.75f, 0.4f).gain(0.4f);

  defsnd(I[I_ARP], "arpeggio", 0, 0, 1, 0, 0);
  lay(W_PULSE).width(0.25f).opt(LF_REL).hz(1.0f).env(0.002f, 0.0f, 0.13f, 2).sus(0.0f, 0.03f).lp(3000.0f, 1200.0f, 0.8f, 0.1f)
      .gain(0.8f);

  defsnd(I[I_KICK], "cassa", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(165.0f, 48.0f, 0.09f, P_FAST).env(0.001f, 0.015f, 0.3f, 3);
  lay(W_NOISE).env(0.001f, 0.0f, 0.012f, 2).lp(2500.0f, 1500.0f).gain(0.3f);
  lay(W_TRI).hz(260.0f, 120.0f, 0.03f, P_OUT).env(0.001f, 0.0f, 0.05f, 2).gain(0.25f);

  defsnd(I[I_SNARE], "rullante", 0, 0, 1, 0, 0);
  lay(W_NOISE).env(0.001f, 0.005f, 0.17f, 3).bp(1900.0f, 1400.0f, 0.8f).gain(1.45f);
  lay(W_TRI).hz(220.0f, 170.0f, 0.05f, P_OUT).env(0.001f, 0.005f, 0.09f, 2).gain(0.95f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.05f, 2).hp(5500.0f, 5500.0f).gain(0.25f);

  defsnd(I[I_HAT], "charleston", 0, 0, 1, 0, 0);
  lay(W_NOISE).env(0.001f, 0.0f, 0.035f, 2).hp(7500.0f, 7500.0f, 0.7f).gain(1.6f);

  defsnd(I[I_OHAT], "charleston_aperto", 0, 0, 1, 0, 0);
  lay(W_NOISE).env(0.002f, 0.01f, 0.25f, 2).hp(6800.0f, 6800.0f, 0.7f).gain(1.3f);

  defsnd(I[I_CRASH], "piatto", 0, 0, 1, 0, 0);
  lay(W_NOISE).env(0.002f, 0.02f, 1.2f, 2).hp(4500.0f, 4500.0f).gain(0.7f);
  lay(W_NOISE).env(0.002f, 0.0f, 0.4f, 2).bp(5500.0f, 5500.0f, 0.6f).gain(0.4f);

  defsnd(I[I_TOM], "tom", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(230.0f, 140.0f, 0.15f, P_OUT).env(0.001f, 0.01f, 0.25f, 3);
  lay(W_NOISE).env(0.001f, 0.0f, 0.03f, 2).lp(1800.0f, 900.0f).gain(0.25f);

  defsnd(I[I_CLAP], "battimani", 0, 0, 1, 0, 0);
  lay(W_NOISE).env(0.001f, 0.0f, 0.15f, 3).bp(1300.0f, 1100.0f, 1.2f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.012f, 2).bp(1700.0f, 1700.0f, 1.5f).gain(1.2f);

  // --- 0.5, la Terra. Gli strumenti melodici hanno al massimo tre strati e niente acuti taglienti: la musica
  // deve stare sotto gli effetti. I fiati hanno il soffio (rumore filtrato che segue la nota, kt 1).

  // Egitto. Liuto: pizzico caldo e un po' nasale che si spegne in fretta.
  defsnd(I[I_OUD], "liuto", 0, 0, 1, 0, 0);
  lay(W_SAW).opt(LF_REL).hz(1.0f).env(0.002f, 0.0f, 0.9f, 3).sus(0.0f, 0.08f).lp(2600.0f, 650.0f, 1.1f, 0.25f).key(0.5f)
      .gain(0.55f);
  lay(W_TRI).opt(LF_REL).hz(1.0f).env(0.002f, 0.0f, 0.75f, 3).sus(0.0f, 0.08f).gain(0.5f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.012f, 2).bp(2000.0f, 2000.0f, 1.5f).key(0.5f).gain(0.3f);

  defsnd(I[I_OUD_BASS], "liuto_basso", 0, 0, 1, 0, 0);
  lay(W_TRI).opt(LF_REL).hz(1.0f).env(0.003f, 0.02f, 0.45f, 2).sus(0.35f, 0.06f).gain(0.45f);
  lay(W_SAW).opt(LF_REL).hz(1.0f).env(0.003f, 0.0f, 0.3f, 3).sus(0.2f, 0.06f).lp(1500.0f, 450.0f, 1.0f, 0.15f).key(0.4f)
      .gain(0.6f);

  // flauto di canna: attacco lento, soffio, vibrato largo che arriva dopo
  defsnd(I[I_NEY], "flauto_canna", 0, 0, 1, 0, 0);
  lay(W_TRI).opt(LF_REL).hz(1.0f).env(0.07f, 0.0f, 0.3f, 2).sus(0.8f, 0.14f).vib(5.0f, 0.2f, 0.25f).gain(0.62f);
  lay(W_NOISE).env(0.03f, 0.0f, 0.18f, 2).sus(0.4f, 0.1f).bp(523.0f, 523.0f, 3.0f).key(1.0f).gain(0.8f);

  // cetra: pizzico brillante con due corde appena scordate
  defsnd(I[I_QANUN], "cetra", 0, 0, 1, 0, 0);
  lay(W_SAW).opt(LF_REL).hz(1.0f).env(0.001f, 0.0f, 0.55f, 3).sus(0.0f, 0.1f).lp(4000.0f, 1300.0f, 0.9f, 0.15f).key(0.3f)
      .gain(0.4f);
  lay(W_TRI).opt(LF_REL).hz(1.004f).env(0.001f, 0.0f, 0.6f, 3).sus(0.0f, 0.1f).gain(0.45f);

  // bordone: entra piano e resta (triangoli: costa poco ed e' sempre acceso)
  defsnd(I[I_DRONE], "bordone", 0, 0, 1, 0, 0);
  lay(W_TRI).opt(LF_REL).hz(1.0f).env(0.9f, 0.0f, 1.0f, 2).sus(0.85f, 0.7f).vib(4.2f, 0.07f).gain(0.7f);

  // tamburo a calice: colpo grave al centro, colpo secco sul bordo, colpo leggero dell'altra mano
  defsnd(I[I_DOUM], "calice_grave", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(108.0f, 74.0f, 0.08f, P_OUT).env(0.001f, 0.01f, 0.32f, 3);
  lay(W_SINE).hz(270.0f, 195.0f, 0.05f, P_OUT).env(0.001f, 0.0f, 0.12f, 3).gain(0.35f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.02f, 2).lp(1300.0f, 700.0f).gain(0.4f);

  defsnd(I[I_TEK], "calice_bordo", 0, 0, 1, 0, 0);
  lay(W_NOISE).env(0.001f, 0.0f, 0.035f, 3).bp(2900.0f, 2600.0f, 1.8f).gain(1.5f);
  lay(W_SINE).hz(930.0f, 800.0f, 0.03f, P_OUT).env(0.001f, 0.0f, 0.05f, 3).gain(0.55f);

  defsnd(I[I_KA], "calice_leggero", 0, 0, 1, 0, 0);
  lay(W_NOISE).env(0.001f, 0.0f, 0.028f, 3).bp(2300.0f, 2100.0f, 1.5f).gain(1.1f);
  lay(W_SINE).hz(800.0f, 700.0f, 0.03f, P_OUT).env(0.001f, 0.0f, 0.035f, 3).gain(0.35f);

  // tamburello a sonagli: sonagli filtrati sotto i 6 kHz e un filo di metallo
  defsnd(I[I_RIQ], "tamburello", 0, 0, 1, 0, 0);
  lay(W_SNOISE).hz(7000.0f).env(0.002f, 0.0f, 0.09f, 2).bp(3300.0f, 3000.0f, 1.0f).gain(1.4f);

  // cimbalini: "tin" che resta nell'aria
  defsnd(I[I_ZILLS], "cimbalini", 0, 0, 1, 0, 0);
  lay(W_FM).hz(1850.0f).fm(1.47f, 0.8f, 0.1f).env(0.001f, 0.0f, 0.9f, 2).gain(0.45f);
  lay(W_SINE).hz(2770.0f).env(0.001f, 0.0f, 0.5f, 2).gain(0.15f);

  // tamburo a cornice con la cordiera
  defsnd(I[I_BENDIR], "cornice", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(88.0f, 63.0f, 0.1f, P_OUT).env(0.002f, 0.01f, 0.38f, 3).gain(0.9f);
  lay(W_SINE).hz(230.0f, 170.0f, 0.05f, P_OUT).env(0.002f, 0.0f, 0.1f, 3).gain(0.25f);
  lay(W_NOISE).env(0.002f, 0.0f, 0.15f, 2).bp(450.0f, 380.0f, 1.2f).gain(0.6f);

  // colpo sul bordo di legno
  defsnd(I[I_BOMBO_RIM], "bordo_legno", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(1100.0f, 900.0f, 0.01f, P_OUT).env(0.001f, 0.0f, 0.03f, 3).gain(0.55f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.02f, 3).bp(1800.0f, 1800.0f, 2.5f).gain(1.1f);

  // Chichen Itza. Ocarina di terracotta: quasi un seno (il vaso risuona), un filo della seconda armonica, il soffio
  // stretto sulla nota (piu' forte all'attacco) e un vibrato leggero che arriva tardi
  defsnd(I[I_OCARINA], "ocarina", 0, 0, 1, 0, 0);
  lay(W_SINE).opt(LF_REL).hz(1.0f).env(0.035f, 0.0f, 0.3f, 2).sus(0.85f, 0.1f).vib(5.2f, 0.16f, 0.28f).gain(0.62f);
  lay(W_SINE).opt(LF_REL).hz(2.0f).env(0.03f, 0.0f, 0.2f, 2).sus(0.45f, 0.08f).vib(5.2f, 0.16f, 0.28f).gain(0.07f);
  lay(W_NOISE).env(0.015f, 0.0f, 0.08f, 2).sus(0.22f, 0.06f).bp(262.0f, 262.0f, 4.5f).key(1.0f).gain(1.0f);

  // marimba: la barra di legno con il suo risuonatore (la fondamentale che resta), il secondo modo due ottave sopra
  // che si spegne subito e il colpo della mazza. Le barre suonano finche' non si spengono da sole: il rilascio e' lungo
  defsnd(I[I_MARIMBA], "marimba", 0, 0, 1, 0, 0);
  lay(W_SINE).opt(LF_REL).hz(1.0f).env(0.002f, 0.0f, 0.7f, 3).sus(0.0f, 0.2f).gain(0.75f);
  lay(W_SINE).opt(LF_REL).hz(3.98f).env(0.001f, 0.0f, 0.1f, 3).sus(0.0f, 0.04f).gain(0.2f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.01f, 2).bp(1500.0f, 1500.0f, 1.2f).key(0.6f).gain(0.3f);

  // marimba bassa: barre lunghe; l'ottava (triangolo) e il secondo modo (4 volte) la fanno sentire anche
  // sull'altoparlantino, che sotto i 200 Hz non suona
  defsnd(I[I_MARIMBA_BASS], "marimba_bassa", 0, 0, 1, 0, 0);
  lay(W_SINE).opt(LF_REL).hz(1.0f).env(0.003f, 0.0f, 1.0f, 3).sus(0.0f, 0.12f).gain(0.7f);
  lay(W_TRI).opt(LF_REL).hz(2.0f).env(0.003f, 0.0f, 0.45f, 3).sus(0.0f, 0.08f).gain(0.2f);
  lay(W_SINE).opt(LF_REL).hz(4.0f).env(0.002f, 0.0f, 0.3f, 3).sus(0.0f, 0.05f).gain(0.3f);

  // tamburo profondo battuto con la mano (huehuetl, pax): "dum" grave, la pelle e il colpo della mano
  defsnd(I[I_HUEHUETL], "tamburo_profondo", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(80.0f, 52.0f, 0.12f, P_OUT).env(0.003f, 0.01f, 0.5f, 3);
  lay(W_SINE).hz(250.0f, 175.0f, 0.06f, P_OUT).env(0.002f, 0.0f, 0.13f, 3).gain(0.4f);
  lay(W_NOISE).env(0.002f, 0.0f, 0.04f, 2).lp(700.0f, 250.0f).gain(0.5f);

  // guscio di tartaruga battuto con il corno di cervo: "toc" secco con due risonanze del guscio
  defsnd(I[I_TURTLE], "guscio_tartaruga", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(780.0f, 730.0f, 0.01f, P_OUT).env(0.001f, 0.0f, 0.035f, 3).gain(0.55f);
  lay(W_SINE).hz(1290.0f).env(0.001f, 0.0f, 0.02f, 3).gain(0.25f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.012f, 3).bp(2000.0f, 2000.0f, 2.0f).gain(0.8f);

  // sonaglio di zucca con i semi: un fruscio morbido, niente metallo
  defsnd(I[I_SONAJA], "sonaglio", 0, 0, 1, 0, 0);
  lay(W_SNOISE).hz(6500.0f).env(0.006f, 0.0f, 0.07f, 2).bp(3000.0f, 2600.0f, 1.1f).gain(1.3f);

  // conchiglia (il richiamo dei sacerdoti, in la): entra calante, il filtro si apre con il fiato
  defsnd(I[I_CONCH], "conchiglia", 0, 0, 1, 0, 0);
  lay(W_SAW).hz(196.0f, 220.0f, 0.12f, P_OUT).env(0.1f, 0.45f, 0.4f, 2).lp(280.0f, 1500.0f, 1.2f).opt(LF_CENV)
      .vib(5.0f, 0.15f, 0.25f).gain(0.7f);
  lay(W_SINE).hz(196.0f, 220.0f, 0.12f, P_OUT).env(0.1f, 0.45f, 0.4f, 2).gain(0.35f);
  lay(W_NOISE).env(0.08f, 0.4f, 0.4f, 2).bp(700.0f, 700.0f, 2.0f).gain(0.3f);

  // bastone della pioggia: i semi che scendono tra le spine (granelli fitti) e il loro fruscio, sale e si spegne piano
  defsnd(I[I_RAINSTICK], "bastone_pioggia", 0, 0, 1, 0, 0);
  lay(W_GRAIN).hz(1600.0f, 4200.0f).grain(400.0f, 0.003f).env(0.35f, 0.25f, 0.9f, 2).gain(0.5f);
  lay(W_SNOISE).hz(5000.0f).env(0.4f, 0.2f, 0.8f, 2).bp(2800.0f, 2800.0f, 1.0f).gain(0.25f);

  // tamburo a fessura (tunkul, teponaztli): tronco cavo con due lingue di legno battute con la gomma; in la, le altre
  // altezze dai tre toni dei tom
  defsnd(I[I_TUNKUL], "tamburo_fessura", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(232.0f, 220.0f, 0.03f, P_OUT).env(0.001f, 0.004f, 0.28f, 3).gain(0.85f);
  lay(W_SINE).hz(612.0f, 596.0f, 0.02f, P_OUT).env(0.001f, 0.0f, 0.06f, 3).gain(0.22f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.012f, 2).bp(900.0f, 700.0f, 1.4f).gain(0.4f);

  // battimani davanti alla scalinata del Castillo: l'eco torna dai gradini come il verso del quetzal (un "cip" che
  // scende, fatto di tanti piccoli riflessi)
  defsnd(I[I_CLAP_ECO], "battimani_eco", 0, 0, 1, 0, 0);
  lay(W_NOISE).env(0.001f, 0.0f, 0.06f, 3).bp(1300.0f, 1100.0f, 1.2f).gain(1.2f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.01f, 2).bp(1800.0f, 1800.0f, 1.5f).gain(1.0f);
  lay(W_SINE).at(0.07f).hz(1150.0f, 520.0f, 0.14f, P_OUT).env(0.004f, 0.02f, 0.12f, 2).gain(0.3f);
  lay(W_PULSE).at(0.07f).width(0.15f).hz(1150.0f, 520.0f, 0.14f, P_OUT).env(0.004f, 0.02f, 0.12f, 2)
      .bp(1000.0f, 700.0f, 1.0f, 0.14f).gain(0.12f);

  // Cina. Flauto con la membrana che ronza; violino a due corde nasale; cetra che risuona a lungo
  defsnd(I[I_DIZI], "flauto_bambu", 0, 0, 1, 0, 0);
  lay(W_TRI).opt(LF_REL).hz(1.0f).env(0.03f, 0.0f, 0.25f, 2).sus(0.85f, 0.08f).vib(6.0f, 0.2f, 0.2f).gain(0.5f);
  lay(W_PULSE).width(0.15f).opt(LF_REL).hz(1.0f).env(0.03f, 0.0f, 0.25f, 2).sus(0.85f, 0.08f).lp(2500.0f, 2500.0f, 1.0f)
      .key(0.5f).vib(6.0f, 0.2f, 0.2f).gain(0.08f);
  lay(W_NOISE).env(0.015f, 0.0f, 0.1f, 2).sus(0.25f, 0.06f).bp(1046.0f, 1046.0f, 3.0f).key(1.0f).gain(0.55f);

  defsnd(I[I_ERHU], "violino_cinese", 0, 0, 1, 0, 0);
  lay(W_SAW).opt(LF_REL).hz(1.0f).env(0.06f, 0.0f, 0.3f, 2).sus(0.85f, 0.12f).lp(1250.0f, 1250.0f, 2.4f)
      .vib(6.2f, 0.28f, 0.15f).gain(0.5f);

  defsnd(I[I_GUZHENG], "cetra_cinese", 0, 0, 1, 0, 0);
  lay(W_TRI).opt(LF_REL).hz(1.0f).env(0.001f, 0.0f, 1.2f, 3).sus(0.0f, 0.3f).gain(0.55f);
  lay(W_SAW).opt(LF_REL).hz(1.0f).env(0.001f, 0.0f, 0.6f, 3).sus(0.0f, 0.2f).lp(4500.0f, 1100.0f, 0.8f, 0.3f).key(0.3f)
      .gain(0.4f);

  // blocco di legno (alto e basso dai tre toni dei tom), gong piccolo che sale, gong grande, piatti, tamburo
  defsnd(I[I_BLOCK], "blocco", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(1350.0f, 1240.0f, 0.01f, P_OUT).env(0.001f, 0.0f, 0.05f, 3).gain(0.7f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.012f, 3).bp(2400.0f, 2400.0f, 3.0f).gain(0.6f);

  defsnd(I[I_GONG], "gong_piccolo", 0, 0, 1, 0, 0);
  lay(W_FM).hz(520.0f, 575.0f, 0.25f, P_OUT).fm(1.41f, 1.5f, 0.3f).env(0.002f, 0.0f, 0.6f, 2).gain(0.5f);
  lay(W_SINE).hz(1340.0f, 1420.0f, 0.25f, P_OUT).env(0.002f, 0.0f, 0.3f, 2).gain(0.15f);

  defsnd(I[I_GONG_BIG], "gong_grande", 0, 0, 1, 0, 0);
  lay(W_FM).hz(176.0f, 150.0f, 0.8f, P_OUT).fm(1.47f, 2.5f, 0.4f).env(0.01f, 0.0f, 2.2f, 2).gain(0.6f);
  lay(W_NOISE).env(0.005f, 0.0f, 0.4f, 2).bp(650.0f, 450.0f, 1.4f).gain(0.4f);
  lay(W_SINE).hz(96.0f).env(0.01f, 0.0f, 1.4f, 2).gain(0.3f);

  defsnd(I[I_BO], "piatti_cinesi", 0, 0, 1, 0, 0);
  lay(W_SNOISE).hz(6500.0f).env(0.002f, 0.0f, 0.32f, 2).bp(2700.0f, 2300.0f, 1.0f).gain(1.1f);
  lay(W_FM).hz(1180.0f).fm(2.76f, 2.0f, 0.6f).env(0.001f, 0.0f, 0.22f, 2).gain(0.18f);

  defsnd(I[I_TANGGU], "tamburo_cinese", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(82.0f, 56.0f, 0.1f, P_OUT).env(0.002f, 0.01f, 0.5f, 3);
  lay(W_NOISE).env(0.002f, 0.0f, 0.08f, 2).lp(1100.0f, 350.0f).gain(0.45f);
  lay(W_SINE).hz(210.0f, 140.0f, 0.06f, P_OUT).env(0.002f, 0.0f, 0.15f, 3).gain(0.35f);

  // Roma. Corno: il filtro si apre con il fiato (LF_CENV) e la nota entra appena calante, come un ottone
  defsnd(I[I_BUCCINA], "corno", 0, 0, 1, 0, 0);
  lay(W_SAW).opt(LF_REL | LF_CENV).hz(0.985f, 1.0f, 0.04f, P_OUT).env(0.03f, 0.05f, 0.25f, 2).sus(0.7f, 0.1f)
      .lp(560.0f, 2200.0f, 1.2f).key(0.5f).vib(5.5f, 0.12f, 0.25f).gain(0.62f);

  defsnd(I[I_HORN], "corni", 0, 0, 1, 0, 0);
  lay(W_SAW).opt(LF_REL | LF_CENV).hz(1.0f).env(0.04f, 0.02f, 0.3f, 2).sus(0.6f, 0.15f).lp(420.0f, 1500.0f, 0.8f).key(0.3f).gain(0.6f);

  defsnd(I[I_TUBA], "tuba", 0, 0, 1, 0, 0);
  lay(W_SAW).opt(LF_REL | LF_CENV).hz(1.0f).env(0.02f, 0.02f, 0.2f, 2).sus(0.65f, 0.08f).lp(230.0f, 1000.0f, 1.1f).key(0.3f).gain(0.8f);

  // archi staccati per l'ostinato, coro "ah" (la formante principale della vocale)
  defsnd(I[I_STRINGS], "archi", 0, 0, 1, 0, 0);
  lay(W_SAW).opt(LF_REL).hz(1.0f).env(0.004f, 0.02f, 0.14f, 2).sus(0.0f, 0.04f).lp(2300.0f, 1200.0f, 0.8f, 0.1f).key(0.3f).gain(0.6f);

  defsnd(I[I_CHOIR], "coro", 0, 0, 1, 0, 0);
  lay(W_SAW).opt(LF_REL).hz(1.0f).env(0.25f, 0.0f, 0.5f, 2).sus(0.85f, 0.4f).bp(780.0f, 780.0f, 2.2f).vib(5.0f, 0.1f, 0.3f).gain(1.5f);

  // tamburo da guerra, rullante militare, timpani (accordati dai tre toni dei tom), incudine, piatto
  defsnd(I[I_WARDRUM], "tamburo_guerra", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(72.0f, 45.0f, 0.12f, P_OUT).env(0.002f, 0.01f, 0.55f, 3);
  lay(W_NOISE).env(0.002f, 0.0f, 0.08f, 2).lp(1400.0f, 350.0f).gain(0.55f);


  defsnd(I[I_SNARE_MIL], "rullante_militare", 0, 0, 1, 0, 0);
  lay(W_SNOISE).hz(8000.0f).env(0.001f, 0.005f, 0.14f, 3).bp(2100.0f, 1700.0f, 0.8f).gain(1.5f);
  lay(W_TRI).hz(205.0f, 165.0f, 0.04f, P_OUT).env(0.001f, 0.0f, 0.06f, 2).gain(0.7f);
  lay(W_CRACK).hz(2200.0f).env(0.001f, 0.0f, 0.1f, 2).bp(2500.0f, 2500.0f, 1.2f).gain(0.3f);

  defsnd(I[I_TIMPANI], "timpani", 0, 0, 1, 0, 0);
  lay(W_SINE).hz(123.47f, 121.0f, 0.3f).env(0.002f, 0.02f, 0.6f, 2).gain(0.8f);
  lay(W_SINE).hz(185.0f).env(0.002f, 0.0f, 0.3f, 2).gain(0.32f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.03f, 2).lp(700.0f, 400.0f).gain(0.35f);

  defsnd(I[I_ANVIL], "incudine", 0, 0, 1, 0, 0);
  lay(W_FM).hz(940.0f).fm(2.76f, 1.4f, 0.2f).env(0.001f, 0.0f, 0.35f, 2).gain(0.45f);
  lay(W_SINE).hz(2350.0f).env(0.001f, 0.0f, 0.2f, 2).gain(0.1f);
  lay(W_NOISE).env(0.001f, 0.0f, 0.01f, 2).bp(2400.0f, 2400.0f, 1.5f).gain(0.3f);

  defsnd(I[I_CYMBAL], "piatto_scuro", 0, 0, 1, 0, 0);
  lay(W_SNOISE).hz(7500.0f).env(0.002f, 0.02f, 1.3f, 2).bp(3200.0f, 2600.0f, 0.7f).gain(0.8f);
  lay(W_SNOISE).hz(5000.0f).env(0.002f, 0.0f, 0.3f, 2).bp(1800.0f, 1800.0f, 0.9f).gain(0.4f);

  // titolo e vittoria: campana FM, tappeto morbido (triangoli), arpeggio di cristallo, basso morbido
  defsnd(I[I_BELL], "campana", 0, 0, 1, 0, 0);
  lay(W_FM).opt(LF_REL).hz(1.0f).fm(3.5f, 1.4f, 0.1f).env(0.002f, 0.0f, 1.6f, 2).sus(0.0f, 0.4f).gain(0.55f);

  defsnd(I[I_WARM], "tappeto_caldo", 0, 0, 1, 0, 0);
  lay(W_TRI).opt(LF_REL).hz(1.0f).env(0.45f, 0.0f, 1.0f, 2).sus(0.8f, 0.6f).vib(4.0f, 0.09f).gain(0.65f);

  defsnd(I[I_GLASS], "cristallo", 0, 0, 1, 0, 0);
  lay(W_FM).opt(LF_REL).hz(1.0f).fm(2.0f, 1.3f, 0.0f).env(0.002f, 0.0f, 0.4f, 3).sus(0.0f, 0.15f).gain(0.52f);

  defsnd(I[I_BASS_SOFT], "basso_morbido", 0, 0, 1, 0, 0);
  lay(W_SINE).opt(LF_REL).hz(1.0f).env(0.004f, 0.03f, 0.4f, 2).sus(0.6f, 0.08f).gain(0.7f);
  lay(W_SAW).opt(LF_REL).hz(1.0f).env(0.004f, 0.03f, 0.3f, 2).sus(0.4f, 0.08f).lp(800.0f, 800.0f, 0.7f).key(0.3f).gain(0.3f);

  for (int i = 0; i < NINST; ++i) finish(I[i]);
}

// Una voce ha passato il suo picco quando ogni strato ha finito, oppure e' gia' partito, non ha tenuta ed e'
// calato (o nel rilascio) sotto un quarto: da li' in poi non torna piu' al picco gia' misurato.
static bool past_peak(const Voice& v) {
  const SDef& d = *v.def;
  const float t = (float)v.age * G.ifs;
  for (int i = 0; i < d.nl; ++i) {
    const LDef& l = d.L[i];
    const LRun& r = v.lr[i];
    if (r.st == ST_DONE) continue;
    if (r.st == ST_RUN && (t < l.t0 + l.a + l.h || l.s > 0.0f)) return false;
    if (r.env > 0.25f) return false;
  }
  return true;
}

// Picco di un suono suonato da solo (al centro, vol 1, prima di norm). I suoni con rumore cambiano un po' a ogni
// copia (+-2 dB di picco): per loro la media di tre copie. Si sintetizza solo fino a quando il suono ha passato il suo
// picco: la coda non serve.
static float measure_peak(int id) {
  const SDef& d = g_snd[id];
  Voice& v = G.sv[0];
  bool noisy = false;
  for (int i = 0; i < d.nl; ++i) noisy = noisy || d.L[i].wave == W_NOISE || d.L[i].wave == W_SNOISE || d.L[i].wave >= W_CRACK;
  const int runs = noisy ? 3 : 1;
  float lsum = 0.0f;
  for (int run = 0; run < runs; ++run) {
    sfx_start(v, id, 0.0f, 1.0f, 1.0f);
    float pk = 1e-9f;
    const int max_blocks = (int)(3.0f * G.fs) / BLK, hold = (int)(0.5f * G.fs);
    for (int b = 0; b < max_blocks; ++b) {
      if ((d.flags & SF_LOOP) && v.age < hold) v.last_trig = v.age;
      if ((d.flags & SF_LOOP) && v.gate && (float)(v.age - v.last_trig) > d.keep * G.fs) v.gate = 0;
      const bool alive = voice_block(v);
      float bp = 0.0f;
      for (int i = 0; i < BLK; ++i) bp = maxf(bp, fabsf(G.vb[i]));
      pk = maxf(pk, bp);
      G.st.init_samples += BLK;
      if (!alive || (bp < 0.5f * pk && past_peak(v))) break;
    }
    lsum += log2f(pk);
  }
  v.on = 0;
  return exp2f(lsum / (float)runs);
}

// Impronta della definizione di un suono (FNV-1a sui parametri degli strati e su cio' che conta per la taratura).
static uint32_t def_hash(const SDef& d) {
  uint32_t h = 2166136261u;
  const auto mix = [&h](const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
  };
  const uint8_t u[2] = { d.nl, d.flags };
  mix(u, sizeof(u));
  mix(&d.keep, sizeof(d.keep));
  for (int i = 0; i < d.nl; ++i) {
    const LDef& l = d.L[i];
    const uint8_t c[6] = { l.wave, l.filt, l.pc, l.cc, l.dc, l.fl };
    const float f[21] = { l.t0, l.a, l.h, l.d, l.s, l.r, l.f0, l.f1, l.pt, l.c0, l.c1, l.ct, l.q, l.kt, l.pw, l.m0, l.m1,
                          l.vr, l.vd, l.vdel, l.g };
    mix(c, sizeof(c));
    mix(f, sizeof(f));
  }
  return h;
}

// Tarature gia' fatte: impronta e picco di ogni suono a 48 kHz. audio_init prende il picco da qui quando l'impronta
// coincide e misura solo i suoni cambiati: l'avvio non sintetizza niente (sulla console la taratura costava 0,4 s).
// La tabella la scrive audio_calibration_table; tools/test_audio.cpp la controlla e la stampa quando va rifatta.
struct CalEntry { uint32_t hash; float peak; };
static const CalEntry CAL_TAB[NSOUNDS] = {
  { 0u, 0.0f },
  { 0xA3D58577u, 2.106737f },   // blaster
  { 0x272EE1B3u, 0.4665188f },   // cristalli
  { 0x9B7867C3u, 0.8556951f },   // ghiaccio_secco
  { 0x7597CC58u, 1.181021f },   // fiamma
  { 0x58317832u, 1.144408f },   // lava
  { 0xCAFA245Fu, 0.9936234f },   // razzi
  { 0xF4C3FD1Fu, 1.075351f },   // balzo_lunare
  { 0x07A3317Du, 1.905933f },   // schianto
  { 0x5D5EBDE6u, 0.5978668f },   // tempesta_gelata
  { 0xB9036192u, 1.270514f },   // eruzione
  { 0x87B23D66u, 1.112329f },   // speciale_pronto
  { 0x378F5571u, 1.15189f },   // salto
  { 0xE035D013u, 1.075755f },   // atterraggio
  { 0x9E2CDEA7u, 1.558477f },   // trampolino
  { 0xF13E5739u, 1.493134f },   // bomba_giu
  { 0xD9EE4A5Eu, 2.059417f },   // esplosione
  { 0x47F28F20u, 1.426952f },   // esplosione_piccola
  { 0x584AFCC4u, 1.201616f },   // talismano
  { 0xD888526Eu, 0.7959376f },   // bomba_presa
  { 0x3E9A202Bu, 2.099108f },   // colpito
  { 0x368A165Au, 1.875233f },   // a_terra
  { 0xE7D0ED77u, 1.453423f },   // rientro
  { 0x179BA63Cu, 0.9644304f },   // colpo
  { 0x483112E4u, 1.095479f },   // sconfitto
  { 0xD358D8EDu, 0.5973368f },   // scorpione
  { 0x9ADBDF8Au, 1.371025f },   // arciere
  { 0x1FB19E26u, 0.726403f },   // scarabei
  { 0x51C545EEu, 1.283243f },   // guerriero
  { 0xEC1425B3u, 1.879649f },   // gladiatore
  { 0x900C371Eu, 1.210488f },   // giavellotto
  { 0x4D799111u, 1.613693f },   // spadata
  { 0xFDE25BD8u, 1.241967f },   // cassa_colpita
  { 0x40FAC7DDu, 1.083091f },   // cassa_rotta
  { 0x68F7DEE3u, 1.408267f },   // capsula
  { 0x803154AFu, 1.314319f },   // esploratore_salvato
  { 0xC1EAF2D5u, 1.321921f },   // aiuto
  { 0x62B8F678u, 1.041175f },   // cancello
  { 0xE37A4A0Fu, 2.187463f },   // zona_finita
  { 0xB0EE1A56u, 1.181561f },   // ondata
  { 0xBB69EC78u, 1.060411f },   // stella
  { 0x4FF43264u, 1.654689f },   // tutti_giu
  { 0x641E7B99u, 0.9734906f },   // menu_sposta
  { 0x5A7A0DC5u, 1.051399f },   // menu_ok
  { 0x600E338Du, 1.051399f },   // menu_indietro
  { 0x71D14BDFu, 0.5935249f },   // carte
  { 0x64256DD6u, 1.134711f },   // carta_scelta
  { 0x895783DBu, 0.7999992f },   // pausa
  { 0x930B7071u, 1.445952f },   // freccia_a_terra
  { 0x1EBAD247u, 2.119899f },   // cenote
  { 0x53AF0F1Bu, 1.754611f },   // passo_gladiatore
  { 0xD7C4E013u, 0.985891f },   // spada_alzata
  { 0xD35FF708u, 0.8441568f },   // fendente
  { 0x1B4B11BCu, 1.351436f },   // carica_scudo
  { 0x78654C85u, 2.212696f },   // scudo_contro_muro
};

// Taratura: norm porta il picco di ogni suono al valore voluto (peak_db, effetti a 8, al centro).
static void calibrate(bool table) {
  const float center = 0.70710678f, sfx8 = vcurve(8);
  for (int id = 1; id < NSOUNDS; ++id) {
    SDef& d = g_snd[id];
    if (!d.nl) continue;
    const uint32_t h = def_hash(d);
    const bool known = table && G.fs == 48000.0f && CAL_TAB[id].hash == h && CAL_TAB[id].peak > 0.0f;
    const float pk = known ? CAL_TAB[id].peak : measure_peak(id);
    G.cal_hash[id] = h;
    G.cal_peak[id] = pk;
    d.norm = pk > 1e-6f ? powf(10.0f, d.peak_db * 0.05f) / (pk * center * sfx8) : 0.0f;
  }
  memset(G.sv, 0, sizeof(G.sv));
}

int audio_calibration_table(char* buf, int size) {
  if (!G.ready) return -1;
  G.rng = RNG_SEED;   // come all'avvio: la misura esce uguale a ogni prova
  calibrate(false);
  int diff = 0, n = 0;
  char changed[400] = "";
  for (int id = 1; id < NSOUNDS; ++id) {
    const bool same = CAL_TAB[id].hash == G.cal_hash[id] && CAL_TAB[id].peak > 0.0f &&
                      fabsf(G.cal_peak[id] / CAL_TAB[id].peak - 1.0f) < 1e-4f;
    if (same) continue;
    diff++;
    const int c = (int)strlen(changed);
    if (c < (int)sizeof(changed) - 40) snprintf(changed + c, sizeof(changed) - (size_t)c, " %s", g_snd[id].name);
  }
  if (!buf || size <= 0) return diff;
  buf[0] = 0;
  n += snprintf(buf + n, (size_t)(size - n), "// da rifare:%s\n  { 0u, 0.0f },\n", diff ? changed : " niente");
  for (int id = 1; id < NSOUNDS && n < size; ++id)
    n += snprintf(buf + n, (size_t)(size - n), "  { 0x%08Xu, %.7gf },   // %s\n", (unsigned)G.cal_hash[id], (double)G.cal_peak[id],
                  g_snd[id].name);
  return diff;
}

// --- brani: lettura dei testi ---------------------------------------------------------------------

static void song_err(const Song& s, int bar, const char* what) {
  G.nerr++;
  const int n = (int)strlen(G.err);
  if (n < (int)sizeof(G.err) - 80)
    snprintf(G.err + n, sizeof(G.err) - (size_t)n, "brano %s, battuta %d: %s\n", s.name, bar + 1, what);
}

static int note_pc(char c) {
  static const int PC[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
  return c >= 'A' && c <= 'G' ? PC[c - 'A'] : -1;
}

static bool parse_chord(const char* t, int n, Chord& c) {
  struct Qual { const char* q; int8_t n, t0, t1, t2, t3, third, fifth, sev; };
  static const Qual Q[] = {
    { "", 3, 0, 4, 7, 0, 4, 7, -1 }, { "m", 3, 0, 3, 7, 0, 3, 7, -1 }, { "7", 4, 0, 4, 7, 10, 4, 7, 10 },
    { "maj7", 4, 0, 4, 7, 11, 4, 7, 11 }, { "m7", 4, 0, 3, 7, 10, 3, 7, 10 }, { "sus4", 3, 0, 5, 7, 0, 5, 7, -1 },
    { "sus2", 3, 0, 2, 7, 0, 2, 7, -1 }, { "dim", 3, 0, 3, 6, 0, 3, 6, -1 }, { "aug", 3, 0, 4, 8, 0, 4, 8, -1 },
    { "6", 4, 0, 4, 7, 9, 4, 7, 9 }, { "m6", 4, 0, 3, 7, 9, 3, 7, 9 }, { "add9", 4, 0, 2, 4, 7, 4, 7, -1 },
    { "7sus4", 4, 0, 5, 7, 10, 5, 7, 10 }, { "m7b5", 4, 0, 3, 6, 10, 3, 6, 10 },
    { "5", 2, 0, 7, 0, 0, 7, 7, -1 },   // quinta vuota (senza terza: il grado 3 del basso da' la quinta)
  };
  memset(&c, 0, sizeof(c));
  int pc = note_pc(t[0]), i = 1;
  if (pc < 0) return false;
  if (i < n && t[i] == '#') { pc++; i++; } else if (i < n && t[i] == 'b') { pc--; i++; }
  char q[8];
  int qn = 0;
  while (i < n && t[i] != '/' && qn < 7) q[qn++] = t[i++];
  q[qn] = 0;
  int qi = -1;
  for (int k = 0; k < (int)(sizeof(Q) / sizeof(Q[0])) && qi < 0; ++k) {
    int j = 0;
    while (Q[k].q[j] && Q[k].q[j] == q[j]) j++;
    if (Q[k].q[j] == q[j]) qi = k;
  }
  if (qi < 0) return false;
  c.root = (int8_t)pcm(pc);
  c.n = Q[qi].n;
  c.iv[0] = Q[qi].t0; c.iv[1] = Q[qi].t1; c.iv[2] = Q[qi].t2; c.iv[3] = Q[qi].t3;
  c.third = Q[qi].third; c.fifth = Q[qi].fifth; c.sev = Q[qi].sev;
  c.bass = c.root;
  if (i < n && t[i] == '/') {
    int b = note_pc(t[i + 1]);
    if (b < 0) return false;
    if (i + 2 < n && t[i + 2] == '#') b++;
    else if (i + 2 < n && t[i + 2] == 'b') b--;
    c.bass = (int8_t)pcm(b);
  }
  const int ln = n < 7 ? n : 7;
  memcpy(c.name, t, (size_t)ln);
  c.name[ln] = 0;
  return true;
}

// Accordi: battute separate da '|', gli accordi di una battuta si dividono i 16 passi in parti uguali.
static void parse_chords(Song& s, const char* txt) {
  int bar = 0;
  const char* p = txt;
  while (*p && bar < MAXBARS) {
    const char* tok[4];
    int len[4], n = 0;
    while (*p && *p != '|') {
      while (*p == ' ') p++;
      if (!*p || *p == '|') break;
      const char* b = p;
      while (*p && *p != ' ' && *p != '|') p++;
      if (n < 4) { tok[n] = b; len[n] = (int)(p - b); }
      n++;
    }
    if (*p == '|') p++;
    if (n == 0) { while (*p == ' ') p++; continue; }
    if (n != 1 && n != 2 && n != 4) { song_err(s, bar, "servono 1, 2 o 4 accordi per battuta"); n = n > 4 ? 4 : (n == 3 ? 2 : n); }
    for (int k = 0; k < n; ++k) {
      Chord c;
      if (!parse_chord(tok[k], len[k], c)) { song_err(s, bar, "accordo sconosciuto"); parse_chord("C", 1, c); }
      if (s.nch < MAXCH) s.ch[s.nch++] = c;
      for (int st = k * 16 / n; st < (k + 1) * 16 / n; ++st) s.cat[bar * 16 + st] = (uint8_t)(s.nch - 1);
    }
    bar++;
    while (*p == ' ') p++;
  }
  s.nbars = bar;
}

static int orn_flag(char c) {
  switch (c) {
  case '_': return O_SLIDE;
  case '/': return O_SCOOP;
  case '^': return O_GRACE;
  case '~': return O_TRILL;
  case '!': return O_ACC;
  case '\'': return O_SOFT;
  default: return 0;
  }
}

// Melodia: "NOTA[:durata]" (durata in sedicesimi, 2 se manca), "r" pausa, "-" prolunga la nota prima;
// davanti alla nota gli abbellimenti (O_*), anche piu' d'uno: "!/A4:4".
static void parse_lead(Song& s, const char* txt) {
  int bar = 0, st = 0, last = -1;
  const char* p = txt;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    if (*p == '|') {
      if (st != 16) song_err(s, bar, "la melodia non fa 16 sedicesimi");
      bar++; st = 0; p++;
      continue;
    }
    const char* b = p;
    while (*p && *p != ' ' && *p != '|') p++;
    int n = (int)(p - b), dur = 2, orn = 0;
    while (n > 1 && orn_flag(b[0])) { orn |= orn_flag(b[0]); b++; n--; }
    for (int k = 0; k < n; ++k)
      if (b[k] == ':') { dur = 0; for (int j = k + 1; j < n; ++j) dur = dur * 10 + (b[j] - '0'); n = k; break; }
    if (dur <= 0 || st + dur > 16 || bar >= s.nbars) { song_err(s, bar, "nota fuori dalla battuta"); st += dur; continue; }
    const int gs = bar * 16 + st;
    if (b[0] == '-') {
      if (last >= 0) s.nlen[last] = (uint8_t)(s.nlen[last] + dur);
    } else if (b[0] != 'r') {
      int pc = note_pc(b[0]), k = 1;
      if (pc < 0 || n < 2) { song_err(s, bar, "nota sconosciuta"); st += dur; continue; }
      if (b[k] == '#') { pc++; k++; } else if (b[k] == 'b') { pc--; k++; }
      if (k >= n || b[k] < '0' || b[k] > '8') { song_err(s, bar, "nota senza ottava"); st += dur; continue; }
      const int oct = b[k] - '0';
      s.note[gs] = (uint8_t)(12 * (oct + 1) + pc);
      s.nlen[gs] = (uint8_t)dur;
      s.orn[gs] = (uint8_t)orn;
      last = gs;
    } else {
      last = -1;
    }
    st += dur;
  }
  if (bar != s.nbars) song_err(s, bar, "la melodia non ha tante battute quanti gli accordi");
}

static void parse_pat(Song& s, Pat& pt, const char* txt) {
  memset(&pt, 0, sizeof(pt));
  int n = 0;
  for (const char* p = txt; *p; ++p)
    if (*p != ' ') { if (n < 16) pt.c[n] = *p; n++; }
  if (n != 16) song_err(s, 0, "un modello non ha 16 passi");
  for (int i = 0; i < 16; ++i) {
    int l = 1;
    for (int j = i + 1; j < 16 && pt.c[j] == '-'; ++j) l++;
    pt.len[i] = (uint8_t)l;
  }
}

static void parse_arr(Song& s, uint8_t* arr, const char* txt, bool onoff) {
  int n = 0;
  for (const char* p = txt ? txt : ""; *p; ++p) {
    if (*p == ' ' || *p == '|') continue;
    if (n < MAXBARS) arr[n] = *p == '.' ? 255 : (uint8_t)(onoff ? 1 : (*p >= 'a' && *p <= 'h' ? *p - 'a' : 0));
    n++;
  }
  if (txt && n != s.nbars) song_err(s, n, "lo schema delle battute non ha la lunghezza del brano");
  for (int i = txt ? n : 0; i < MAXBARS; ++i) arr[i] = 255;
}

// Seconda voce battuta per battuta: '.' spenta, 'x' come dice la traccia (TrackCfg.lo), 't' armonizzata,
// 'u' all'unisono, 'o' / 'O' ottava sotto / sopra, 'f' quinta sotto, 'q' quarta sotto, 's' sesta sotto.
static void parse_harm(Song& s, const char* txt) {
  const int lo = s.tc[T_HARM].lo;
  int n = 0;
  for (const char* p = txt ? txt : ""; *p; ++p) {
    if (*p == ' ' || *p == '|') continue;
    int v = HV_OFF;
    switch (*p) {
    case '.': break;
    case 'x': v = lo ? lo : HV_HARM; break;
    case 't': v = HV_HARM; break;
    case 'u': v = 0; break;
    case 'o': v = 12; break;
    case 'O': v = -12; break;
    case 'f': v = 7; break;
    case 'q': v = 5; break;
    case 's': v = 9; break;
    default: song_err(s, n, "seconda voce: lettera sconosciuta"); break;
    }
    if (n < MAXBARS) s.hiv[n] = (int8_t)v;
    n++;
  }
  if (txt && n != s.nbars) song_err(s, n, "lo schema della seconda voce non ha la lunghezza del brano");
  for (int i = txt ? n : 0; i < MAXBARS; ++i) s.hiv[i] = HV_OFF;
}

static void build_song(Song& s, const SongSrc& src) {
  memset(&s, 0, sizeof(s));
  s.name = src.name; s.bpm = src.bpm; s.swing = src.swing; s.gain = src.gain; s.loop = src.loop;
  s.dly_fb = src.dly_fb; s.dly_mix = src.dly_mix;
  static const int8_t MODE[NMODES][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },   // maggiore
    { 0, 2, 4, 5, 7, 9, 10 },   // misolidio
    { 0, 2, 3, 5, 7, 8, 10 },   // minore
    { 0, 1, 4, 5, 7, 8, 10 },   // hijaz
    { 0, 2, 3, 5, 7, 9, 10 },   // dorico
    { 0, 2, 3, 5, 7, 8, 11 },   // minore armonica
    { 0, 1, 3, 5, 7, 8, 10 },   // frigio
  };
  const int mode = src.mode >= 0 && src.mode < NMODES ? src.mode : M_MAJ;
  s.key = pcm(src.key);
  for (int k = 0; k < 7; ++k) s.scale[k] = (int8_t)pcm(src.key + MODE[mode][k]);
  memcpy(s.tc, src.tc, sizeof(s.tc));
  s.tl2 = src.tl2;
  memcpy(s.dvol, src.dvol, sizeof(s.dvol));
  memcpy(s.dpan, src.dpan, sizeof(s.dpan));
  memcpy(s.tom, src.tom, sizeof(s.tom));
  for (int k = 0; k < ND; ++k) s.dk[k] = (uint8_t)(src.dk[k] >= 0 && src.dk[k] < NINST ? src.dk[k] : I_KICK + k);
  s.choke = src.choke;
  parse_chords(s, src.chords);
  parse_lead(s, src.lead);
  for (int t = 0; t < 4; ++t) {
    for (int v = 0; v < 6; ++v)
      if (src.pat[t][v]) parse_pat(s, s.pat[t][v], src.pat[t][v]);
    parse_arr(s, s.arr[T_BASS + t], src.arr[t], false);
  }
  parse_harm(s, src.harm);
  parse_arr(s, s.larr, src.larr, true);
  for (int v = 0; v < 8; ++v)
    for (int k = 0; k < ND; ++k) {
      const char* d = src.drum[v][k];
      if (!d) continue;
      int n = 0;
      for (const char* p = d; *p; ++p) {
        if (*p == ' ') continue;
        if (n < 16) {
          // X forte, x normale, o piano, R / r rullo forte / normale; tom: 1 2 3, rullo a b c
          uint8_t x = 0;
          if (k == DK_TOM)
            x = (*p >= '1' && *p <= '3') ? (uint8_t)((*p - '0') << 2 | 2)
                                         : ((*p >= 'a' && *p <= 'c') ? (uint8_t)((*p - 'a' + 1) << 2 | 2 | DR_ROLL) : 0);
          else x = *p == 'X' ? 3 : (*p == 'x' ? 2 : (*p == 'o' ? 1 : (*p == 'R' ? 3 | DR_ROLL : (*p == 'r' ? 2 | DR_ROLL : 0))));
          if (!x && *p != '.') song_err(s, 0, "batteria: lettera sconosciuta");
          s.dp[v].v[n][k] = x;
        }
        n++;
      }
      if (n != 16) song_err(s, 0, "una riga di batteria non ha 16 passi");
    }
  parse_arr(s, s.darr, src.darr, false);
  if (s.loop >= s.nbars) s.loop = 0;
  // tempi: battuta in campioni interi (niente deriva), passi con lo swing sugli ottavi in levare
  s.bar_len = (int)(4.0f * 60.0f / s.bpm * G.fs + 0.5f);
  const float beat = (float)s.bar_len * 0.25f, sw = s.swing > 0.0f ? s.swing : 0.5f;
  const float frac[4] = { 0.0f, sw * 0.5f, sw, (1.0f + sw) * 0.5f };
  for (int i = 0; i < 16; ++i) s.off[i] = (int)(beat * ((float)(i / 4) + frac[i % 4]) + 0.5f);
  s.off[16] = s.bar_len;
  s.dly1 = (int)(0.75f * beat);
  s.dly2 = s.dly1 + s.dly1 / 2;
  if (s.dly2 > DLY_N - BLK - 1) s.dly2 = DLY_N - BLK - 1;
  if (s.dly1 > s.dly2) s.dly1 = s.dly2;
}

static TrackCfg tcfg(int inst, float vol, float pan, float send, float gate, int lo) {
  TrackCfg t;
  memset(&t, 0, sizeof(t));
  t.inst = (uint8_t)inst; t.vol = vol; t.pan = pan; t.send = send; t.gate = gate; t.lo = lo;
  return t;
}

static void src_defaults(SongSrc& s) {
  memset(&s, 0, sizeof(s));
  s.tc[T_LEAD] = tcfg(I_LEAD, 0.5f, 0.0f, 0.22f, 0.9f, 0);
  s.tc[T_HARM] = tcfg(I_HARM, 0.4f, -0.3f, 0.15f, 0.9f, 0);
  s.tc[T_BASS] = tcfg(I_BASS, 0.27f, 0.0f, 0.0f, 0.8f, 40);
  s.tc[T_CHORD] = tcfg(I_STAB, 0.28f, 0.25f, 0.1f, 0.5f, 57);
  s.tc[T_PAD] = tcfg(I_PAD, 0.16f, -0.2f, 0.1f, 1.0f, 53);
  s.tc[T_ARP] = tcfg(I_ARP, 0.38f, 0.35f, 0.3f, 0.6f, 62);
  s.tl2 = s.tc[T_LEAD];
  // cassa, rullante, charleston chiuso e aperto, piatto, tom, battimani: strumenti, volumi, posizioni
  const float dv[ND] = { 0.75f, 1.0f, 0.3f, 0.2f, 0.25f, 0.6f, 0.5f };
  const float dp[ND] = { 0.0f, 0.05f, 0.25f, 0.25f, -0.2f, 0.0f, -0.1f };
  memcpy(s.dvol, dv, sizeof(dv));
  memcpy(s.dpan, dp, sizeof(dp));
  for (int k = 0; k < ND; ++k) s.dk[k] = I_KICK + k;
  s.choke = true;
  s.tom[0] = 1.3f; s.tom[1] = 1.0f; s.tom[2] = 0.78f;
  s.dly_fb = 0.3f; s.dly_mix = 0.3f; s.gain = 1.0f; s.swing = 0.5f;
}

// --- i brani (composizioni originali) ---------------------------------------------------------------
// Righe di batteria: K cassa, S rullante, H charleston, O charleston aperto, C piatto, T tom (1 2 3), P battimani;
// x normale, X accento, o leggero. Modelli: basso con i gradi dell'accordo (1 3 5 7 8, b 2 4 6; < > ^ note di
// avvicinamento all'accordo dopo), accordi e tappeto con x (suona) e - (tiene), arpeggio con gli indici 0-9
// delle note dell'accordo dal basso. Gli schemi dicono quale modello (a, b, c...) usare in ogni battuta.

static void song_title(SongSrc& s) {
  // Do maggiore, 96 BPM: avventura nello spazio. Introduzione con la campana sugli accordi sospesi del modo lidio
  // (re maggiore sul do: il fa diesis), A (melodia morbida), B (si sale verso la quinta, la campana a terze),
  // A' (la campana all'unisono), coda come l'introduzione che riporta ad A. 32 battute, ripresa dalla 5.
  src_defaults(s);
  s.name = "titolo"; s.bpm = 96.0f; s.swing = 0.5f; s.loop = 4; s.key = 0; s.mode = M_MAJ; s.gain = 0.72f;
  s.chords =
    "Cmaj7 | D/C | Cmaj7 | D/C |"
    "C | G/B | Am7 | Fmaj7 | C | G/B | Fmaj7 | G |"
    "Am | Em | F | C | Dm7 | Em7 | Fmaj7 | Gsus4 G |"
    "C | G/B | Am7 | Fmaj7 | C | G/B | Fmaj7 G | C |"
    "Cmaj7 | D/C | Cmaj7 | D/C |";
  s.lead =
    // introduzione: la campana
    "r:4 G4:2 C5:2 E5:8 | F#5:6 E5:2 D5:8 | r:4 G4:2 C5:2 E5:4 G5:4 | F#5:4 A5:4 D5:8 |"
    // A
    "E5:4 G5:4 C6:6 B5:2 | B5:4 A5:2 G5:2 D5:8 | C6:4 B5:2 A5:2 E5:4 G5:4 | A5:12 r:4 |"
    "E5:4 G5:4 C6:4 D6:4 | E6:6 D6:2 B5:4 G5:4 | A5:4 C6:4 E6:4 C6:4 | D6:8 B5:4 G5:4 |"
    // B
    "A5:6 B5:2 C6:4 A5:4 | G5:6 A5:2 B5:4 G5:4 | F5:4 A5:4 C6:6 A5:2 | G5:8 E5:8 |"
    "F5:4 A5:4 C6:4 D6:4 | E6:4 D6:2 B5:2 G5:8 | A5:4 C6:4 E6:8 | D6:8 B5:4 D6:4 |"
    // A'
    "E5:4 G5:4 C6:6 B5:2 | B5:4 A5:2 G5:2 D5:8 | C6:4 B5:2 A5:2 E5:4 G5:4 | A5:12 r:4 |"
    "E5:4 G5:4 C6:4 E6:4 | D6:6 B5:2 G5:4 D5:4 | A5:4 C6:4 B5:4 D6:4 | C6:12 r:4 |"
    // coda
    "r:4 G4:2 C5:2 E5:8 | F#5:6 E5:2 D5:8 | r:4 G4:2 C5:2 E5:4 G5:4 | F#5:4 A5:4 D5:4 B4:2 D5:2 |";
  s.tc[T_LEAD] = tcfg(I_LEAD_SOFT, 0.36f, 0.0f, 0.25f, 0.9f, 0);
  s.tl2 = tcfg(I_BELL, 0.42f, 0.1f, 0.3f, 0.95f, 0);
  s.larr = "xxxx ........ ........ ........ xxx.";   // l'ultima battuta della coda: la melodia riporta ad A
  s.tc[T_HARM] = tcfg(I_BELL, 0.24f, -0.25f, 0.3f, 0.95f, 0);
  s.harm = ".... ........ tttttttt uuuuuuuu ....";
  s.tc[T_BASS] = tcfg(I_BASS_SOFT, 0.34f, 0.0f, 0.0f, 0.9f, 36);
  s.pat[0][0] = "1------- 5---8---";
  s.pat[0][1] = "1---------------";
  s.pat[0][2] = "1------- 1-------";       // due accordi nella battuta
  s.arr[0] = "bbbb aaaaaaaa aaaaaaac aaaaaaca bbba";
  s.tc[T_PAD] = tcfg(I_WARM, 0.15f, -0.15f, 0.2f, 1.0f, 55);
  s.pat[2][0] = "x---------------";
  s.arr[2] = "aaaa aaaaaaaa aaaaaaaa aaaaaaaa aaaa";
  s.tc[T_ARP] = tcfg(I_GLASS, 0.17f, 0.3f, 0.35f, 0.9f, 67);
  s.pat[3][0] = "0-1-2-3-1-2-3-4-";
  s.arr[3] = "aaaa ........ aaaaaaaa ........ aaaa";
  // cassa, bordo di legno, charleston, charleston aperto, cimbalini (le stelle), timpani in do, battimani
  const int dk[ND] = { I_KICK, I_BOMBO_RIM, I_HAT, I_OHAT, I_ZILLS, I_TIMPANI, I_CLAP };
  const float dv[ND] = { 0.35f, 0.3f, 0.12f, 0.1f, 0.16f, 0.36f, 0.2f };
  memcpy(s.dk, dk, sizeof(dk)); memcpy(s.dvol, dv, sizeof(dv));
  s.tom[0] = 1.059f; s.tom[1] = 0.794f; s.tom[2] = 0.891f;   // do, sol, la
  s.drum[0][DK_KICK] = "x... .... x... ...."; s.drum[0][DK_SNARE] = ".... x... .... x..."; s.drum[0][DK_HAT] = "o.o. o.o. o.o. o.o.";
  s.drum[1][DK_KICK] = "x... ..x. x... ...."; s.drum[1][DK_SNARE] = ".... x... .... x..."; s.drum[1][DK_HAT] = "o.o. o.oo o.o. o.o.";
  s.drum[2][DK_CRASH] = "x... .... .... ...."; s.drum[2][DK_TOM] = "1... .... .... ....";
  s.drum[3][DK_KICK] = "x... .... x... ...."; s.drum[3][DK_SNARE] = ".... x... .... ...."; s.drum[3][DK_TOM] = ".... .... aaaa aaaa";
  s.drum[4][DK_TOM] = "1... .... 2... 2.2."; s.drum[4][DK_CRASH] = "x... .... .... ....";
  s.drum[5][DK_KICK] = "x... .... x.x. x.x."; s.drum[5][DK_SNARE] = ".... x... ..x. x.xx"; s.drum[5][DK_HAT] = "o.o. o.o. o.o. o.o.";
  s.drum[5][DK_TOM] = "1... .... .... ....";   // fine della coda: si riparte con A
  s.darr = "ccce aaaaaaaa bbbbbbbd aaaaaaaa cccf";
  s.dly_mix = 0.28f; s.dly_fb = 0.35f;
}

static void song_piramidi(SongSrc& s) {
  // Re hijaz (re, mi bemolle, fa diesis, sol, la, si bemolle, do), 116 BPM. Introduzione libera del flauto di canna
  // sul bordone, A (flauto), A' (liuto un'ottava sotto, la cetra lo raddoppia sopra), B (sol minore: il punto piu'
  // alto, flauto e cetra a ottave, liuto arpeggiato), pausa delle percussioni con le risposte del liuto, A'' (flauto
  // e cetra all'unisono, battimani). 40 battute, ripresa dalla 5. Tamburo a calice: maqsum, saidi nel B.
  src_defaults(s);
  s.name = "piramidi"; s.bpm = 116.0f; s.swing = 0.52f; s.loop = 4; s.key = 2; s.mode = M_HIJAZ; s.gain = 0.83f;
  s.chords =
    "D | D | Cm | D |"
    "D | D | Cm | D | D | Gm | Cm D | D |"
    "D | D | Cm | D | D | Gm | Cm D | D |"
    "Gm | Gm | Cm | D | Gm | Bb | Eb | D |"
    "D | D | Eb | D |"
    "D | D | Cm | D | D | Gm | Cm D | D |";
  s.lead =
    // introduzione
    "r:4 /A4:8 _Bb4:2 _A4:2 | ^G4:4 F#4:2 Eb4:2 _F#4:8 | r:2 G4:2 A4:2 C5:2 ~Bb4:6 A4:2 | G4:2 F#4:2 Eb4:2 F#4:2 D4:8 |"
    // A
    "A4:2 D5:2 Eb5:2 F#5:6 Eb5:2 D5:2 | ^Eb5:3 D5:1 C5:2 D5:2 A4:8 | G5:3 F#5:1 Eb5:2 F#5:2 G5:4 A5:4 |"
    "~A5:4 G5:2 F#5:2 Eb5:2 F#5:2 D5:4 |"
    "A4:2 D5:2 Eb5:2 F#5:6 Eb5:2 D5:2 | G5:4 Bb5:2 A5:2 G5:2 F#5:2 G5:4 | C6:3 Bb5:1 A5:2 G5:2 ~F#5:4 Eb5:4 |"
    "D5:10 r:2 A4:2 D5:2 |"
    // A': il liuto
    "A3:2 D4:2 Eb4:2 F#4:4 F#4:2 Eb4:2 D4:2 | Eb4:2 Eb4:1 D4:1 C4:2 D4:2 A3:4 A3:2 C4:2 |"
    "G4:2 G4:1 F#4:1 Eb4:2 F#4:2 G4:2 A4:2 Bb4:2 A4:2 | A4:4 G4:2 F#4:2 Eb4:2 F#4:2 D4:4 |"
    "A3:2 D4:2 Eb4:2 F#4:4 F#4:2 G4:2 A4:2 | Bb4:4 A4:2 G4:2 A4:2 Bb4:2 C5:2 Bb4:2 |"
    "C5:3 Bb4:1 A4:2 G4:2 F#4:2 G4:2 Eb4:4 | D4:8 r:4 A4:2 C5:2 |"
    // B
    "D5:4 G5:4 A5:2 Bb5:2 A5:4 | /G5:6 F#5:2 G5:2 A5:2 Bb5:4 | C6:4 Bb5:2 A5:2 G5:4 Eb5:4 | ~F#5:6 Eb5:2 D5:8 |"
    "D5:4 G5:4 A5:2 Bb5:2 C6:4 | !D6:4 C6:2 Bb5:2 A5:4 Bb5:4 | G5:3 A5:1 Bb5:2 A5:2 G5:2 F#5:2 Eb5:4 | D5:8 r:8 |"
    // pausa: il liuto risponde alle percussioni
    "r:8 A3:1 C4:1 Eb4:2 F#4:2 A4:2 | r:16 | r:8 Bb3:1 C4:1 Eb4:2 G4:2 Bb4:2 | A4:2 G4:2 F#4:2 Eb4:2 D4:4 r:4 |"
    // A''
    "A4:2 D5:2 Eb5:2 F#5:6 Eb5:2 D5:2 | ^Eb5:3 D5:1 C5:2 D5:2 A4:8 | G5:3 F#5:1 Eb5:2 F#5:2 G5:4 A5:4 |"
    "~A5:4 G5:2 F#5:2 Eb5:2 F#5:2 D5:4 |"
    "A4:2 D5:2 Eb5:2 F#5:4 G5:2 A5:2 Bb5:2 | D6:4 C6:2 Bb5:2 A5:2 Bb5:2 G5:4 | C6:3 Bb5:1 A5:2 G5:2 ~F#5:4 Eb5:4 |"
    "D5:8 r:4 F#4:2 G4:2 |";
  s.tc[T_LEAD] = tcfg(I_NEY, 0.45f, 0.0f, 0.25f, 0.92f, 0);
  s.tc[T_LEAD].glide = 0.09f;
  s.tl2 = tcfg(I_OUD, 0.5f, 0.1f, 0.12f, 0.95f, 0);
  s.tl2.glide = 0.06f;
  s.larr = ".... ........ xxxxxxxx ........ xxxx ........";
  s.tc[T_HARM] = tcfg(I_QANUN, 0.28f, -0.3f, 0.2f, 0.95f, 0);
  s.harm = ".... ........ OOOOOOOO oooooooo .... uuuuuuuu";
  s.tc[T_BASS] = tcfg(I_OUD_BASS, 0.36f, 0.0f, 0.0f, 0.85f, 38);
  s.pat[0][0] = "1-.. ..1- 8-.. 5-..";      // sul maqsum: tonica, ottava, quinta
  s.pat[0][1] = "1-.. ..1- 1-.. 5-..";
  s.pat[0][2] = "1-.. ..1- 8-.. b-..";      // con il mi bemolle che torna sul re
  s.pat[0][3] = "1..1 ..1. 1..1 ..1.";      // pausa: con il tamburo
  s.pat[0][4] = "1-.. ..1- 5-.. <-..";      // chiusura con avvicinamento
  s.arr[0] = "..aa acacaaae acacaaae bbbcbbbe dddd acacaaae";
  s.tc[T_CHORD] = tcfg(I_OUD, 0.17f, 0.3f, 0.12f, 0.9f, 57);
  s.tc[T_CHORD].strum = 0.012f;
  s.pat[1][0] = "..x. ..o. ..x. ..o.";      // liuto arpeggiato in levare
  s.arr[1] = ".... ........ ........ aaaaaaaa .... ........";
  s.tc[T_PAD] = tcfg(I_DRONE, 0.13f, -0.15f, 0.08f, 1.0f, 50);
  s.tc[T_PAD].pedal = 1;
  s.pat[2][0] = "x---------------";
  s.arr[2] = "aaaa aaaaaaaa aaaaaaaa aaaaaaaa aaaa aaaaaaaa";
  // calice grave, bordo, colpo leggero, tamburello, cimbalini, tamburo a cornice (tre toni), battimani
  const int dk[ND] = { I_DOUM, I_TEK, I_KA, I_RIQ, I_ZILLS, I_BENDIR, I_CLAP };
  const float dv[ND] = { 0.5f, 0.5f, 0.35f, 0.26f, 0.22f, 0.4f, 0.3f };
  const float dp[ND] = { 0.0f, 0.12f, -0.12f, 0.3f, -0.3f, -0.15f, 0.15f };
  memcpy(s.dk, dk, sizeof(dk)); memcpy(s.dvol, dv, sizeof(dv)); memcpy(s.dpan, dp, sizeof(dp));
  s.choke = false;
  s.tom[0] = 1.25f; s.tom[1] = 1.0f; s.tom[2] = 0.8f;
  s.drum[0][DK_KICK] = "x... .... x... ...."; s.drum[0][DK_SNARE] = "..x. ..x. .... x..."; // maqsum
  s.drum[0][DK_HAT] = ".... o... ..o. ..o."; s.drum[0][DK_OHAT] = "o.o. o.o. o.o. o.o.";
  s.drum[1][DK_KICK] = "x... .... x... ...."; s.drum[1][DK_SNARE] = "..x. ..x. .... x.o.";
  s.drum[1][DK_HAT] = "o... o.o. o.o. ..o."; s.drum[1][DK_OHAT] = "o.x. o.o. o.x. o.o."; s.drum[1][DK_TOM] = "2... .... 2... ....";
  s.drum[2][DK_KICK] = "x... ..x. x... ...."; s.drum[2][DK_SNARE] = "..x. .... .... x.x."; // saidi
  s.drum[2][DK_HAT] = ".... o.o. ..o. ...."; s.drum[2][DK_OHAT] = "o.o. o.o. o.o. o.o."; s.drum[2][DK_TOM] = "2... ..2. 2... ....";
  s.drum[3][DK_KICK] = "x... .... x... ...."; s.drum[3][DK_SNARE] = "..x. ..x. ..rr x.R.";  // chiusura con il rullo
  s.drum[3][DK_HAT] = ".... o... .... ...."; s.drum[3][DK_OHAT] = "o.o. o.o. o... ...."; s.drum[3][DK_CRASH] = ".... .... .... ..x.";
  s.drum[4][DK_KICK] = "x... .... .... ...."; s.drum[4][DK_TOM] = ".... .... 3... ..2."; // introduzione
  s.drum[4][DK_CRASH] = "x... .... .... ....";
  s.drum[5][DK_KICK] = "x..x ..x. x... ..x."; s.drum[5][DK_SNARE] = "..x. x.rr ..x. rrrr"; // assolo delle percussioni
  s.drum[5][DK_HAT] = "o..o .o.. o..o .o.."; s.drum[5][DK_CLAP] = ".... x... .... x..."; s.drum[5][DK_TOM] = "1... .... 1... ....";
  s.drum[6][DK_KICK] = "x... .... x... ...."; s.drum[6][DK_SNARE] = "..x. ..x. .... x.o."; // A'' con i battimani
  s.drum[6][DK_HAT] = "o... o.o. o.o. ..o."; s.drum[6][DK_OHAT] = "o.x. o.o. o.x. o.o."; s.drum[6][DK_TOM] = "2... .... 2... ....";
  s.drum[6][DK_CLAP] = ".... x... .... x...";
  s.drum[7][DK_KICK] = "x... .... x.x. x..."; s.drum[7][DK_SNARE] = "rrrr rrrr RRRR X..."; // fine della pausa
  s.drum[7][DK_CRASH] = ".... .... .... x..."; s.drum[7][DK_TOM] = "1... 2... 3... ....";
  s.darr = "eeed aaabaaad bbbbbbbd cccccccd fffh gggggggd";
  s.dly_mix = 0.2f; s.dly_fb = 0.28f;
}

static void song_chichenitza(SongSrc& s) {
  // La frigio (la, si bemolle, do, re, mi, fa, sol; la melodia quasi sempre sulla pentatonica minore), 108 BPM:
  // giungla dello Yucatan. Introduzione: la conchiglia chiama due volte sul bordone, l'ocarina risponde con il
  // sospiro frigio (si bemolle che torna sul la), il bastone della pioggia. A (ocarina sugli arpeggi della
  // marimba), A' (la marimba prende la melodia, l'altra marimba accompagna in levare), B (la salita al Castillo:
  // ogni battuta un gradino piu' su, due ocarine a terze come i flauti doppi; poi il serpente piumato scende
  // strisciando fino al la), pausa delle percussioni (tunkul, battimani con l'eco della scalinata, la marimba
  // risponde), A'' (marimba con l'ocarina un'ottava sopra). 40 battute, ripresa dalla 5.
  src_defaults(s);
  s.name = "chichenitza"; s.bpm = 108.0f; s.swing = 0.53f; s.loop = 4; s.key = 9; s.mode = M_PHR; s.gain = 0.745f;
  s.chords =
    "Am | Am | Am | Am |"
    "Am | C | Dm | Bb | Am | C | Dm Bb | Am |"
    "Am | C | Dm | Bb | Am | C | Dm Bb | Am |"
    "F | G | Am | Am | C | Dm | Bb | Am |"
    "Am | Am | Bb | Am |"
    "Am | C | Dm | Bb | Am | C | Dm Bb | Am |";
  s.lead =
    // introduzione: l'ocarina risponde alla conchiglia
    "r:8 /E5:4 _A5:4 | -:2 _Bb5:2 _A5:4 G5:2 E5:2 D5:4 | r:4 D5:2 E5:2 G5:4 ~A5:4 | G5:3 E5:1 D5:4 r:4 C5:2 D5:2 |"
    // A: ocarina
    "/E5:6 D5:2 E5:2 G5:2 A5:4 | G5:2 A5:2 G5:2 E5:2 D5:4 C5:4 | A5:3 G5:1 F5:2 E5:2 D5:2 C5:2 D5:4 |"
    "D5:4 F5:2 E5:2 ~D5:8 |"
    "/E5:6 D5:2 E5:2 G5:2 A5:4 | C6:2 A5:2 G5:2 E5:2 G5:4 ^A5:4 | D6:3 C6:1 A5:2 F5:2 Bb5:2 _A5:2 F5:4 |"
    "E5:3 D5:1 C5:2 D5:2 A4:8 |"
    // A': la marimba, con le note ribattute
    "E5:2 E5:2 E5:1 E5:1 D5:2 E5:2 G5:2 A5:2 A5:2 | G5:2 A5:2 G5:2 E5:2 D5:2 D5:1 E5:1 C5:4 |"
    "A5:2 A5:1 G5:1 F5:2 E5:2 D5:2 C5:2 D5:2 D5:2 | D5:2 D5:1 D5:1 F5:2 E5:2 D5:2 F5:2 Bb5:4 |"
    "E5:2 E5:2 E5:1 E5:1 D5:2 E5:2 G5:2 A5:2 A5:2 | C6:2 C6:1 A5:1 G5:2 E5:2 G5:2 G5:1 A5:1 G5:2 E5:2 |"
    "D6:2 C6:1 A5:1 F5:2 A5:2 F5:2 E5:1 D5:1 F5:2 D5:2 | E5:2 D5:1 C5:1 D5:2 C5:2 A4:6 r:2 |"
    // B: la salita (ogni battuta un gradino), la cima, il serpente che scende
    "A4:2 C5:2 F5:2 A5:6 G5:2 F5:2 | B4:2 D5:2 G5:2 B5:6 A5:2 G5:2 | C5:2 E5:2 A5:2 C6:6 B5:2 A5:2 |"
    "D6:2 C6:2 A5:2 C6:2 !E6:8 |"
    "E6:3 D6:1 C6:2 G5:2 C6:4 G5:4 | D6:3 C6:1 A5:2 F5:2 A5:4 D5:4 | F5:2 _E5:1 _F5:1 D5:2 _C5:2 Bb4:4 _A4:4 |"
    "~A4:12 r:4 |"
    // pausa: la marimba risponde alle percussioni
    "r:12 E5:1 D5:1 C5:1 A4:1 | r:8 A4:1 C5:1 D5:1 E5:1 G5:2 E5:2 | r:12 F5:1 D5:1 Bb4:1 F4:1 |"
    "A4:2 G4:1 E4:1 G4:2 A4:2 G4:2 E4:2 C4:2 D4:2 |"
    // A'': la marimba, l'ocarina un'ottava sopra
    "E4:6 D4:2 E4:2 G4:2 A4:4 | G4:2 A4:2 G4:2 E4:2 D4:4 C4:4 | A4:3 G4:1 F4:2 E4:2 D4:2 C4:2 D4:4 | D4:4 F4:2 E4:2 D4:8 |"
    "E4:6 D4:2 E4:2 G4:2 A4:4 | C5:2 D5:2 E5:2 D5:2 C5:2 A4:2 G4:4 | D5:3 C5:1 A4:2 F4:2 F4:2 E4:1 D4:1 F4:4 |"
    "E4:3 D4:1 C4:2 D4:2 A3:4 C4:2 D4:2 |";
  s.tc[T_LEAD] = tcfg(I_OCARINA, 0.42f, 0.0f, 0.28f, 0.92f, 0);
  s.tc[T_LEAD].glide = 0.08f;
  s.tl2 = tcfg(I_MARIMBA, 0.42f, 0.1f, 0.18f, 0.95f, 0);
  s.larr = ".... ........ xxxxxxxx ........ xxxx xxxxxxxx";
  s.tc[T_HARM] = tcfg(I_OCARINA, 0.26f, -0.3f, 0.25f, 0.9f, 0);
  s.tc[T_HARM].glide = 0.08f;
  s.harm = ".... ........ ........ tttttttt .... OOOOOOOO";
  s.tc[T_BASS] = tcfg(I_MARIMBA_BASS, 0.34f, 0.0f, 0.0f, 0.9f, 40);
  s.pat[0][0] = "1-.. ..1- .... 5-..";      // con il tamburo profondo (3 + 3 + 2)
  s.pat[0][1] = "1-.. ..1- .... <-..";      // chiusura con avvicinamento
  s.pat[0][2] = "1------- 5-------";        // la salita
  s.pat[0][3] = "1-.. 5-.. 1-.. 5-..";      // il serpente
  s.pat[0][4] = "1--- .... .... ....";      // pausa
  s.pat[0][5] = "1---------------";         // introduzione
  s.arr[0] = "..ff aaaaaaab aaaaaaab ccccdddb eeee aaaaaaab";
  s.tc[T_CHORD] = tcfg(I_MARIMBA, 0.16f, 0.25f, 0.1f, 0.8f, 60);
  s.tc[T_CHORD].ring = 1;
  s.pat[1][0] = "..x. .ox. ..x. .ox.";      // la seconda marimba in levare
  s.arr[1] = ".... ........ aaaaaaaa ........ .... aaaaaaaa";
  s.tc[T_PAD] = tcfg(I_DRONE, 0.13f, -0.1f, 0.08f, 1.0f, 52);
  s.tc[T_PAD].pedal = 1;
  s.pat[2][0] = "x---------------";
  s.arr[2] = "aaaa ........ ........ ........ aaaa ........";
  s.tc[T_ARP] = tcfg(I_MARIMBA, 0.19f, -0.25f, 0.2f, 0.9f, 55);
  s.tc[T_ARP].ring = 1;
  s.pat[3][0] = "0-2-13-20-2-13-2";
  s.pat[3][1] = "0-1-2-3-4-3-2-1-";         // la salita
  s.pat[3][2] = "5-4-3-2-1-2-3-4-";         // la discesa
  s.arr[3] = ".... aaaaaaaa ........ bbbbcccc .... ........";
  // tamburo profondo, guscio di tartaruga, sonaglio, conchiglia, bastone della pioggia, tunkul (do, la, mi), battimani
  // con l'eco
  const int dk[ND] = { I_HUEHUETL, I_TURTLE, I_SONAJA, I_CONCH, I_RAINSTICK, I_TUNKUL, I_CLAP_ECO };
  const float dv[ND] = { 0.38f, 0.6f, 0.34f, 0.5f, 0.28f, 0.26f, 0.7f };
  const float dp[ND] = { 0.0f, 0.15f, 0.3f, -0.05f, -0.25f, -0.15f, 0.1f };
  memcpy(s.dk, dk, sizeof(dk)); memcpy(s.dvol, dv, sizeof(dv)); memcpy(s.dpan, dp, sizeof(dp));
  s.choke = false;
  s.tom[0] = 1.189f; s.tom[1] = 1.0f; s.tom[2] = 0.749f;
  s.drum[0][DK_KICK] = "X... ..x. .... x..."; s.drum[0][DK_SNARE] = ".... x... .... x..."; // A
  s.drum[0][DK_HAT] = "x.o. x.ox x.o. x.ox"; s.drum[0][DK_TOM] = "..1. 2..1 ..1. 2.2.";
  s.drum[1][DK_KICK] = "X... ..x. x.x. x..."; s.drum[1][DK_SNARE] = ".... x... .... ...."; // fine della frase
  s.drum[1][DK_HAT] = "x.o. x.ox x.o. ...."; s.drum[1][DK_TOM] = "..1. 2..1 1212 3.3."; s.drum[1][DK_CRASH] = ".... .... x... ....";
  s.drum[2][DK_KICK] = "X... ..x. ..x. x..."; s.drum[2][DK_SNARE] = ".... x..o .... x..."; // A' e A''
  s.drum[2][DK_HAT] = "xoxo xoxo xoxo xoxo"; s.drum[2][DK_TOM] = "1.2. .12. 1.2. .21.";
  s.drum[3][DK_KICK] = "X... .... x... ...."; s.drum[3][DK_SNARE] = ".... .... x... ...."; // la salita
  s.drum[3][DK_HAT] = "x... x... x... x..."; s.drum[3][DK_TOM] = "3.3. 2.2. 1.1. 1.1.";
  s.drum[4][DK_KICK] = "x... .... .... ...."; s.drum[4][DK_OHAT] = "x... .... .... ...."; // introduzione
  s.drum[4][DK_TOM] = ".... .... 2... 2.1."; s.drum[4][DK_CRASH] = ".... .... x... ....";
  s.drum[5][DK_KICK] = "X... ..x. ..x. x.x."; s.drum[5][DK_HAT] = ".... x.o. x.o. xoxo"; // si riparte
  s.drum[5][DK_TOM] = "2.1. 2.1. 2.12 1212"; s.drum[5][DK_CRASH] = ".... .... x... ....";
  s.drum[6][DK_KICK] = "X... .... x... ...."; s.drum[6][DK_HAT] = "..o. ..o. ..o. ..o."; // pausa
  s.drum[6][DK_TOM] = "..1. 2.2. ..1. 2.1."; s.drum[6][DK_CLAP] = "x... .... x... ....";
  s.drum[7][DK_KICK] = "X... .... .... ...."; s.drum[7][DK_TOM] = "1.2. 3... .... ...."; // il serpente arriva al la
  s.drum[7][DK_OHAT] = ".... .... x... ...."; s.drum[7][DK_CRASH] = "x... .... .... ....";
  s.darr = "eeff aaaaaaab cccccccb dddddddh gggf cccccccb";
  s.dly_mix = 0.22f; s.dly_fb = 0.3f;
}

static void song_muraglia(SongSrc& s) {
  // Sol pentatonico (modo gong: sol la si re mi), 104 BPM. Introduzione con la cetra e il gong grande, A (violino a
  // due corde), A' (flauto di bambu' con la cetra un'ottava sotto), B (mi, il modo yu: violino e flauto si
  // rispondono ogni due battute, la cetra arpeggia), pausa dei tamburi e dei gong, A'' (violino con la cetra).
  // 40 battute, ripresa dalla 5.
  src_defaults(s);
  s.name = "muraglia"; s.bpm = 104.0f; s.swing = 0.5f; s.loop = 4; s.key = 7; s.mode = M_MAJ; s.gain = 0.92f;
  s.chords =
    "G6 | G6 | Em7 | D |"
    "G6 | Em7 | Cmaj7 | D | G6 | Em7 | Am7 D | G6 |"
    "G6 | Em7 | Cmaj7 | D | G6 | Em7 | Am7 D | G6 |"
    "Em | Em | Am7 | D | Em | Cmaj7 | Bm7 | Em |"
    "G6 | Em7 | Cmaj7 | D |"
    "G6 | Em7 | Cmaj7 | D | G6 | Em7 | Am7 D | G6 |";
  s.lead =
    // introduzione
    "r:8 /D5:8 | _E5:4 D5:4 B4:8 | /E5:6 G5:2 A5:4 G5:4 | ^A5:8 r:4 A4:2 B4:2 |"
    // A: violino
    "D5:3 E5:1 G5:2 E5:2 D5:4 B4:4 | /E5:4 G5:2 A5:2 G5:2 E5:2 D5:4 | E5:3 G5:1 A5:2 G5:2 E5:4 G5:4 |"
    "A5:6 _B5:2 A5:4 D5:4 |"
    "D5:3 E5:1 G5:2 A5:2 B5:4 A5:2 G5:2 | E6:4 D6:2 B5:2 A5:4 G5:4 | A5:2 G5:2 E5:2 G5:2 ^A5:4 D5:4 |"
    "G5:10 r:2 A4:2 B4:2 |"
    // A': flauto di bambu'
    "D5:2 D5:1 E5:1 G5:2 E5:2 D5:2 B4:2 D5:4 | E5:2 E5:1 G5:1 A5:2 G5:2 E5:2 D5:2 E5:4 |"
    "G5:3 A5:1 G5:2 E5:2 D5:2 E5:2 G5:4 | ~A5:8 B5:2 A5:2 F#5:2 E5:2 |"
    "D5:2 G5:2 A5:2 B5:2 D6:4 B5:2 A5:2 | E6:3 D6:1 B5:2 D6:2 E6:4 G5:4 |"
    "A5:2 B5:2 A5:2 G5:2 E5:2 G5:2 A5:4 | G5:8 r:8 |"
    // B: violino e flauto si rispondono
    "B4:4 E5:4 G5:3 A5:1 G5:4 | /B5:6 A5:2 G5:2 E5:2 D5:4 | A5:3 B5:1 D6:2 E6:2 D6:4 B5:4 | D6:3 B5:1 A5:4 D5:8 |"
    "G5:4 E5:2 G5:2 A5:4 B5:4 | ^E6:4 D6:2 B5:2 G5:4 E5:4 | D6:3 B5:1 A5:2 B5:2 D6:4 B5:4 | ~E6:8 B5:4 E5:4 |"
    // pausa
    "r:16 | r:16 | r:16 | r:12 A4:2 B4:2 |"
    // A''
    "D5:3 E5:1 G5:2 E5:2 D5:4 B4:4 | /E5:4 G5:2 A5:2 G5:2 E5:2 D5:4 | E5:3 G5:1 A5:2 G5:2 E5:4 G5:4 |"
    "A5:6 _B5:2 A5:4 D5:4 |"
    "D5:3 E5:1 G5:2 A5:2 B5:4 D6:4 | E6:4 D6:2 B5:2 A5:2 B5:2 G5:4 | A5:2 G5:2 E5:2 G5:2 A5:3 B5:1 A5:2 F#5:2 |"
    "G5:10 r:2 A4:2 B4:2 |";
  s.tc[T_LEAD] = tcfg(I_ERHU, 0.4f, 0.0f, 0.25f, 0.92f, 0);
  s.tc[T_LEAD].glide = 0.1f;
  s.tl2 = tcfg(I_DIZI, 0.36f, 0.12f, 0.25f, 0.88f, 0);
  s.tl2.glide = 0.06f;
  s.larr = ".... ........ xxxxxxxx ..xx..xx .... ........";
  s.tc[T_HARM] = tcfg(I_GUZHENG, 0.3f, -0.3f, 0.25f, 0.95f, 0);
  s.harm = ".... ........ oooooooo ........ .... oooooooo";
  s.tc[T_BASS] = tcfg(I_BASS_SOFT, 0.28f, 0.0f, 0.0f, 0.9f, 43);
  s.pat[0][0] = "1------. 5------.";
  s.pat[0][1] = "1-----.. 5---1-..";
  s.pat[0][2] = "1-..1-.. 1-..5-..";
  s.pat[0][3] = "1------. 1------.";      // due accordi nella battuta
  s.arr[0] = "..aa aaaaaada aaaaaada bbbbbbbb cccc aaaaaada";
  s.tc[T_ARP] = tcfg(I_GUZHENG, 0.27f, 0.25f, 0.3f, 0.95f, 55);
  s.tc[T_ARP].ring = 1;
  s.pat[3][0] = "0-1-2-3-4-3-2-1-";
  s.pat[3][1] = "0123 4567 8765 4321";     // glissato su e giu'
  s.pat[3][2] = "0-2-4-2-1-3-5-3-";
  s.arr[3] = "baab aaaaaaaa ........ cccccccc bbbb ........";
  s.tc[T_PAD] = tcfg(I_WARM, 0.1f, -0.2f, 0.15f, 1.0f, 52);
  s.pat[2][0] = "x---------------";
  s.arr[2] = ".... ........ ........ aaaaaaaa .... ........";
  // tamburo, gong piccolo, blocco di legno, piatti, gong grande, blocchi accordati, battente di legno
  const int dk[ND] = { I_TANGGU, I_GONG, I_BLOCK, I_BO, I_GONG_BIG, I_BLOCK, I_BOMBO_RIM };
  const float dv[ND] = { 0.3f, 0.28f, 0.4f, 0.22f, 0.35f, 0.38f, 0.3f };
  const float dp[ND] = { 0.0f, 0.2f, -0.25f, 0.3f, -0.1f, 0.1f, -0.15f };
  memcpy(s.dk, dk, sizeof(dk)); memcpy(s.dvol, dv, sizeof(dv)); memcpy(s.dpan, dp, sizeof(dp));
  s.choke = false;
  s.tom[0] = 1.0f; s.tom[1] = 0.8f; s.tom[2] = 0.62f;
  s.drum[0][DK_KICK] = "x... .... x.x. ...."; s.drum[0][DK_HAT] = "o.o. o.o. o.o. o.o."; s.drum[0][DK_SNARE] = ".... .... .... x...";
  s.drum[1][DK_KICK] = "x... .... x.x. ...."; s.drum[1][DK_HAT] = "o.o. o.o. o.o. o.o."; s.drum[1][DK_SNARE] = ".... .... .... x...";
  s.drum[1][DK_CLAP] = "..x. ..x. ..x. ..x.";
  s.drum[2][DK_KICK] = "x... .... .... ...."; s.drum[2][DK_TOM] = "1... 2... 1.1. 2..."; s.drum[2][DK_SNARE] = ".... .... .... x...";
  s.drum[3][DK_KICK] = "x.x. x... x.x. x..."; s.drum[3][DK_SNARE] = ".... x... .... x..."; s.drum[3][DK_OHAT] = ".... x... .... x...";
  s.drum[3][DK_TOM] = "1.2. 1.2. 1.2. 3.3.";
  s.drum[4][DK_CRASH] = "x... .... .... ...."; s.drum[4][DK_KICK] = ".... .... .... x.x.";
  s.drum[5][DK_KICK] = "x... x... rrrr RRRR"; s.drum[5][DK_OHAT] = ".... .... .... ..x.";
  s.drum[6][DK_KICK] = "x... .... x.x. ...."; s.drum[6][DK_HAT] = "o.o. o.o. o.o. o.o."; s.drum[6][DK_CRASH] = "x... .... .... ....";
  s.drum[6][DK_OHAT] = "x... .... .... ....";
  s.drum[7][DK_KICK] = "x.x. x.x. xxxx XXXX"; s.drum[7][DK_SNARE] = ".... x... .... x..."; s.drum[7][DK_OHAT] = "x... x... x.x. X...";
  s.drum[7][DK_TOM] = "1.2. 1.2. 3333 ....";
  s.darr = "eccf gaaaaaaf gbbbbbbf cccccccf dddh gbbbbbbf";
  s.dly_mix = 0.25f; s.dly_fb = 0.32f;
}

static void song_colosseo(SongSrc& s) {
  // Si minore (minore armonica: la diesis sul fa diesis), 142 BPM. Richiamo del corno sui timpani, A (fanfara del
  // corno sull'ostinato degli archi), A' (con i corni a terze e i battimani del pubblico), B (il coro canta, piu'
  // scuro), pausa "dell'arena" (tamburi, battimani, incudine), A'' (corni all'ottava e il coro). 40 battute,
  // ripresa dalla 5: teso ma allegro.
  src_defaults(s);
  s.name = "colosseo"; s.bpm = 142.0f; s.swing = 0.5f; s.loop = 4; s.key = 11; s.mode = M_HMIN; s.gain = 0.75f;
  s.chords =
    "Bm | Bm | G | F# |"
    "Bm | G | A | F# | Bm | G | Em F# | Bm |"
    "Bm | G | A | F# | Bm | G | Em F# | Bm |"
    "G | A | Bm | Bm | G | A | F# | F#7 |"
    "Bm | Bm | G | F# |"
    "Bm | G | A | F# | Bm | G | Em F# | Bm |";
  s.lead =
    // richiamo
    "r:8 F#4:1 F#4:1 F#4:2 B4:4 | D5:6 C#5:2 B4:4 F#4:4 | r:8 G4:1 G4:1 G4:2 D5:4 | C#5:8 A#4:4 F#4:4 |"
    // A
    "!B4:2 B4:1 B4:1 D5:2 F#5:6 E5:2 D5:2 | E5:3 D5:1 B4:2 G4:2 B4:4 D5:4 | C#5:2 C#5:1 C#5:1 E5:2 A5:6 G5:2 F#5:2 |"
    "F#5:4 E5:2 C#5:2 A#4:4 F#4:4 |"
    "!B4:2 B4:1 B4:1 D5:2 F#5:6 E5:2 D5:2 | G5:3 F#5:1 E5:2 D5:2 B4:4 G5:4 | E5:2 F#5:2 G5:2 E5:2 F#5:3 E5:1 C#5:2 A#4:2 |"
    "B4:12 r:4 |"
    // A'
    "!B4:2 B4:1 B4:1 D5:2 F#5:6 E5:2 D5:2 | E5:3 D5:1 B4:2 G4:2 B4:4 D5:4 | C#5:2 C#5:1 C#5:1 E5:2 A5:6 G5:2 F#5:2 |"
    "F#5:4 E5:2 C#5:2 A#4:4 F#4:4 |"
    "!B4:2 B4:1 B4:1 D5:2 F#5:4 A5:2 B5:4 | B5:3 A5:1 G5:2 F#5:2 G5:4 D5:4 | E5:2 G5:2 B5:2 G5:2 F#5:4 C#5:4 |"
    "B4:8 r:4 F#4:2 A#4:2 |"
    // B: il coro
    "D5:8 B4:8 | C#5:8 E5:8 | F#5:12 E5:4 | D5:16 | G5:8 F#5:4 E5:4 | E5:8 A5:8 | A#5:8 C#6:4 A#5:4 | F#5:16 |"
    // pausa dell'arena
    "r:16 | r:16 | r:8 G4:1 G4:1 G4:2 D5:4 | C#5:4 A#4:4 F#4:2 F#4:1 F#4:1 A#4:2 C#5:2 |"
    // A''
    "!B4:2 B4:1 B4:1 D5:2 F#5:6 E5:2 D5:2 | E5:3 D5:1 B4:2 G4:2 B4:4 D5:4 | C#5:2 C#5:1 C#5:1 E5:2 A5:6 G5:2 F#5:2 |"
    "F#5:4 E5:2 C#5:2 A#4:4 F#4:4 |"
    "!B4:2 B4:1 B4:1 D5:2 F#5:4 A5:2 B5:4 | B5:3 A5:1 G5:2 F#5:2 G5:4 D5:4 | E5:2 G5:2 B5:2 G5:2 F#5:4 C#5:4 |"
    "B4:8 r:4 F#4:2 A#4:2 |";
  s.tc[T_LEAD] = tcfg(I_BUCCINA, 0.42f, 0.0f, 0.2f, 0.85f, 0);
  s.tl2 = tcfg(I_CHOIR, 0.4f, 0.0f, 0.25f, 0.95f, 0);
  s.larr = ".... ........ ........ xxxxxxxx .... ........";
  s.tc[T_HARM] = tcfg(I_HORN, 0.3f, -0.3f, 0.15f, 0.85f, 0);
  s.harm = ".... ........ tttttttt ........ .... oooooooo";
  s.tc[T_BASS] = tcfg(I_TUBA, 0.3f, 0.0f, 0.0f, 0.85f, 40);
  s.pat[0][0] = "1--- ..1- 5--- ..1-";
  s.pat[0][1] = "1------- 5-------";
  s.pat[0][2] = "1-1- 1-1- 1-1- 1-1-";
  s.pat[0][3] = "1--- ..1- 1--- ..1-";      // due accordi nella battuta
  s.arr[0] = "aaaa aaaaaada aaaaaada bbbbbbbb cccc aaaaaada";
  s.tc[T_CHORD] = tcfg(I_HORN, 0.2f, 0.25f, 0.12f, 0.6f, 59);
  s.pat[1][0] = "x... ..x. .... x...";
  s.pat[1][1] = "x------- x-------";
  s.arr[1] = ".... ........ aaaaaaaa bbbbbbbb .... aaaaaaaa";
  s.tc[T_PAD] = tcfg(I_CHOIR, 0.14f, -0.2f, 0.2f, 1.0f, 54);
  s.pat[2][0] = "x---------------";
  s.arr[2] = ".... ........ ........ ........ aaaa aaaaaaaa";
  s.tc[T_ARP] = tcfg(I_STRINGS, 0.2f, 0.3f, 0.1f, 0.9f, 59);
  s.pat[3][0] = "0102 0201 0102 0201";      // ostinato degli archi
  s.pat[3][1] = "0-1-2-1-0-1-2-1-";
  s.pat[3][2] = "0-0-1-0-2-0-1-0-";
  s.arr[3] = "..cc cccccccc aaaaaaaa bbbbbbbb aaaa aaaaaaaa";
  // tamburo da guerra, rullante militare, tamburello, incudine, piatto, timpani (si, fa diesis, re), battimani
  const int dk[ND] = { I_WARDRUM, I_SNARE_MIL, I_RIQ, I_ANVIL, I_CYMBAL, I_TIMPANI, I_CLAP };
  const float dv[ND] = { 0.4f, 0.5f, 0.16f, 0.25f, 0.28f, 0.32f, 0.3f };
  const float dp[ND] = { 0.0f, 0.1f, 0.3f, -0.25f, -0.2f, -0.1f, 0.15f };
  memcpy(s.dk, dk, sizeof(dk)); memcpy(s.dvol, dv, sizeof(dv)); memcpy(s.dpan, dp, sizeof(dp));
  s.choke = false;
  s.tom[0] = 1.0f; s.tom[1] = 0.749f; s.tom[2] = 1.189f;
  s.drum[0][DK_KICK] = "x... ..x. x... ..x."; s.drum[0][DK_SNARE] = ".... x... .... x..."; s.drum[0][DK_TOM] = "1... .... 2... ....";
  s.drum[1][DK_KICK] = "x... ..x. x... ..x."; s.drum[1][DK_SNARE] = ".... x..o .... x..."; s.drum[1][DK_TOM] = "1... .... 2... ....";
  s.drum[1][DK_CLAP] = ".... x... .... x...";
  s.drum[2][DK_KICK] = "x... .... x... ...."; s.drum[2][DK_TOM] = "1... ..1. 2... ....";
  s.drum[3][DK_KICK] = "x.x. .... x.x. ...."; s.drum[3][DK_CLAP] = ".... x... .... x..."; s.drum[3][DK_OHAT] = "x... .... .... ....";
  s.drum[4][DK_KICK] = "x... .... .... ...."; s.drum[4][DK_TOM] = "1... .... .... ....";
  s.drum[5][DK_KICK] = "x... ..x. x... x..."; s.drum[5][DK_SNARE] = "..x. ..x. rrrr RRRR"; s.drum[5][DK_TOM] = "1... .... .... ....";
  s.drum[6][DK_KICK] = "x... ..x. x... ..x."; s.drum[6][DK_SNARE] = ".... x... .... x..."; s.drum[6][DK_TOM] = "1... .... 2... ....";
  s.drum[6][DK_CRASH] = "x... .... .... ...."; s.drum[6][DK_CLAP] = ".... x... .... x...";
  s.drum[7][DK_KICK] = "x... .... .... x..."; s.drum[7][DK_TOM] = "aaaa aaaa aaaa aaaa"; s.drum[7][DK_OHAT] = ".... .... .... x...";
  s.darr = "eeeh gaaaaaaf gbbbbbbf cccccccf dddh gbbbbbbf";
  s.dly_mix = 0.16f; s.dly_fb = 0.25f;
}

static void song_victory(SongSrc& s) {
  // Do maggiore, 120 BPM: fanfara di tre battute (corno con i corni a terze, campana, timpani), suona una volta.
  src_defaults(s);
  s.name = "vittoria"; s.bpm = 120.0f; s.swing = 0.5f; s.loop = -1; s.key = 0; s.mode = M_MAJ; s.gain = 1.25f;
  s.chords = "C | F G | C |";
  s.lead = "G4:1 G4:1 G4:1 r:1 C5:4 E5:2 G5:6 | A5:4 F5:4 B5:4 D6:4 | C6:16 |";
  s.tc[T_LEAD] = tcfg(I_BUCCINA, 0.45f, 0.0f, 0.25f, 0.92f, 0);
  s.tc[T_HARM] = tcfg(I_HORN, 0.32f, -0.25f, 0.2f, 0.9f, 0);
  s.harm = "ttt";
  s.tc[T_BASS] = tcfg(I_BASS_SOFT, 0.36f, 0.0f, 0.0f, 0.9f, 36);
  s.pat[0][0] = "1-.. 1-.. 1-.. 1-..";
  s.pat[0][1] = "1------- 1-------";
  s.pat[0][2] = "1---------------";
  s.arr[0] = "abc";
  s.tc[T_PAD] = tcfg(I_WARM, 0.16f, -0.15f, 0.2f, 1.0f, 55);
  s.pat[2][0] = "x---------------";
  s.arr[2] = "..a";
  s.tc[T_ARP] = tcfg(I_GLASS, 0.2f, 0.3f, 0.35f, 0.9f, 67);
  s.pat[3][0] = "01234567--------";
  s.arr[3] = "..a";
  const int dk[ND] = { I_KICK, I_SNARE_MIL, I_HAT, I_OHAT, I_CYMBAL, I_TIMPANI, I_CLAP };
  const float dv[ND] = { 0.5f, 0.4f, 0.2f, 0.15f, 0.3f, 0.45f, 0.3f };
  memcpy(s.dk, dk, sizeof(dk)); memcpy(s.dvol, dv, sizeof(dv));
  s.tom[0] = 1.059f; s.tom[1] = 0.794f; s.tom[2] = 0.707f;   // do, sol, fa
  s.drum[0][DK_TOM] = "1... .... .... ...."; s.drum[0][DK_SNARE] = ".... .... ..x. x...";
  s.drum[1][DK_TOM] = "3... .... 2... ...."; s.drum[1][DK_SNARE] = ".... .... rrrr RRRR";
  s.drum[2][DK_KICK] = "x... .... .... ...."; s.drum[2][DK_CRASH] = "x... .... .... ...."; s.drum[2][DK_TOM] = "1... .... .... ....";
  s.darr = "abc";
}

static SongSrc g_src;

static void build_songs(void) {
  void (*const fn[NMUSIC])(SongSrc&) = { 0, song_title, song_piramidi, song_chichenitza, song_muraglia, song_colosseo, song_victory };
  for (int i = 1; i < NMUSIC; ++i) {
    fn[i](g_src);
    build_song(g_song[i], g_src);
  }
}

// --- avvio ---------------------------------------------------------------------------------------

void audio_init(int rate) {
  memset(&G, 0, sizeof(G));
  memset(g_pl, 0, sizeof(g_pl));
  G.fs = (float)(rate >= 8000 && rate <= 192000 ? rate : 48000);
  G.ifs = 1.0f / G.fs;
  for (int i = 0; i <= BLK; ++i) { G.ii[i] = (float)i; G.tt[i] = 0.5f * (float)i * (float)(i - 1); }
  G.rng = RNG_SEED;
  G.vol_m = 7; G.vol_s = 8;
  G.vm = vcurve(7); G.vs = vcurve(8);
  G.duck_g = G.auto_g = 1.0f;
  G.gm = G.vm; G.gs = G.vs;
  const float bt = (float)BLK * G.ifs;
  G.k_fast = 1.0f - expf(-bt / 0.05f);
  G.k_duck = 1.0f - expf(-bt / 0.12f);
  G.k_auto_dn = 1.0f - expf(-bt / 0.05f);
  G.k_auto_up = 1.0f - expf(-bt / 0.4f);
  G.k_rel = 1.0f - expf(-bt / 0.2f);
  G.dc_r = expf(-6.2831853f * 20.0f * G.ifs);
  G.lim_g = 1.0f;
  G.out_pos = BLK;
  G.mus_cur = MUS_NONE;
  G.st.lim_min = 1.0f;
  for (int p = 0; p < NPLAYER; ++p) g_pl[p].pending = -1;
  for (int i = 0; i < NSOUNDS; ++i) G.step_last[i] = 0x80000000u;   // molto tempo fa
  define_sounds();
  define_instruments();
  calibrate(true);
  build_songs();
  G.ready = true;
}

// --- testo dei brani (prove) ------------------------------------------------------------------------

static int note_name(int m, char* b) {
  static const char* const N[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  const int oct = m < 0 ? 0 : (m > 127 ? 9 : m / 12 - 1);
  return snprintf(b, 16, "%s%d", N[pcm(m)], oct);
}

int audio_music_text(int id, char* buf, int size) {
  if (!buf || size <= 0) return 0;
  buf[0] = 0;
  if (id <= MUS_NONE || id >= NMUSIC || !G.ready) return 0;
  const Song& s = g_song[id];
  int n = 0, notes = 0, strong = 0, strong_ok = 0, lo = 127, hi = 0;
  for (int gs = 0; gs < s.nbars * 16; ++gs) {
    if (!s.note[gs]) continue;
    notes++;
    lo = s.note[gs] < lo ? s.note[gs] : lo;
    hi = s.note[gs] > hi ? s.note[gs] : hi;
    if (gs % 4 == 0) { strong++; if (chord_has(s.ch[s.cat[gs]], s.note[gs])) strong_ok++; }
  }
  char a[16], b[16], lp[32];
  note_name(lo, a);
  note_name(hi, b);
  if (s.loop < 0) snprintf(lp, sizeof(lp), "nessuna");
  else snprintf(lp, sizeof(lp), "dalla battuta %d", s.loop + 1);
  n += snprintf(buf + n, (size_t)(size - n), "%s: %.0f BPM, swing %.2f, %d battute, ripresa %s, %d note da %s a %s, "
                "sui tempi %d su %d nell'accordo\n", s.name, (double)s.bpm, (double)s.swing, s.nbars, lp, notes, a, b,
                strong_ok, strong);
  if (G.nerr && n < size) n += snprintf(buf + n, (size_t)(size - n), "%s", G.err);
  // per battuta: numero, strumento della melodia (1 o 2), seconda voce (- spenta, t armonizzata, o i semitoni)
  for (int bar = 0; bar < s.nbars && n < size - 300; ++bar) {
    char hv[8];
    if (s.hiv[bar] == HV_OFF) snprintf(hv, sizeof(hv), "-");
    else if (s.hiv[bar] == HV_HARM) snprintf(hv, sizeof(hv), "t");
    else snprintf(hv, sizeof(hv), "%d", s.hiv[bar]);
    n += snprintf(buf + n, (size_t)(size - n), "%2d %c %3s |", bar + 1, s.larr[bar] == 1 ? '2' : '1', hv);
    int last = -1;
    for (int st = 0; st < 16; ++st) {
      const int gs = bar * 16 + st;
      if (s.cat[gs] != last) { last = s.cat[gs]; n += snprintf(buf + n, (size_t)(size - n), " [%s]", s.ch[last].name); }
      if (s.note[gs]) {
        char nb[16], ob[8];
        int on = 0;
        static const char OC[6] = { '_', '/', '^', '~', '!', '\'' };
        for (int k = 0; k < 6; ++k)
          if (s.orn[gs] & (1 << k)) ob[on++] = OC[k];
        ob[on] = 0;
        note_name(s.note[gs], nb);
        const bool out = st % 4 == 0 && !chord_has(s.ch[s.cat[gs]], s.note[gs]);
        n += snprintf(buf + n, (size_t)(size - n), " %s%s:%d%s", ob, nb, s.nlen[gs], out ? "*" : "");
      }
    }
    n += snprintf(buf + n, (size_t)(size - n), "\n");
  }
  return n < size ? n : size - 1;
}
