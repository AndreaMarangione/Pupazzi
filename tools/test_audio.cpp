// Prove del sonoro senza console: ogni effetto e ogni brano in un WAV con le misure, controllo del punto
// di ripresa dei brani, una battaglia affollata di 60 s con musica e tempi di calcolo (con il bilanciamento tra
// musica ed effetti), casi limite.
//   g++ -std=c++17 -O2 -Wall -Wextra -Isrc tools/test_audio.cpp src/audio.cpp -o build/native/test_audio
//   ./build/native/test_audio wav
// Esce con errore se trova NaN o infiniti, campioni al fondo scala, continua oltre l'1%, errori nei brani, brani a
// ripetizione fuori dalla finestra di livello (RMS tra -28 e -22 dBFS con la musica a 7) o la musica di una mappa
// che nella battaglia non sta almeno 3 LU sotto gli effetti.
#include "audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const int RATE = 48000, FRAME = 800;
static int g_fail = 0;

// Tempo di CPU di questo thread: non conta i momenti in cui la macchina fa altro (con altri programmi accesi il tempo
// dell'orologio salta di millisecondi).
static double now_ms(void) {
  timespec ts;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
  return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

struct Buf {
  int16_t* s = nullptr;
  int n = 0, cap = 0;   // campioni per canale
  void reserve(int frames) { if (frames > cap) { s = (int16_t*)realloc(s, (size_t)frames * 4); cap = frames; } }
  int16_t* add(int frames) { reserve(n + frames > cap ? (n + frames) * 2 : cap); int16_t* p = s + 2 * n; n += frames; return p; }
};

struct Timing {
  double sum = 0, max = 0, v[8192];
  int n = 0, at = 0;
  double pct(double q) const {   // percentile dei tempi
    static double t[8192];
    const int m = n < 8192 ? n : 8192;
    memcpy(t, v, sizeof(double) * (size_t)m);
    qsort(t, (size_t)m, sizeof(double), [](const void* a, const void* b) {
      const double x = *(const double*)a, y = *(const double*)b;
      return x < y ? -1 : (x > y);
    });
    return m ? t[(int)(q * (m - 1))] : 0;
  }
};

static void render(Buf& b, int frames, Timing* t = nullptr) {
  while (frames > 0) {
    const int f = frames < FRAME ? frames : FRAME;
    int16_t* p = b.add(f);
    const double t0 = now_ms();
    audio_render(p, f);
    const double dt = now_ms() - t0;
    if (t) { if (t->n < 8192) t->v[t->n] = dt; if (dt > t->max) { t->max = dt; t->at = t->n; } t->sum += dt; t->n++; }
    frames -= f;
  }
}

static void write_wav(const char* dir, const char* name, const Buf& b) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.wav", dir, name);
  FILE* f = fopen(path, "wb");
  if (!f) { printf("ERRORE: non riesco a scrivere %s\n", path); g_fail = 1; return; }
  const unsigned data = (unsigned)b.n * 4u, riff = 36u + data, rate = RATE, brate = RATE * 4u, fmt = 16;
  const unsigned short pcm = 1, ch = 2, align = 4, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
  fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f);
  fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  fwrite(b.s, 4, (size_t)b.n, f);
  fclose(f);
}

// Butterworth del 4o ordine (due biquad): energia sopra i 6 kHz (suoni stridenti) e sotto i 200 Hz (che
// l'altoparlantino della console non riproduce).
struct Biquad {
  double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  Biquad(double fc, double q, bool low = false) {
    const double w = 2 * M_PI * fc / RATE, c = cos(w), al = sin(w) / (2 * q), a0 = 1 + al;
    if (low) { b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; }
    else { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; }
    b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
  }
  double run(double x) {
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    return y;
  }
};

struct Meas { double dur, peak, rms, dc, hf, frms, lf, lufs; int full; };

