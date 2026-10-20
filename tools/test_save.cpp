// Prove dei salvataggi: dalla 0.5.3 le impostazioni in pupazzi.sav e le partite in partita1..4.sav.
// File che mancano, andata e ritorno, file rovinati (troncati, binari, con righe non valide), chiavi
// sconosciute conservate, cartella in sola lettura o che non c'e', tante scritture di fila, giri e
// mappe aperte di ogni pianeta, i salvataggi della 0.4 e della 0.5.2 (che diventano la partita 1),
// le partite: nuova, carica, copia, elimina. Usa solo save.cpp e plat.cpp, in una cartella temporanea.
//   make build/native/test_save && ./build/native/test_save
#include "save.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int g_fail = 0;
static int g_logs = 0;
static char g_lastlog[512];
static char g_base[64] = "/tmp/pupazzi_test_save_XXXXXX";

static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "  ok    " : "  ERRORE", what);
  if (!ok) g_fail++;
}

static void sink(const char* line) {
  g_logs++;
  snprintf(g_lastlog, sizeof(g_lastlog), "%s", line);
}

// Cartella di prova name dentro quella temporanea (creata se create).
static const char* dir_of(const char* name, bool create) {
  static char path[8][200];
  static int k = 0;
  char* p = path[k++ & 7];
  snprintf(p, 200, "%s/%s", g_base, name);
  if (create) mkdir(p, 0755);
  return p;
}

static const char* file_in(const char* dir, const char* name) {
  static char path[4][300];
  static int k = 0;
  char* p = path[k++ & 3];
  snprintf(p, 300, "%s/%s", dir, name);
  return p;
}

static void put_file(const char* path, const void* data, size_t n) {
  FILE* f = fopen(path, "wb");
  if (!f) { printf("  (non riesco a scrivere %s)\n", path); return; }
  fwrite(data, 1, n, f);
  fclose(f);
}
static void put_text(const char* path, const char* s) { put_file(path, s, strlen(s)); }

static int get_file(const char* path, char* buf, int cap) {
  FILE* f = fopen(path, "rb");
  if (!f) return -1;
  const int n = (int)fread(buf, 1, (size_t)cap - 1, f);
  fclose(f);
  buf[n < 0 ? 0 : n] = 0;
  return n;
}

