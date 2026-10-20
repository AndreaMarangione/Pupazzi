// Prove delle lingue (0.5.3): tr trova i testi; ogni testo della tabella, in ogni lingua, si disegna
// senza caratteri che mancano e con le parole hindi gia' composte (nelle facce piccole e nelle facce
// grandi in cui si usa); a capo e puntini non spezzano le parole hindi, in cinese e giapponese si va
// a capo tra i caratteri, una parola piu' larga della riga si spezza senza perdere nulla.
//   make build/native/test_lang && ./build/native/test_lang
#include "lang.h"
#include "hud.h"
#include "ui.h"
#include "lang_data.h"
#include <stdio.h>
#include <string.h>

static int g_fail = 0;

static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "  ok    " : "  ERRORE", what);
  if (!ok) g_fail++;
}

// Il testo come lo scrive printf: %d e %u diventano 7, %s niente (sono altri testi), %% un %.
static void expand(char* out, int n, const char* s) {
  int k = 0;
  for (const char* p = s; *p && k < n - 2;) {
    if (*p != '%') {
      out[k++] = *p++;
      continue;
    }
    const char* q = p + 1;
    while ((*q >= '0' && *q <= '9') || *q == '$' || *q == '-' || *q == '+' || *q == '#') ++q;
    if (*q == '%') {
      out[k++] = '%';
      p = q + 1;
    } else if (*q == 'd' || *q == 'u') {
      out[k++] = '7';
      p = q + 1;
    } else if (*q == 's') {
      p = q + 1;
    } else {
      out[k++] = *p++;                     // "il 40% più": testo
    }
  }
  out[k] = 0;
}

// Le parole hindi di line (separate da spazi e punteggiatura latina) sono tutte parole di whole.
static bool whole_words(const char* line, const char* whole) {
  for (const char* p = line; *p;) {
    const char* q = p;
    if (!hud_is_deva(hud_utf8_next(q))) {
      p = q;
      continue;
    }
    const char* e = p;
    while (*e) {
      const char* t = e;
      if (!hud_is_deva(hud_utf8_next(t))) break;
      e = t;
    }
    // la stessa parola, intera, deve esserci nel testo intero
    bool found = false;
    const size_t n = (size_t)(e - p);
    for (const char* w = strstr(whole, ""); w && *w; ++w) {
      if (strncmp(w, p, n)) continue;
      const char* a = w;
      const char* b = w + n;
      bool left = a == whole, right = !*b;
      if (!left) {
        int st = (int)(a - whole) - 1;
        while (st > 0 && ((unsigned char)whole[st] & 0xC0u) == 0x80u) --st;
        const char* t = whole + st;
        left = !hud_is_deva(hud_utf8_next(t));
      }
      if (!right) {
        const char* t = b;
        right = !hud_is_deva(hud_utf8_next(t));
      }
      if (left && right) {
        found = true;
        break;
      }
    }
    if (!found) return false;
    p = e;
  }
  return true;
}