// Sonorita' alla BS.1770 (filtro K a 48 kHz, senza soglie): LUFS = -0,691 + 10 log10 (somma dei due canali).
static double loudness(const Buf& b, int from = 0, int to = -1) {
  if (to < 0) to = b.n;
  if (to <= from) return -200;
  struct Bq { double b0, b1, b2, a1, a2, x1, x2, y1, y2; double run(double x) {
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; } };
  double e = 0;
  for (int c = 0; c < 2; ++c) {
    Bq sh = { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585, 0, 0, 0, 0 };
    Bq hp = { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621, 0, 0, 0, 0 };
    for (int i = from; i < to; ++i) { const double y = hp.run(sh.run(b.s[2 * i + c] / 32768.0)); e += y * y; }
  }
  return e > 0 ? -0.691 + 10 * log10(e / (to - from)) : -200;
}

static Meas measure(const Buf& b, int from = 0, int to = -1) {
  Meas m{};
  if (to < 0) to = b.n;
  int last = from - 1;
  double sum = 0, e = 0, eh = 0, el = 0, pk = 0;
  Biquad h[2][2] = { { Biquad(6000, 0.5412), Biquad(6000, 1.3066) }, { Biquad(6000, 0.5412), Biquad(6000, 1.3066) } };
  Biquad l[2][2] = { { Biquad(200, 0.5412, true), Biquad(200, 1.3066, true) },
                     { Biquad(200, 0.5412, true), Biquad(200, 1.3066, true) } };
  for (int i = from; i < to; ++i)
    for (int c = 0; c < 2; ++c) {
      const int v = b.s[2 * i + c];
      const double x = v / 32768.0;
      if (abs(v) > 10) last = i;
      if (abs(v) >= 32767) m.full++;
      pk = fabs(x) > pk ? fabs(x) : pk;
      sum += x;
      const double y = h[c][1].run(h[c][0].run(x)), z = l[c][1].run(l[c][0].run(x));
      e += x * x;
      eh += y * y;
      el += z * z;
    }
  const int n = to - from, act = last - from + 1;
  m.dur = act / (double)RATE;
  double ea = 0, ed = 0;
  for (int i = from; i <= last; ++i)
    for (int c = 0; c < 2; ++c) {
      const double x = b.s[2 * i + c] / 32768.0, xp = i > from ? b.s[2 * (i - 1) + c] / 32768.0 : 0.0;
      ea += x * x;
      ed += (x - xp) * (x - xp);
    }
  m.frms = ea > 0 ? RATE / (2 * M_PI) * sqrt(ed / ea) : 0;   // "frequenza media" (dalla derivata): quanto e' brillante
  m.peak = pk > 0 ? 20 * log10(pk) : -200;
  m.rms = act > 0 && ea > 0 ? 10 * log10(ea / (2.0 * act)) : -200;
  m.dc = n > 0 ? 100.0 * fabs(sum / (2.0 * n)) : 0;
  m.hf = e > 0 ? 100.0 * eh / e : 0;
  m.lf = e > 0 ? 100.0 * el / e : 0;
  m.lufs = loudness(b, from, to);
  return m;
}

static bool check(const char* what, const Meas& m) {
  bool ok = true;
  if (m.full) { printf("ERRORE %s: %d campioni al fondo scala\n", what, m.full); ok = false; }
  if (m.dc > 1.0) { printf("ERRORE %s: continua %.2f%%\n", what, m.dc); ok = false; }
  if (!ok) g_fail = 1;
  return ok;
}

static void silence(int frames) { Buf b; render(b, frames); free(b.s); }

// --- effetti --------------------------------------------------------------------------------------