static bool exists(const char* path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static int count_of(const char* hay, const char* needle) {
  int n = 0;
  for (const char* p = strstr(hay, needle); p; p = strstr(p + 1, needle)) n++;
  return n;
}

// Pianeti: la Terra con la prima mappa aperta, gli altri chiusi; tutti al giro 1.
static bool worlds_default(const SaveData* d) {
  bool ok = d->maps_open[0] == 1;
  for (int w = 0; w < SAVE_WORLDS; ++w) ok &= d->giro[w] == 1 && (w == 0 || d->maps_open[w] == 0);
  return ok;
}

static bool settings_default(const SaveData* d) {
  bool ok = d->antialias && d->music_vol == 6 && d->sfx_vol == 8 && d->lang == 0;
  for (int i = 0; i < SAVE_PLAYERS; ++i) ok &= d->aim[i] == 1;
  return ok;
}

static bool game_default(const SaveData* d) { return worlds_default(d) && !d->story_seen && d->nmaps == 0 && d->seconds == 0; }
static bool is_default(const SaveData* d) { return settings_default(d) && game_default(d); }

static const char* const FULL_SET =
    "# Pupazzi: impostazioni di questa console. Le partite sono in partita1.sav ... partita4.sav\n"
    "# (si duplicano e si eliminano dal gioco: prima schermata, Gestisci salvataggi).\n"
    "versione=2\n"
    "mira.1=0\n"
    "mira.2=2\n"
    "mira.3=3\n"
    "mira.4=1\n"
    "antialias=0\n"
    "volume_musica=0\n"
    "volume_effetti=10\n"
    "lingua=ja\n"
    "salvataggi=1\n";

static const char* const FULL_GAME =
    "# Pupazzi: una partita. Cancella il file (o eliminala dal gioco) per liberare il posto.\n"
    "# giro.PIANETA: giro del pianeta; mappe.PIANETA: sue mappe aperte, in ordine; tempo: secondi di gioco\n"
    "# stelle.MAPPA: stelle del giro in corso, somma di 1 (finita), 2 (esploratore salvato), 4 (senza ricominciare la mappa)\n"
    "versione=2\n"
    "storia_pianeti=1\n"
    "mappe.terra=3\n"
    "giro.terra=2\n"
    "mappe.luna=1\n"
    "giro.luna=1\n"
    "stelle.terra1=3\n"
    "stelle.terra_boss=4\n"
    "tempo=3725\n";

static bool settings_full(const SaveData* d) {
  return d->aim[0] == 0 && d->aim[1] == 2 && d->aim[2] == 3 && d->aim[3] == 1 && !d->antialias && d->music_vol == 0 &&
         d->sfx_vol == 10 && d->lang == 5;
}

static bool game_full(const SaveData* d) {
  bool ok = d->maps_open[0] == 3 && d->giro[0] == 2 && d->maps_open[1] == 1 && d->giro[1] == 1;
  for (int w = 2; w < SAVE_WORLDS; ++w) ok &= d->maps_open[w] == 0 && d->giro[w] == 1;
  return ok && d->story_seen && d->seconds == 3725 && save_stars_in(d, "terra1") == 3 && save_stars_in(d, "terra_boss") == 4;
}

// Il file della partita 1 con questi byte, poi la si carica partendo da capo: una partita gia' attiva
// prima si scriverebbe sopra il file appena messo (come nel gioco, dove nessuno cambia i file mentre
// gira).
static bool load_bytes(const char* dir, const void* data, size_t n) {
  put_file(file_in(dir, "partita1.sav"), data, n);
  save_open(dir);
  return save_slot_load(0);
}
static bool load_text(const char* dir, const char* s) { return load_bytes(dir, s, strlen(s)); }

int main() {
  plat_set_sink(sink);
  if (!mkdtemp(g_base)) { printf("mkdtemp non riuscita\n"); return 1; }
  static char buf[64 * 1024];

  printf("0) prima di save_open\n");
  {
    check(!strcmp(save_path(), "") && !save_write(), "nessun percorso, save_write non fa nulla");
    check(is_default(save_data()) && save_stars("terra1") == 0 && save_slot() == -1, "valori predefiniti, nessuna partita");
    check(!save_slot_new(0) && !save_slot_load(0) && !strcmp(save_slot_path(0), ""), "senza cartella niente partite");
  }

  printf("1) file che mancano\n");
  const char* A = dir_of("a", true);
  {
    const int st = save_open(A);
    check(st == SAVE_NUOVO && !strcmp(save_status(), "nuovo"), "esito: nuovo");
    check(is_default(save_data()) && save_stars("terra1") == 0,
          "valori predefiniti (Terra con 1 mappa aperta, giro 1 ovunque, mira 1, antialias, volumi 6 e 8, italiano)");
    check(!strcmp(save_path(), file_in(A, "pupazzi.sav")) && !exists(save_path()), "percorso giusto; aprire non crea il file");
    check(!strcmp(save_slot_path(0), file_in(A, "partita1.sav")) && !strcmp(save_slot_path(3), file_in(A, "partita4.sav")) &&
              !strcmp(save_slot_path(4), "") && !strcmp(save_slot_path(-1), ""),
          "percorsi delle partite");
    SaveData d;
    bool none = save_slot() == -1 && !save_slot_load(0);
    for (int i = 0; i < SAVE_SLOTS; ++i) none &= save_slot_peek(i, &d) == SAVE_NUOVO && game_default(&d);
    check(none && save_slot_peek(9, &d) == SAVE_NUOVO, "nessuna partita: posti liberi, caricare non riesce");
  }

  printf("2) andata e ritorno\n");
  {
    check(save_slot_new(0) && save_slot() == 0 && exists(save_slot_path(0)), "partita nuova al posto 1, subito scritta");
    SaveData* d = save_data();
    d->maps_open[0] = 3;
    d->giro[0] = 2;
    d->maps_open[1] = 1;          // Luna raggiunta: si scrive anche lei
    d->story_seen = true;
    d->seconds = 3725;
    d->aim[0] = 0; d->aim[1] = 2; d->aim[2] = 3; d->aim[3] = 1;
    d->antialias = false;
    d->music_vol = 0;
    d->sfx_vol = 10;
    d->lang = 5;
    save_add_stars("terra1", SAVE_STAR_DONE);
    save_add_stars("terra1", SAVE_STAR_EXPLORER);
    save_add_stars("terra_boss", SAVE_STAR_NO_WIPE);
    save_add_stars("terra1", SAVE_STAR_DONE);
    check(save_stars("terra1") == 3 && save_stars("terra_boss") == 4 && save_stars("terra2") == 0, "le stelle si sommano (OR)");
    const int logs = g_logs;
    check(save_write() && g_logs == logs, "scritto, niente nel registro");
    get_file(save_path(), buf, sizeof(buf));
    check(!strcmp(buf, FULL_SET), "impostazioni come previsto");
    if (strcmp(buf, FULL_SET)) printf("--- scritto:\n%s---\n", buf);
    get_file(save_slot_path(0), buf, sizeof(buf));
    check(!strcmp(buf, FULL_GAME), "partita come previsto");
    if (strcmp(buf, FULL_GAME)) printf("--- scritto:\n%s---\n", buf);
    check(!exists(file_in(A, "pupazzi.sav.tmp")) && !exists(file_in(A, "partita1.sav.tmp")), "nessun file .tmp rimasto");
    const int st = save_open(A);
    check(st == SAVE_LETTO && !strcmp(save_status(), "letto") && settings_full(save_data()) && game_default(save_data()) && save_slot() == -1,
          "riletto: impostazioni uguali, nessuna partita attiva");
    SaveData peek;
    check(save_slot_peek(0, &peek) == SAVE_LETTO && game_full(&peek) && settings_default(&peek), "la partita 1 si legge a parte");
    check(save_slot_load(0) && save_slot() == 0 && game_full(save_data()) && settings_full(save_data()) &&
              !strcmp(save_slot_status(), "letto"),
          "caricata: tutto come prima");
  }

  printf("3) se nulla cambia i file non si riscrivono\n");
  {
    struct stat s0, s1, s2, g0, g1, g2;
    stat(save_path(), &s0);
    stat(save_slot_path(0), &g0);
    const bool w1 = save_write();
    stat(save_path(), &s1);
    stat(save_slot_path(0), &g1);
    save_data()->music_vol = 4;
    const bool w2 = save_write();
    stat(save_path(), &s2);
    stat(save_slot_path(0), &g2);
    check(w1 && s0.st_ino == s1.st_ino && g0.st_ino == g1.st_ino, "dopo la lettura: nessuna scrittura");
    check(w2 && s2.st_ino != s1.st_ino && g2.st_ino == g1.st_ino, "cambiata un'impostazione: solo pupazzi.sav");
    save_data()->seconds++;
    save_write();
    struct stat g3;
    stat(save_slot_path(0), &g3);
    check(g3.st_ino != g2.st_ino && save_open(A) == SAVE_LETTO && save_data()->music_vol == 4 && save_slot_load(0) &&
              save_data()->seconds == 3726,
          "cambiata la partita: il suo file");
  }

  printf("4) valori fuori dai limiti e righe non valide\n");
  {
    const char* B = dir_of("b", true);
    char text[2048];
    char longline[700];
    memset(longline, 'a', 600);
    memcpy(longline + 600, "=1\n", 4);
    snprintf(text, sizeof(text), "mira.1=9\nmira.2=-3\nvolume_musica=99999999999999999999\nvolume_effetti=abc\nantialias=7\nlingua=fr\n"
                                 "ciao\n=5\nmira.3=\n%slingua=pt\n  mira.4 =2  \r\n",
             longline);
    put_text(file_in(B, "pupazzi.sav"), text);
    const int st = save_open(B);
    const SaveData* d = save_data();
    check(st == SAVE_LETTO && strstr(save_status(), "righe non valide ignorate: 6"), save_status());
    check(d->aim[0] == 3 && d->aim[1] == 0 && d->music_vol == 10 && d->sfx_vol == 8 && d->antialias && d->lang == 3 && d->aim[3] == 2 &&
              d->aim[2] == 1,
          "riportati nei limiti; volume_effetti=abc e lingua=fr ignorati; spazi attorno e \\r finale");
    snprintf(text, sizeof(text), "mappe.terra=0\nciao\nstelle.prateria2=12\nstelle.prateria2=1\n  storia_pianeti = 1  \r\ntempo=-5\ngiro.luna=x\n");
    check(load_text(B, text) && strstr(save_slot_status(), "righe non valide ignorate: 2"), save_slot_status());
    check(d->maps_open[0] == 1 && d->story_seen && save_stars("prateria2") == 7 && d->seconds == 0, "partita: mappe.terra=0 -> 1, stelle 12 -> 7, tempo negativo -> 0");
  }

  printf("5) file troncato in ogni punto\n");
  {
    const char* C = dir_of("c", true);
    const int len = (int)strlen(FULL_GAME);
    const int ver = (int)(strstr(FULL_GAME, "versione=2\n") - FULL_GAME), unl = (int)(strstr(FULL_GAME, "mappe.terra=3\n") - FULL_GAME);
    bool ok = true, kept = true;
    for (int cut = 0; cut <= len; ++cut) {
      const bool loaded = load_bytes(C, FULL_GAME, (size_t)cut);
      const SaveData* d = save_data();
      if (cut == 0) { ok &= loaded && strstr(save_slot_status(), "vuoto") != 0 && game_default(d); continue; }
      if (cut < ver + 10) { ok &= loaded && (strstr(save_slot_status(), "letto") || game_default(d)); continue; }
      ok &= loaded && !strncmp(save_slot_status(), "letto", 5);
      // Ogni valore e' quello scritto, quello predefinito o (numero tagliato) un suo prefisso.
      ok &= save_stars("terra_boss") == 0 || save_stars("terra_boss") == 4;
      ok &= d->giro[0] == 1 || d->giro[0] == 2;
      ok &= d->seconds == 0 || d->seconds == 3 || d->seconds == 37 || d->seconds == 372 || d->seconds == 3725;
      kept &= d->maps_open[0] == (cut >= unl + 13 ? 3 : 1);
    }
    check(ok, "mai valori strani; vuoto = illeggibile (si riparte dall'inizio)");
    check(kept, "le righe intere prima del taglio restano");
    check(load_bytes(C, FULL_GAME, (size_t)len - 1) && game_full(save_data()) && !strcmp(save_slot_status(), "letto"),
          "ultima riga senza a capo: valida");
  }

  printf("6) file binario o rovinato\n");
  {
    const char* D = dir_of("d", true);
    static unsigned char junk[8192];
    uint32_t x = 2463534242u;
    for (unsigned i = 0; i < sizeof(junk); ++i) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      junk[i] = (unsigned char)(x >> 24);
    }
    put_file(file_in(D, "pupazzi.sav"), junk, sizeof(junk));
    put_file(file_in(D, "partita1.sav"), junk, sizeof(junk));
    int st = save_open(D);
    check(st == SAVE_ILLEGGIBILE && strstr(save_status(), "non e' un file di testo") && is_default(save_data()), save_status());
    SaveData peek;
    check(save_slot_peek(0, &peek) == SAVE_ILLEGGIBILE, "la partita rovinata si vede come illeggibile");
    check(save_slot_load(0) && strstr(save_slot_status(), "non e' un file di testo") && game_default(save_data()), save_slot_status());
    save_data()->maps_open[0] = 2;
    save_data()->music_vol = 5;
    check(save_write(), "si scrive lo stesso");
    char back[sizeof(junk) + 16];
    int nb = get_file(file_in(D, "pupazzi.sav.illeggibile"), back, sizeof(back));
    bool same = nb == (int)sizeof(junk) && !memcmp(back, junk, sizeof(junk));
    nb = get_file(file_in(D, "partita1.sav.illeggibile"), back, sizeof(back));
    same &= nb == (int)sizeof(junk) && !memcmp(back, junk, sizeof(junk));
    check(same, "i file rovinati restano come .illeggibile");
    check(save_open(D) == SAVE_LETTO && save_data()->music_vol == 5 && save_slot_load(0) && save_data()->maps_open[0] == 2,
          "i file nuovi si rileggono");
    static char zeros[4096];
    check(load_bytes(D, zeros, sizeof(zeros)) && strstr(save_slot_status(), "illeggibile") && game_default(save_data()),
          "tutto zeri (spegnimento sulla FAT): illeggibile");
    // Contenuto buono seguito da zeri: quello che c'e' si tiene.
    char mixed[8192];
    const int lf = (int)strlen(FULL_GAME);
    memcpy(mixed, FULL_GAME, (size_t)lf);
    memset(mixed + lf, 0, 2000);
    check(load_bytes(D, mixed, (size_t)lf + 2000) && game_full(save_data()) && strstr(save_slot_status(), "caratteri non di testo"),
          save_slot_status());
    check(load_text(D, "solo testo\nsenza uguali\n") && strstr(save_slot_status(), "nessuna riga valida"), "solo righe senza '=': illeggibile");
    check(load_text(D, "# solo commenti\n\n   \n") && !strcmp(save_slot_status(), "letto") && game_default(save_data()),
          "solo commenti: letto, valori predefiniti");
    check(load_text(D, "\xEF\xBB\xBFmappe.terra=2\r\ntempo=7\r\n") && save_data()->maps_open[0] == 2 && save_data()->seconds == 7 &&
              !strcmp(save_slot_status(), "letto"),
          "BOM e a capo di Windows");
  }

  printf("7) chiavi sconosciute conservate\n");
  {
    const char* E = dir_of("e", true);
    put_text(file_in(E, "pupazzi.sav"), "versione=7\nmonete=12\nmira.5=2\nnome.giocatore.1 = Andrea Rossi\nvuota=\n");
    put_text(file_in(E, "partita1.sav"),
             "versione=7\nmonete=12\nstelle.mappa_dal_nome_davvero_troppo_lungo=3\nstelle.prateria1=1\nvuota=\nstelle.a.b=2\nmira.1=3\n");
    const int st = save_open(E);
    check(st == SAVE_LETTO && strstr(save_status(), "versione piu' nuova"), save_status());
    check(save_slot_load(0) && strstr(save_slot_status(), "versione piu' nuova") && save_stars("prateria1") == 1 && save_data()->aim[0] == 1,
          "partita letta; mira.1 in una partita non conta");
    save_data()->sfx_vol = 2;
    save_data()->seconds = 1;
    check(save_write(), "scritto");
    get_file(save_path(), buf, sizeof(buf));
    static const char* const KEEP_SET[4] = { "\nmonete=12\n", "\nmira.5=2\n", "\nnome.giocatore.1=Andrea Rossi\n", "\nvuota=\n" };
    bool ok = true;
    for (const char* k : KEEP_SET) ok &= count_of(buf, k) == 1;
    ok &= strstr(buf, "\nmonete=12\n") < strstr(buf, "\nmira.5=2\n") && strstr(buf, "\nvolume_effetti=2\n") && strstr(buf, "\nversione=2\n");
    get_file(save_slot_path(0), buf, sizeof(buf));
    static const char* const KEEP_GAME[4] = { "\nmonete=12\n", "\nstelle.mappa_dal_nome_davvero_troppo_lungo=3\n", "\nvuota=\n",
                                              "\nstelle.a.b=2\n" };
    for (const char* k : KEEP_GAME) ok &= count_of(buf, k) == 1;
    ok &= strstr(buf, "\nvuota=\n") < strstr(buf, "\nstelle.a.b=2\n") && count_of(buf, "mira.1") == 0 && strstr(buf, "\ntempo=1\n");
    check(ok, "ogni chiave sconosciuta una volta, nello stesso ordine, dopo quelle note; le impostazioni non finiscono nella partita");
    save_open(E);
    save_slot_load(0);
    save_data()->sfx_vol = 3;
    save_data()->seconds = 2;
    save_write();
    get_file(save_path(), buf, sizeof(buf));
    ok = save_data()->music_vol == 6;
    for (const char* k : KEEP_SET) ok &= count_of(buf, k) == 1;
    get_file(save_slot_path(0), buf, sizeof(buf));
    for (const char* k : KEEP_GAME) ok &= count_of(buf, k) == 1;
    check(ok, "e restano anche alla seconda scrittura");
    // Troppe: se ne tengono 64 e il registro lo dice.
    static char many[400 * 1024];
    int n = 0;
    for (int i = 0; n < (int)sizeof(many) - 64; ++i) n += snprintf(many + n, sizeof(many) - (size_t)n, "futura.chiave%d=%d\n", i, i);
    check(load_text(E, many) && strstr(save_slot_status(), "voci sconosciute di troppo") && strstr(save_slot_status(), "letto solo l'inizio"),
          save_slot_status());
    save_data()->seconds = 4;     // senza cambiamenti non si riscriverebbe
    save_write();
    get_file(save_slot_path(0), buf, sizeof(buf));
    check(count_of(buf, "\nfutura.chiave") == 64 && strstr(buf, "\nfutura.chiave63=63\n"), "se ne tengono le prime 64");
  }

  printf("8) cartella in sola lettura, che non c'e' o che non e' una cartella\n");
  {
    const char* F = dir_of("manca/ancora", false);
    check(save_open(F) == SAVE_NUOVO, "cartella (e madre) che non c'e': nuovo");
    int logs = g_logs;
    const bool w1 = save_write(), w2 = save_write();
    check(!w1 && !w2 && g_logs == logs + 1 && strstr(g_lastlog, "non riuscito"), g_lastlog);
    logs = g_logs;
    const bool n1 = save_slot_new(0);
    check(!n1 && save_slot() == 0 && g_logs == logs + 2 && strstr(g_lastlog, "partita1.sav.tmp"),
          "partita nuova che non si puo' scrivere: resta in memoria; nel registro la partita nuova e l'errore");
    const char* G = dir_of("nuova", false);
    save_open(G);
    check(save_write() && exists(file_in(G, "pupazzi.sav")), "manca solo l'ultima cartella: si crea");
    const char* plain = file_in(g_base, "un_file.txt");
    put_text(plain, "x\n");
    char sub[300];
    snprintf(sub, sizeof(sub), "%s/sotto", plain);
    const int st = save_open(sub);
    logs = g_logs;
    const bool w3 = save_write(), w4 = save_write();
    check(st == SAVE_ILLEGGIBILE && strstr(save_status(), "non e' una cartella") && !w3 && !w4 && g_logs == logs + 1, save_status());
    const char* H = dir_of("h", true);
    mkdir(file_in(H, "pupazzi.sav"), 0755);
    const int st2 = save_open(H);
    logs = g_logs;
    const bool w5 = save_write();
    check(st2 == SAVE_ILLEGGIBILE && strstr(save_status(), "c'e' una cartella") && !w5 && g_logs == logs + 1, save_status());
    if (geteuid() != 0) {
      const char* R = dir_of("r", true);
      put_text(file_in(R, "pupazzi.sav"), FULL_SET);
      chmod(R, 0555);
      const int st3 = save_open(R);
      save_data()->music_vol = 9;
      logs = g_logs;
      const bool w6 = save_write(), w7 = save_write();
      check(st3 == SAVE_LETTO && !w6 && !w7 && g_logs == logs + 1, g_lastlog);
      chmod(R, 0755);
      logs = g_logs;
      const bool w8 = save_write();
      check(w8 && g_logs == logs + 1 && strstr(g_lastlog, "di nuovo riuscito"), "tornata scrivibile: si salva e il registro lo dice");
    } else {
      printf("  (si gira come root: la prova con chmod 555 non avrebbe senso)\n");
    }
    if (exists("/proc/self")) {
      save_open("/proc");
      logs = g_logs;
      const bool w9 = save_write();
      check(!w9 && g_logs == logs + 1, "/proc (non si puo' scrivere nemmeno da root): false, una riga nel registro");
    }
    save_open(dir_of("barra/", false));
    check(!strcmp(save_path(), file_in(dir_of("barra", false), "pupazzi.sav")), "la / finale della cartella non raddoppia");
    save_open(NULL);
    check(!strcmp(save_path(), "") && !save_write() && !save_slot_new(0), "cartella NULL: niente file");
  }

  printf("9) tante scritture di fila\n");
  {
    const char* M = dir_of("m", true);
    save_open(M);
    save_slot_new(2);
    const int logs = g_logs;
    bool ok = true;
    const int N = 300;
    struct timespec w0, w1;
    clock_gettime(CLOCK_MONOTONIC, &w0);
    for (int i = 0; i < N; ++i) {
      SaveData* d = save_data();
      d->music_vol = (uint8_t)(i % 11);
      d->maps_open[0] = (uint8_t)(1 + i % 20);
      d->giro[i % SAVE_WORLDS] = (uint8_t)(1 + i % 7);
      char id[16];
      snprintf(id, sizeof(id), "m%02d", i % 50);
      save_add_stars(id, (uint8_t)(1u << (i % 3)));
      ok &= save_write();
    }
    clock_gettime(CLOCK_MONOTONIC, &w1);
    const double ms = (double)(w1.tv_sec - w0.tv_sec) * 1000.0 + (double)(w1.tv_nsec - w0.tv_nsec) * 1e-6;
    check(ok && g_logs == logs && !exists(file_in(M, "pupazzi.sav.tmp")) && !exists(file_in(M, "partita3.sav.tmp")),
          "300 scritture riuscite, nessun file .tmp rimasto");
    printf("  (%.2f ms per scrittura su questa macchina)\n", ms / N);
    const int st = save_open(M);
    const SaveData* d = save_data();
    bool same = st == SAVE_LETTO && d->music_vol == (N - 1) % 11 && save_slot_load(2) && d->maps_open[0] == 1 + (N - 1) % 20 && d->nmaps == 50;
    for (int w = 0; w < SAVE_WORLDS; ++w) {
      int last = 1;                     // l'ultima scrittura del pianeta w
      for (int i = 0; i < N; ++i)
        if (i % SAVE_WORLDS == w) last = 1 + i % 7;
      same &= d->giro[w] == last;
    }
    for (int k = 0; k < 50; ++k) {
      char id[16];
      snprintf(id, sizeof(id), "m%02d", k);
      same &= save_stars(id) == 7;      // ogni mappa ha preso 1, 2 e 4 in giri diversi
    }
    check(same, "i file finali hanno gli ultimi valori");
  }

  printf("10) nomi di mappa non validi e dati cambiati male dal gioco\n");
  {
    const char* P = dir_of("p", true);
    save_open(P);
    save_slot_new(1);
    const int logs = g_logs;
    save_add_stars(NULL, 1);
    save_add_stars("", 1);
    save_add_stars("con spazio", 1);
    save_add_stars("a=b", 1);
    save_add_stars("nome_di_mappa_lungo_24ch", 1);
    save_add_stars("nome_di_mappa_lungo_23c", 9);
    SaveData* d = save_data();
    check(d->nmaps == 1 && save_stars("nome_di_mappa_lungo_23c") == 1 && g_logs == logs + 5, "nomi non validi ignorati (e scritti nel registro)");
    d->aim[2] = 200;
    d->music_vol = 77;
    d->lang = 200;
    d->maps_open[0] = 0;
    d->maps_open[3] = 250;
    d->giro[1] = 0;
    d->giro[2] = 200;
    d->nmaps = 1000;
    d->seconds = 4000000000u;
    check(save_write() && d->aim[2] == 3 && d->music_vol == 10 && d->lang == 0 && d->maps_open[0] == 1 && d->maps_open[3] == 99 &&
              d->giro[1] == 1 && d->giro[2] == SAVE_GIRO_MAX && d->nmaps == SAVE_MAX_MAPS && d->seconds == 999999999u,
          "riportati nei limiti");
    check(save_open(P) == SAVE_LETTO && !strcmp(save_status(), "letto") && save_slot_load(1) && !strcmp(save_slot_status(), "letto"),
          "e i file si rileggono senza righe non valide");
    for (int i = 0; i < SAVE_MAX_MAPS + 5; ++i) {
      char id[16];
      snprintf(id, sizeof(id), "x%d", i);
      save_add_stars(id, 1);
    }
    check(save_data()->nmaps == SAVE_MAX_MAPS && save_stars("x62") == 1 && save_stars("x63") == 0,
          "oltre 64 mappe le altre non si salvano (detto una volta)");
  }

  printf("11) il salvataggio della 0.4 (la prateria tutta finita) diventa la partita 1\n");
  {
    // Il file di Andrea dopo la 0.4: 4 mappe aperte e 12 stelle della prateria. La Terra e' nuova:
    // si ricomincia dalla prima mappa, al giro 1, senza stelle; i dati vecchi restano nei file.
    const char* Q = dir_of("q", true);
    put_text(file_in(Q, "pupazzi.sav"),
             "# Pupazzi: salvataggio. Cancellalo per ricominciare da capo.\n"
             "# stelle.MAPPA: somma di 1 (finita), 2 (esploratore salvato), 4 (senza mai finire tutti a terra)\n"
             "versione=1\nmappe_sbloccate=4\nstoria_vista=1\nmira.1=1\nmira.2=1\nmira.3=1\nmira.4=1\nantialias=1\n"
             "volume_musica=1\nvolume_effetti=7\nstelle.prateria1=7\nstelle.prateria2=7\nstelle.prateria3=7\nstelle.prateria_boss=7\n");
    const int logs = g_logs;
    const int st = save_open(Q);
    const SaveData* d = save_data();
    check(st == SAVE_LETTO && !strcmp(save_status(), "letto") && d->music_vol == 1 && d->sfx_vol == 7 && save_slot() == -1,
          "letto senza errori; impostazioni tenute");
    check(exists(file_in(Q, "partita1.sav")) && g_logs == logs + 1 && strstr(g_lastlog, "partita 1"), "i progressi sono diventati la partita 1");
    check(save_slot_load(0) && worlds_default(d) && !d->story_seen && save_stars("terra1") == 0 && save_stars("prateria1") == 7,
          "partita 1: Terra con la sola prima mappa aperta, giro 1, storia nuova; le stelle della prateria restano");
    save_data()->maps_open[0] = 2;
    save_add_stars("terra1", SAVE_STAR_DONE);
    check(save_write(), "scritto");
    get_file(save_slot_path(0), buf, sizeof(buf));
    bool ok = count_of(buf, "\nmappe_sbloccate=4\n") == 1 && count_of(buf, "\nstelle.prateria_boss=7\n") == 1;
    ok &= count_of(buf, "\nstoria_vista=1\n") == 1 && count_of(buf, "\nstoria_pianeti=0\n") == 1;
    ok &= count_of(buf, "\nmappe.terra=2\n") == 1 && count_of(buf, "\ngiro.terra=1\n") == 1 && count_of(buf, "\nstelle.terra1=1\n") == 1;
    ok &= count_of(buf, "mappe.luna") == 0 && count_of(buf, "mira.") == 0 && count_of(buf, "volume_") == 0;
    check(ok, "nella partita: mappe_sbloccate, storia_vista e la prateria accanto alle chiavi nuove; niente impostazioni");
    get_file(save_path(), buf, sizeof(buf));
    ok = count_of(buf, "\nmappe_sbloccate=4\n") == 1 && count_of(buf, "\nstelle.prateria1=7\n") == 1 && count_of(buf, "\nsalvataggi=1\n") == 1 &&
         count_of(buf, "\nversione=2\n") == 1;
    check(ok, "in pupazzi.sav i dati vecchi restano (per una versione di prima) e salvataggi=1");
    check(save_slot_delete(0) && !exists(file_in(Q, "partita1.sav")) && save_slot() == -1, "eliminata la partita 1");
    save_open(Q);
    check(!exists(file_in(Q, "partita1.sav")), "riaprendo non torna: i progressi vecchi si prendono una volta sola");
  }

  printf("12) il salvataggio della 0.5.2 diventa la partita 1\n");
  {
    const char* U = dir_of("u", true);
    put_text(file_in(U, "pupazzi.sav"),
             "# Pupazzi: salvataggio. Cancellalo per ricominciare da capo.\nversione=1\nstoria_pianeti=1\nmira.1=0\nmira.2=1\nmira.3=1\n"
             "mira.4=1\nantialias=1\nvolume_musica=4\nvolume_effetti=7\nmappe.terra=4\ngiro.terra=2\nmappe.luna=1\ngiro.luna=1\n"
             "stelle.terra1=7\nstelle.terra2=3\nstelle.terra3=0\nstelle.terra_boss=0\n");
    save_open(U);
    const SaveData* d = save_data();
    check(d->aim[0] == 0 && d->music_vol == 4 && d->lang == 0 && save_slot() == -1, "impostazioni lette, italiano");
    SaveData peek;
    const bool p = save_slot_peek(0, &peek) == SAVE_LETTO;
    check(p && peek.story_seen && peek.maps_open[0] == 4 && peek.giro[0] == 2 && peek.maps_open[1] == 1 && save_stars_in(&peek, "terra1") == 7 &&
              save_stars_in(&peek, "terra2") == 3 && peek.seconds == 0,
          "la partita 1 ha tutti i progressi");
    check(save_slot_peek(1, &peek) == SAVE_NUOVO && save_slot_peek(3, &peek) == SAVE_NUOVO, "gli altri posti sono liberi");
  }

  printf("13) partite: nuova, carica, copia, elimina\n");
  {
    const char* V = dir_of("v", true);
    save_open(V);
    check(save_slot_new(0) && save_slot() == 0, "nuova al posto 1");
    save_data()->maps_open[0] = 3;
    save_data()->seconds = 100;
    save_add_stars("terra1", 7);
    check(save_slot_new(2) && save_slot() == 2 && game_default(save_data()), "nuova al posto 3: si parte da zero");
    SaveData peek;
    check(save_slot_peek(0, &peek) == SAVE_LETTO && peek.maps_open[0] == 3 && peek.seconds == 100 && save_stars_in(&peek, "terra1") == 7,
          "passando a un'altra, quella di prima e' stata scritta");
    save_data()->giro[0] = 5;
    check(save_slot_copy(0, 1) && save_slot_peek(1, &peek) == SAVE_LETTO && peek.maps_open[0] == 3 && save_stars_in(&peek, "terra1") == 7,
          "copia della 1 nella 2");
    check(save_slot() == 2 && save_data()->giro[0] == 5, "la partita attiva non cambia copiando");
    check(save_slot_copy(2, 3) && save_slot_peek(3, &peek) == SAVE_LETTO && peek.giro[0] == 5, "copiando quella attiva prima la si scrive");
    check(!save_slot_copy(0, 0) && !save_slot_copy(-1, 1) && !save_slot_copy(0, 4) && !save_slot_copy(0, 5), "copie impossibili: false");
    remove(save_slot_path(1));
    check(!save_slot_copy(1, 0) && save_slot_peek(0, &peek) == SAVE_LETTO, "da un posto vuoto non si copia nulla");
    check(save_slot_load(0) && save_slot() == 0 && save_data()->maps_open[0] == 3 && save_data()->giro[0] == 1, "caricata la 1");
    check(save_slot_peek(2, &peek) == SAVE_LETTO && peek.giro[0] == 5, "e la 3 era stata scritta prima");
    check(save_slot_copy(3, 0) && save_slot() == 0 && save_data()->giro[0] == 5, "copia sopra quella attiva: si ricarica la copia");
    check(save_slot_delete(0) && save_slot() == -1 && game_default(save_data()) && save_slot_peek(0, &peek) == SAVE_NUOVO,
          "eliminata quella attiva: nessuna partita attiva");
    check(save_slot_delete(1) && !save_slot_delete(7), "eliminare un posto vuoto va bene; fuori dai limiti no");
    check(!save_slot_load(0) && save_slot() == -1, "caricare un posto vuoto non riesce");
    check(save_write(), "senza partita attiva si scrivono le impostazioni");
    check(!strcmp(save_lang_id(0), "it") && !strcmp(save_lang_id(6), "hi") && !strcmp(save_lang_id(7), ""), "codici delle lingue");
  }

  printf("14) giri e mappe di ogni pianeta\n");
  {
    const char* T = dir_of("t", true);
    save_open(T);
    const bool l = load_text(T, "giro.terra=3\nmappe.terra=4\nmappe.luna=2\ngiro.marte=7\ngiro.nettuno=500\nmappe.venere=-4\ngiro.mercurio=0\n"
                                "giro.plutone=2\nmappe.Terra=3\ngiro.terra.1=4\ngiro.luna=due\nstelle.terra1=5\nstelle.terra2=0\n");
    const SaveData* d = save_data();
    bool ok = l && d->giro[0] == 3 && d->maps_open[0] == 4 && d->maps_open[1] == 2 && d->giro[1] == 1;
    ok &= d->giro[2] == 7 && d->giro[8] == SAVE_GIRO_MAX && d->maps_open[3] == 0 && d->giro[4] == 1;
    check(ok && strstr(save_slot_status(), "righe non valide ignorate: 1"), save_slot_status());
    save_data()->seconds = 5;
    save_write();
    get_file(save_slot_path(0), buf, sizeof(buf));
    ok = count_of(buf, "\ngiro.plutone=2\n") == 1 && count_of(buf, "\nmappe.Terra=3\n") == 1 && count_of(buf, "\ngiro.terra.1=4\n") == 1;
    ok &= count_of(buf, "\ngiro.marte=7\nmappe.venere") == 0 && count_of(buf, "\nmappe.marte=0\ngiro.marte=7\n") == 1;
    ok &= count_of(buf, "\nmappe.nettuno=0\ngiro.nettuno=99\n") == 1 && count_of(buf, "venere") == 0 && count_of(buf, "mercurio") == 0;
    check(ok, "pianeti sconosciuti o scritti male restano come chiavi sconosciute; si scrivono i pianeti raggiunti");
    // Giro nuovo: le stelle del pianeta tornano a zero (la mappa resta nel file con 0).
    save_set_stars("terra1", 0);
    save_set_stars("terra3", 0);          // non c'era: niente riga
    save_set_stars("terra2", 6);
    save_set_stars("a b", 3);             // nome non valido: niente
    check(save_stars("terra1") == 0 && save_stars("terra2") == 6 && save_stars("terra3") == 0 && save_write(), "save_set_stars");
    get_file(save_slot_path(0), buf, sizeof(buf));
    check(count_of(buf, "\nstelle.terra1=0\n") == 1 && count_of(buf, "\nstelle.terra2=6\n") == 1 && count_of(buf, "terra3") == 0,
          "le stelle azzerate si scrivono come 0");
    check(!strcmp(save_world_id(0), "terra") && !strcmp(save_world_id(8), "nettuno") && !strcmp(save_world_id(9), "") &&
              !strcmp(save_world_id(-1), ""),
          "nomi dei pianeti nel file");
  }

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "chmod -R u+w %s 2>/dev/null; rm -rf %s", g_base, g_base);
  if (system(cmd) != 0) printf("  (non riesco a cancellare %s)\n", g_base);
  printf(g_fail ? "\n%d prove fallite\n" : "\ntutto bene\n", g_fail);
  return g_fail ? 1 : 0;
}