int main() {
  printf("1) tr\n");
  {
    lang_set(LANG_IT);
    const char* k = "NUOVA PARTITA";
    check(tr(k) == k || !strcmp(tr(k), k), "italiano: il testo com'e'");
    lang_set(LANG_EN);
    check(!strcmp(tr("NUOVA PARTITA"), "NEW GAME") && !strcmp(tr("%u min"), "%u min"), "inglese: NEW GAME");
    lang_set(LANG_ZH);
    check(!strcmp(tr("LINGUA"), "语言") && hud_cjk_get() == HUD_CJK_SC, "cinese: 语言, caratteri cinesi");
    lang_set(LANG_JA);
    check(!strcmp(tr("LINGUA"), "言語") && hud_cjk_get() == HUD_CJK_JP, "giapponese: 言語, caratteri giapponesi");
    check(!strcmp(tr("Mappa di Andrea"), "Mappa di Andrea") && !strcmp(tr("12"), "12") && !strcmp(tr(""), ""),
          "testi che non ci sono (mappe personalizzate, numeri): restano come sono");
    check(!strcmp(tr_in(LANG_PT, "Giù"), "Baixo") && !strcmp(tr_in(LANG_IT, "Giù"), "Giù") && tr_known("ESCI") && !tr_known("esci!"),
          "tr_in e tr_known");
    lang_set(99);
    check(lang_get() == LANG_IT, "lingua fuori dai limiti: italiano");
  }

  printf("2) ogni testo in ogni lingua si disegna\n");
  {
    static const int SMALL[] = { HF_B10, HF_B11, HF_B12, HF_S12, HF_B13, HF_S13, HF_S14, HF_S15, HF_S16, HF_B16, HF_S17 };
    static const int BIG[5] = { HF_B22, HF_T25, HF_T32, HF_T38, HF_T44 };
    for (int l = 0; l < NLANGS; ++l) {
      lang_set(l);
      int bad = 0;
      char first[200] = "";
      for (int i = 0; i < LANG_NTEXTS; ++i) {
        char s[512], up[512];
        expand(s, sizeof(s), LANG_TEXT[i][l]);
        hud_upper(up, s, sizeof(up));
        hud_missing(true);
        for (int f : SMALL) hud_text_w(f, s, 0.5f);
        for (int b = 0; b < 5; ++b)
          if ((LANG_BIG[i] >> b) & 1) hud_text_w(BIG[b], up, 3.0f);
        if (hud_missing(true)) {
          if (!bad) snprintf(first, sizeof(first), " (per esempio %.80s)", LANG_TEXT[i][l]);
          bad++;
        }
      }
      char what[320];
      snprintf(what, sizeof(what), "%s: %d testi%s%s", save_lang_id(l), LANG_NTEXTS, bad ? " con caratteri che mancano" : ", tutti",
               first);
      check(bad == 0, what);
    }
    lang_set(LANG_IT);
    // I nomi delle lingue, ognuno con i suoi caratteri (pagina delle lingue S16, prima schermata S13).
    hud_missing(true);
    for (int l = 0; l < NLANGS; ++l) {
      hud_cjk(l == LANG_JA ? HUD_CJK_JP : HUD_CJK_SC);
      hud_text_w(HF_S16, lang_name(l), 1.0f);
      hud_text_w(HF_S13, lang_name(l), 0.5f);
    }
    hud_cjk(HUD_CJK_SC);
    check(hud_missing(true) == 0, "nomi delle lingue");
    // Un carattere che nessuno ha: si conta (e si disegna '?').
    hud_text_w(HF_S13, "\xE2\x99\xA5", 0.0f);
    check(hud_missing(true) == 1, "carattere sconosciuto: contato");
    // Una parola hindi che non e' tra quelle composte (testi cambiati senza rigenerare i caratteri).
    hud_text_w(HF_S13, "क्षत्रियों", 0.0f);
    check(hud_missing(true) >= 1, "parola hindi non composta: contata");
  }

  printf("3) larghezze e a capo\n");
  {
    lang_set(LANG_HI);
    const char* s = tr("Battuto il boss, l'eroe del pianeta entra in squadra. In ogni mappa c'è una capsula nascosta: salvate l'esploratore!");
    const float w = hud_text_w(HF_S16, s, 0.5f);
    char L[12][ui::WRAP_LEN];
    bool cut = true;
    const int n = ui::wrap(s, 300.0f, HF_S16, 0.5f, L, 12, &cut);
    bool ok = n >= 2 && !cut && w > 300.0f;
    for (int k = 0; k < n; ++k) ok &= hud_text_w(HF_S16, L[k], 0.5f) <= 300.0f && whole_words(L[k], s);
    check(ok, "hindi: righe entro 300 pixel, parole intere");
    char f[128];
    ui::fit(f, sizeof(f), s, HF_S16, 0.5f, 150.0f);
    ok = hud_text_w(HF_S16, f, 0.5f) <= 150.0f && strstr(f, "\xE2\x80\xA6") && whole_words(f, s);
    check(ok, "hindi: puntini dopo una parola intera");
    lang_set(LANG_ZH);
    s = tr("Astro parte dalla Terra con gli amici della Luna, di Marte e di Venere: il giro dei pianeti comincia! "
           "Ogni pianeta ha i suoi mostri e un boss.");
    const int nz = ui::wrap(s, 200.0f, HF_S16, 0.5f, L, 12, &cut);
    ok = nz >= 3 && !cut;
    static char joined[4096];
    joined[0] = 0;
    for (int k = 0; k < nz; ++k) {
      ok &= hud_text_w(HF_S16, L[k], 0.5f) <= 200.0f && (k == nz - 1 || hud_text_w(HF_S16, L[k], 0.5f) > 160.0f);
      if (strlen(joined) + strlen(L[k]) < sizeof(joined)) strcat(joined, L[k]);
      // niente riga che comincia con la punteggiatura di chiusura
      ok &= strncmp(L[k], "\xEF\xBC\x8C", 3) && strncmp(L[k], "\xE3\x80\x82", 3) && strncmp(L[k], "\xEF\xBC\x81", 3);
    }
    ok &= !strcmp(joined, s);
    check(ok, "cinese: a capo tra i caratteri, righe piene, niente punteggiatura in testa");
    lang_set(LANG_JA);
    const int nj = ui::wrap(tr("nessun posto libero: elimina prima un salvataggio"), 180.0f, HF_S14, 0.5f, L, 3, &cut);
    check(nj >= 2 && !cut, "giapponese: a capo");
    const int nc = ui::wrap(tr("nessun posto libero: elimina prima un salvataggio"), 60.0f, HF_S14, 0.5f, L, 2, &cut);
    check(nc == 2 && cut && strstr(L[1], "\xE2\x80\xA6"), "giapponese: in due righe strette non ci sta, l'ultima con i puntini");
    lang_set(LANG_IT);
    const int ni = ui::wrap("uno due tre quattro cinque sei", 60.0f, HF_S14, 0.5f, L, 6, &cut);
    check(ni >= 3 && !cut && !strchr(L[0], ' ') == false, "italiano: a capo sugli spazi come prima");
    // Una parola piu' larga della riga: spezzata, nulla si perde, e cut lo dice.
    const char* longw = "precipitevolissimevolmente e poi basta";
    const int nl = ui::wrap(longw, 60.0f, HF_S14, 0.5f, L, 12, &cut);
    char a[256], b[256];
    int na = 0, nb = 0;
    for (const char* p = longw; *p && na < 255; ++p)
      if (*p != ' ') a[na++] = *p;
    a[na] = 0;
    ok = cut && nl >= 4;
    for (int k = 0; k < nl; ++k) {
      ok &= hud_text_w(HF_S14, L[k], 0.5f) <= 60.0f && !strstr(L[k], "\xE2\x80\xA6");
      for (const char* p = L[k]; *p && nb < 255; ++p)
        if (*p != ' ') b[nb++] = *p;
    }
    b[nb] = 0;
    check(ok && !strcmp(a, b), "parola piu' larga della riga: spezzata senza perdere nulla, cut");
    // In hindi no: la parola resta intera (o i puntini), e cut.
    lang_set(LANG_HI);
    s = tr("Battuto il boss, l'eroe del pianeta entra in squadra. In ogni mappa c'è una capsula nascosta: salvate l'esploratore!");
    const int nh = ui::wrap(s, 24.0f, HF_S16, 0.5f, L, 12, &cut);
    ok = cut && nh >= 1;
    for (int k = 0; k < nh; ++k) ok &= whole_words(L[k], s);
    check(ok, "hindi in una riga strettissima: parole mai spezzate, cut");
    lang_set(LANG_IT);
  }

  printf(g_fail ? "\n%d prove fallite\n" : "\ntutto bene\n", g_fail);
  return g_fail ? 1 : 0;
}