static void test_sounds(const char* dir) {
  printf("\n== Effetti, ognuno da solo (effetti a 8, al centro, vol 1) ==\n");
  printf("%-20s %7s %7s %7s %7s %7s %7s %6s %5s %5s\n", "suono", "durata", "picco", "RMS", "contin.", ">6kHz", "<200Hz", "f_med",
         "voci", "fine");
  printf("%-20s %7s %7s %7s %7s %7s %7s %6s %5s %5s\n", "", "s", "dBFS", "dBFS", "%", "%", "%", "Hz", "max", "env");
  for (int id = 1; id < NSOUNDS; ++id) {
    audio_reset();
    silence(RATE / 2);
    audio_clear_stats();
    Buf b;
    audio_play(id, 0.0f, 1.0f);
    AudioStats st;
    int t = 0;
    // la fiamma si tiene premuta per 1 s, come nel gioco (una richiesta ogni 5 fotogrammi)
    do {
      if (id == SND_FLAME && t < 60 && t % 5 == 0) audio_play(id, 0.0f, 1.0f);
      render(b, FRAME);
      audio_get_stats(&st);
      t++;
    } while ((st.voices > 0 || t < 3) && t < 60 * 6);
    render(b, RATE / 2);
    audio_get_stats(&st);
    const Meas m = measure(b);
    check(audio_sound_name(id), m);
    if (st.bad) { printf("ERRORE %s: %u valori non validi\n", audio_sound_name(id), st.bad); g_fail = 1; }
    if (st.voices > 0) { printf("ERRORE %s: la voce non si spegne\n", audio_sound_name(id)); g_fail = 1; }
    printf("%-20s %7.3f %7.1f %7.1f %7.3f %7.2f %7.1f %6.0f %5d %5.3f\n", audio_sound_name(id), m.dur, m.peak, m.rms, m.dc, m.hf,
           m.lf, m.frms, st.voices_peak, (double)st.end_max);
    char name[64];
    snprintf(name, sizeof(name), "sfx_%02d_%s", id, audio_sound_name(id));
    write_wav(dir, name, b);
    free(b.s);
  }
}

// --- brani ----------------------------------------------------------------------------------------

static void test_music(const char* dir) {
  printf("\n== Brani (musica a 7) ==\n");
  static char text[65536];
  for (int id = 1; id < NMUSIC; ++id) {
    audio_reset();
    silence(RATE);
    audio_clear_stats();
    float loop_s = 0;
    const float len_s = audio_music_length(id, &loop_s);
    audio_music(id);
    Buf b;
    Timing tm;
    // 60 s, e comunque oltre il punto di ripresa
    const int frames = loop_s < 0 ? (int)((len_s + 3.0f) * RATE)
                                  : (len_s + 5.0f > 60.0f ? (int)((len_s + 5.0f) * RATE) : 60 * RATE);
    render(b, frames, &tm);
    AudioStats st;
    audio_get_stats(&st);
    int onset = 0;
    while (onset < b.n && b.s[2 * onset] == 0 && b.s[2 * onset + 1] == 0) onset++;
    const Meas m = measure(b, onset);
    check(audio_music_name(id), m);
    if (st.bad) { printf("ERRORE %s: %u valori non validi\n", audio_music_name(id), st.bad); g_fail = 1; }
    printf("%-10s durata %6.2f s (ripresa %s), suono fino a %6.2f s, picco %6.1f dBFS, RMS %6.1f dBFS, %.1f LUFS, continua %.3f%%, "
           ">6kHz %.2f%%, <200Hz %.1f%%\n", audio_music_name(id), len_s, loop_s < 0 ? "nessuna" : "si'", m.dur, m.peak, m.rms, m.lufs,
           m.dc, m.hf, m.lf);
    // i brani delle mappe stanno sotto gli effetti (0.5: la musica della 0.4 era a -20 dBFS e copriva il gioco)
    if (loop_s >= 0 && (m.rms < -28.0 || m.rms > -22.0)) {
      printf("ERRORE %s: livello fuori dalla finestra (RMS tra -28 e -22 dBFS con la musica a 7)\n", audio_music_name(id));
      g_fail = 1;
    }
    printf("           tempo per fotogramma di 800 campioni: medio %.4f ms, 99%% sotto %.4f ms, massimo %.4f ms (fotogramma %d); "
           "voci alla fine %d\n", tm.sum / tm.n, tm.pct(0.99), tm.max, tm.at, st.music_voices);
    const int n = audio_music_text(id, text, sizeof(text));
    int nbars = 0;
    { float bpm, sw; const char* c = strchr(text, ':'); if (c) sscanf(c + 1, " %f BPM, swing %f, %d battute", &bpm, &sw, &nbars); }
    if (loop_s >= 0 && nbars > 1) {
      // Punto di ripresa. (1) Salto massimo tra campioni vicini nei 10 ms attorno alla ripresa, rispetto allo stesso
      // valore ai cambi di battuta del primo giro (li' la musica e' continua per costruzione). (2) Livello nei 250 ms
      // prima e dopo. (3) Inviluppo (RMS a finestre di 20 ms) dei 4 s dopo la ripresa contro quelli dell'inizio: le fasi
      // degli oscillatori non ripartono da zero, quindi si confronta l'andamento, non i campioni.
      const int seam = onset + (int)lround((double)len_s * RATE), lsamp = (int)lround((double)loop_s * RATE);
      const double bar = (double)len_s * RATE / nbars;
      auto jump = [&](int at) {
        int d = 0;
        for (int i = at - 480; i < at + 480; ++i)
          for (int c = 0; c < 2; ++c) { const int x = abs(b.s[2 * i + c] - b.s[2 * (i - 1) + c]); d = x > d ? x : d; }
        return d;
      };
      auto rms = [&](int a, int z) {
        double e = 0;
        for (int i = a; i < z; ++i)
          for (int c = 0; c < 2; ++c) { const double x = b.s[2 * i + c]; e += x * x; }
        return sqrt(e / (2.0 * (z - a)) + 1e-9);
      };
      if (seam + RATE * 4 < b.n) {
        int ref = 0;
        for (int k = 1; k < nbars; ++k) { const int j = jump(onset + (int)lround(k * bar)); ref = j > ref ? j : ref; }
        const int dj = jump(seam);
        const double lv = 20 * log10(rms(seam, seam + RATE / 4) / rms(seam - RATE / 4, seam));
        double sxy = 0, sxx = 0, syy = 0, mx = 0, my = 0;
        const int W = RATE / 50, NW = 4 * 50;
        static double ex[NW], ey[NW];
        for (int w = 0; w < NW; ++w) {
          ex[w] = rms(onset + lsamp + w * W, onset + lsamp + (w + 1) * W);
          ey[w] = rms(seam + w * W, seam + (w + 1) * W);
          mx += ex[w]; my += ey[w];
        }
        mx /= NW; my /= NW;
        for (int w = 0; w < NW; ++w) {
          sxy += (ex[w] - mx) * (ey[w] - my); sxx += (ex[w] - mx) * (ex[w] - mx); syy += (ey[w] - my) * (ey[w] - my);
        }
        const double corr = sxy / sqrt(sxx * syy + 1e-9);
        const bool ok = dj <= ref * 1.25 && fabs(lv) < 6.0 && corr > 0.9;
        printf("           ripresa a %.2f s: salto %d (ai cambi di battuta fino a %d), livello %+.1f dB, "
               "inviluppo come all'inizio %.3f: %s\n", seam / (double)RATE, dj, ref, lv, corr, ok ? "ok" : "DA CONTROLLARE");
        if (!ok) g_fail = 1;
      }
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/musica_%s.txt", dir, audio_music_name(id));
    if (FILE* f = fopen(path, "w")) { fwrite(text, 1, (size_t)n, f); fclose(f); }
    const char* nl = strchr(text, '\n');
    printf("           %.*s\n", nl ? (int)(nl - text) : n, text);
    char name[64];
    snprintf(name, sizeof(name), "musica_%s", audio_music_name(id));
    write_wav(dir, name, b);
    free(b.s);
  }
  AudioStats st;
  audio_get_stats(&st);
  if (st.parse_errors) {
    printf("ERRORE: %d errori nei testi dei brani (vedi i file musica_*.txt)\n", st.parse_errors);
    g_fail = 1;
  }
}

// --- battaglia affollata ------------------------------------------------------------------------------

static unsigned g_r = 12345;
static float frand(void) { g_r = g_r * 1664525u + 1013904223u; return (float)(g_r >> 8) / 16777216.0f; }
static bool chance(float per_s) { return frand() < per_s / 60.0f; }

struct Delayed { int frame, id; float pan; };

// mode: 0 musica ed effetti (a 7 e 8), 1 solo musica, 2 solo effetti; stessa sequenza di richieste ogni volta
static Buf run_battle(int mode, Timing* tmp, int* nplays, int song = MUS_PIRAMIDI) {
  audio_reset();
  silence(RATE);
  audio_set_volume(mode == 2 ? 0 : 7, mode == 1 ? 0 : 8);
  audio_clear_stats();
  audio_music(song);
  g_r = 12345;
  Buf b;
  Timing& tm = *tmp;
  Delayed q[256];
  int nq = 0, plays = 0;
  float pan[4] = { -0.5f, -0.1f, 0.2f, 0.6f };
  int fire[4] = { 0, 0, 0, 0 }, cool[4] = { 0, 30, 50, 10 };
  auto later = [&](int f, int id, float p) { if (nq < 256) q[nq++] = Delayed{ f, id, p }; };
  auto play = [&](int id, float p, float v) { audio_play(id, p, v); plays++; };
  for (int f = 0; f < 60 * 60; ++f) {
    for (int k = 0; k < nq;) {
      if (q[k].frame <= f) { play(q[k].id, q[k].pan, 0.9f); q[k] = q[--nq]; } else ++k;
    }
    for (int p = 0; p < 4; ++p) {
      pan[p] += (frand() - 0.5f) * 0.04f;
      pan[p] = pan[p] < -0.9f ? -0.9f : (pan[p] > 0.9f ? 0.9f : pan[p]);
      // raffiche di 1-3 s e pause di 0,5-1,5 s
      if (fire[p] > 0) fire[p]--;
      else if (cool[p] > 0) cool[p]--;
      else { fire[p] = 60 + (int)(frand() * 120); cool[p] = 30 + (int)(frand() * 60); }
      if (fire[p] <= 0) continue;
      const float v = 0.8f + 0.2f * frand();
      switch (p) {   // ASTRO a raffiche di tre colpi, MOCHI cristalli, MARZIO ghiaccio secco, ZOLFO fiamma
      case 0: if (f % 9 == 0) { play(SND_BULLET, pan[p], v); later(f + 4, SND_BULLET, pan[p]); later(f + 8, SND_BULLET, pan[p]); } break;
      case 1: if (f % 15 == 0) play(SND_SHARD, pan[p], v); break;
      case 2: if (f % 12 == 0) play(SND_FROST, pan[p], v); break;
      default: if (f % 5 == 0) play(SND_FLAME, pan[p], v); break;
      }
    }
    const float rp = frand() * 1.6f - 0.8f, rv = 0.5f + 0.5f * frand();
    if (chance(9.0f)) play(SND_HIT, frand() * 1.6f - 0.8f, rv);
    if (chance(0.9f)) play(SND_DEFEAT, rp, rv);
    if (chance(0.25f)) { play(SND_BOMB_DROP, rp, rv); later(f + 180, SND_EXPLODE, rp); }
    if (chance(0.3f)) play(SND_EXPLODE_SMALL, rp, rv);
    if (chance(0.15f)) play(frand() < 0.5f ? SND_PICK_TALISMAN : SND_PICK_BOMB, rp, 1.0f);
    if (chance(0.35f)) play(SND_SCORPION, rp, rv);
    if (chance(0.5f)) { play(SND_BOW, rp, rv); later(f + 55, SND_ARROW_LAND, rp * 0.8f); }   // la freccia cade dopo 0,9 s
    if (chance(0.7f)) play(SND_SCARAB, rp, rv);
    if (chance(0.1f)) play(SND_WARRIOR, rp, rv);
    if (chance(0.5f)) play(SND_HURT, rp, 1.0f);
    if (chance(2.0f)) { const float jp = pan[(int)(frand() * 4) & 3]; play(SND_JUMP, jp, 1.0f); later(f + 30, SND_LAND, jp); }
    if (chance(0.15f)) play(SND_SPECIAL_READY, rp, 1.0f);
    if (chance(0.3f)) play(frand() < 0.6f ? SND_CRATE_HIT : (frand() < 0.5f ? SND_CRATE_BREAK : SND_CAPSULE_HIT), rp, rv);
    if (chance(0.12f)) {
      switch ((int)(frand() * 4)) {
      case 0: play(SND_ROCKET, pan[0], 1.0f); break;
      case 1: play(SND_LEAP, pan[1], 1.0f); later(f + 40, SND_SLAM, pan[1]); break;
      case 2: play(SND_BLIZZARD, pan[2], 1.0f); break;
      default:
        play(SND_ERUPT, pan[3], 1.0f);
        for (int k = 0; k < 5; ++k) {
          later(f + 9 * k, SND_LAVA, pan[3]);
          later(f + 9 * k + 25, SND_EXPLODE_SMALL, pan[3] + 0.1f * k);
        }
        break;
      }
    }
    if (f == 20 * 60 || f == 45 * 60) {   // pausa: menu con qualche spostamento
      play(SND_PAUSE, 0, 1); audio_duck(true);
      for (int k = 1; k <= 3; ++k) later(f + 15 * k, SND_UI_MOVE, 0);
      later(f + 70, SND_UI_OK, 0);
    }
    if (f == 20 * 60 + 75 || f == 45 * 60 + 75) audio_duck(false);
    if (f == 30 * 60) play(SND_ZONE_CLEAR, 0, 1);
    if (f == 31 * 60) { play(SND_WAVE, 0, 1); play(SND_CARD_OPEN, 0, 1); later(f + 50, SND_CARD_PICK, 0); }
    if (f >= 40 * 60 && f < 50 * 60 && f % 170 == 0) play(SND_HELP, 0.3f, 0.8f);   // l'esploratore chiama aiuto
    if (f == 50 * 60) { play(SND_CRATE_BREAK, 0.3f, 1); play(SND_RESCUE, 0.3f, 1); }
    if (f == 25 * 60)   // quattro scorpioni si fermano quasi insieme
      for (int k = 0; k < 4; ++k) later(f + 7 * k, SND_SCORPION, -0.6f + 0.4f * (float)k);
    if (f == 35 * 60) play(SND_SPLASH, 0.2f, 1.0f);   // un giocatore cade nel cenote
    // il gladiatore, due volte per 10 s: cammina (un passo ogni 27 fotogrammi), fendente (avviso, colpo 33 fotogrammi
    // dopo), carica (preparazione di 48 fotogrammi, corsa con un passo ogni 9, poi il muro), cammina ancora
    for (int w = 0; w < 2; ++w) {
      const int t = f - (w ? 38 : 8) * 60;
      if (t < 0 || t >= 10 * 60) continue;
      const float bp = 0.15f - 0.3f * (float)w;
      if (t == 180) play(SND_BOSS_SWEEP_WARN, bp, 1.0f);
      else if (t == 213) play(SND_BOSS_SWEEP, bp, 1.0f);
      else if (t == 360) play(SND_BOSS_RUSH, bp, 1.0f);
      else if (t >= 408 && t < 483 && (t - 408) % 9 == 0) play(SND_BOSS_STEP, bp + 0.004f * (float)(t - 408), 1.0f);
      else if (t == 483) play(SND_BOSS_IMPACT, bp + 0.3f, 1.0f);
      else if ((t < 170 || (t > 260 && t < 350) || t > 540) && t % 27 == 0) play(SND_BOSS_STEP, bp, 0.9f);
    }
    if (f == 55 * 60) play(SND_WIPE, 0, 1);
    render(b, FRAME, &tm);
  }
  *nplays = plays;
  audio_set_volume(7, 8);
  return b;
}

static void test_battle(const char* dir) {
  printf("\n== Battaglia affollata: 60 s, le piramidi, 4 giocatori (ASTRO, MOCHI, MARZIO, ZOLFO) ==\n");
  Timing tm, tm2;
  int plays = 0, p2 = 0;
  // bilanciamento: la stessa battaglia con la sola musica e con i soli effetti
  Buf bm = run_battle(1, &tm2, &p2), bs = run_battle(2, &tm2, &p2);
  const double lm = loudness(bm), ls = loudness(bs);
  write_wav(dir, "battaglia_musica", bm);
  write_wav(dir, "battaglia_effetti", bs);
  free(bm.s); free(bs.s);
  Buf b = run_battle(0, &tm, &plays);
  AudioStats st;
  audio_get_stats(&st);
  const Meas m = measure(b);
  check("battaglia", m);
  if (st.bad) { printf("ERRORE battaglia: %u valori non validi\n", st.bad); g_fail = 1; }
  printf("richieste %d (accettate %u, fuse %u), voci rubate %u, scartate %u, voci degli effetti al massimo %d\n", plays, st.plays,
         st.merged, st.stolen, st.dropped, st.voices_peak);
  printf("picco %.1f dBFS, RMS %.1f dBFS, >6kHz %.2f%%, continua %.3f%%; limitatore attivo nel %.2f%% dei blocchi "
         "(al massimo %.1f dB)\n",
         m.peak, m.rms, m.hf, m.dc, 100.0 * st.limited / (st.blocks ? st.blocks : 1), 20 * log10((double)st.lim_min));
  printf("tempo per fotogramma di 800 campioni: medio %.4f ms, 99%% sotto %.4f ms, massimo %.4f ms (fotogramma %d di %d)\n",
         tm.sum / tm.n, tm.pct(0.99), tm.max, tm.at, tm.n);
  printf("inviluppo piu' alto allo spegnimento di una voce: %.5f\n", (double)st.end_max);
  printf("bilanciamento: musica da sola %.1f LUFS, effetti da soli %.1f LUFS, insieme %.1f LUFS: la musica sta %.1f LU sotto gli "
         "effetti\n", lm, ls, m.lufs, ls - lm);
  if (ls - lm < 3.0) { printf("ERRORE battaglia: la musica copre gli effetti (meno di 3 LU sotto)\n"); g_fail = 1; }
  write_wav(dir, "battaglia", b);
  free(b.s);
  // le altre mappe: la stessa battaglia con la loro musica, contro gli stessi effetti
  printf("bilanciamento delle altre mappe (musica da sola, LU sotto gli effetti):");
  const int other[3] = { MUS_CHICHENITZA, MUS_MURAGLIA, MUS_COLOSSEO };
  for (int k = 0; k < 3; ++k) {
    Buf bo = run_battle(1, &tm2, &p2, other[k]);
    const double lo = loudness(bo);
    printf(" %s %.1f LUFS (%.1f)", audio_music_name(other[k]), lo, ls - lo);
    if (ls - lo < 3.0) { printf("\nERRORE battaglia: la musica di %s copre gli effetti (meno di 3 LU sotto)\n", audio_music_name(other[k])); g_fail = 1; }
    free(bo.s);
  }
  printf("\n");
}

// --- dissolvenze e casi limite -------------------------------------------------------------------------

static void test_edges(const char* dir) {
  printf("\n== Cambi di brano, volumi e casi limite ==\n");
  audio_reset();
  silence(RATE);
  audio_clear_stats();
  Buf b;
  audio_music(MUS_TITLE);
  render(b, 3 * RATE);
  audio_music(MUS_TITLE);              // stesso brano: continua
  render(b, RATE);
  audio_music(MUS_PIRAMIDI);
  render(b, RATE / 10);
  audio_music(MUS_COLOSSEO);               // cambio mentre la dissolvenza e' in corso
  render(b, RATE / 10);
  audio_music(MUS_PIRAMIDI);
  render(b, 3 * RATE);
  audio_duck(true);
  render(b, RATE);
  audio_duck(false);
  audio_set_volume(10, 10);
  for (int k = 0; k < 6; ++k) audio_play(SND_EXPLODE, -1.0f + 0.4f * k, 1.0f);   // sei esplosioni insieme al massimo
  render(b, RATE);
  for (int k = 0; k < 200; ++k) audio_play(1 + k % (NSOUNDS - 1), (k % 7) / 3.5f - 1.0f, 1.0f);   // 200 richieste in un fotogramma
  render(b, RATE);
  audio_play(-5, 0, 1); audio_play(999, 0, 1); audio_play(SND_HIT, NAN, NAN); audio_play(SND_HIT, 5.0f, 7.0f);
  audio_play(SND_HIT, INFINITY, 1.0f); audio_play(SND_HIT, 0.0f, -INFINITY);
  audio_music(77); audio_music(-1); audio_set_volume(-3, 99);
  audio_render(nullptr, 800);
  { int16_t x[2] = { 0, 0 }; audio_render(x, 0); audio_render(x, -4); }
  for (int k = 0; k < 50; ++k) { int16_t* p = b.add(1 + k % 37); audio_render(p, 1 + k % 37); }   // dimensioni strane
  audio_set_volume(7, 8);
  audio_music(MUS_VICTORY);
  render(b, 5 * RATE);
  audio_music(MUS_VICTORY);            // finita: non riparte
  render(b, RATE);
  audio_music(MUS_COLOSSEO);
  render(b, 2 * RATE);
  audio_set_volume(0, 0);
  render(b, RATE / 2);
  audio_set_volume(7, 8);
  audio_music(MUS_NONE);
  render(b, RATE);
  audio_reset();
  render(b, RATE / 2);
  AudioStats st;
  audio_get_stats(&st);
  const Meas m = measure(b);
  check("casi limite", m);
  if (st.bad) { printf("ERRORE casi limite: %u valori non validi\n", st.bad); g_fail = 1; }
  printf("picco %.1f dBFS, limitatore al massimo %.1f dB, voci al massimo %d (limite 24), rubate %u, scartate %u, "
         "inviluppo allo spegnimento %.5f, alla fine voci %d + musica %d\n", m.peak, 20 * log10((double)st.lim_min),
         st.voices_peak, st.stolen, st.dropped, (double)st.end_max, st.voices, st.music_voices);
  if (st.voices_peak > 24) { printf("ERRORE: piu' di 24 voci\n"); g_fail = 1; }
  write_wav(dir, "casi_limite", b);
  free(b.s);
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : ".";
  const double t0 = now_ms();
  audio_init(RATE);
  const double t1 = now_ms();
  AudioStats st;
  audio_get_stats(&st);
  printf("audio_init: %.2f ms di CPU (taratura degli effetti: %.1f s di suono sintetizzato)\n", t1 - t0, st.init_samples / (double)RATE);
  // la tabella delle tarature in audio.cpp: ogni suono si ritara da zero e si confronta
  {
    static char tab[16384];
    const double c0 = now_ms();
    const int diff = audio_calibration_table(tab, sizeof(tab));
    const double c1 = now_ms();
    audio_get_stats(&st);
    char path[512];
    snprintf(path, sizeof(path), "%s/taratura.txt", dir);
    const char* rows = strchr(tab, '\n');   // la prima riga e' l'elenco dei suoni da rifare, poi la tabella
    if (FILE* f = fopen(path, "w")) { fputs(rows ? rows + 1 : tab, f); fclose(f); }
    printf("taratura completa: %.1f ms di CPU, %.1f s di suono; tabella CAL_TAB %s\n", c1 - c0, st.init_samples / (double)RATE,
           diff == 0 ? "giusta" : "da rifare");
    if (diff != 0) {
      const char* list = strchr(tab, ':') ? strchr(tab, ':') + 2 : tab;
      printf("ERRORE: %d suoni cambiati dall'ultima taratura (%.*s): copiare %s in CAL_TAB di src/audio.cpp\n", diff,
             rows ? (int)(rows - list) : 0, list, path);
      g_fail = 1;
    }
  }
  audio_set_volume(7, 8);
  test_sounds(dir);
  test_music(dir);
  test_battle(dir);
  test_edges(dir);
  printf("\n%s\n", g_fail ? "PROVE FALLITE" : "tutte le prove passate");
  return g_fail ? 1 : 0;
}
