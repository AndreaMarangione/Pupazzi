// Lingue del gioco (dalla 0.5.3): italiano, inglese, spagnolo, portoghese (del Portogallo), cinese
// semplificato, giapponese, hindi.
//
// Il testo si scrive nel codice in italiano e si passa a tr() dove si disegna: tr() restituisce la
// traduzione nella lingua scelta, presa dalla tabella lang/testi.txt (src/lang_data.h, generato da
// tools/make_lang.py, che controlla anche che ogni testo passato a tr() sia nella tabella e che le
// traduzioni abbiano gli stessi %d e %s). Un testo che non c'e' (il nome di una mappa
// personalizzata) resta com'e'. I testi nelle tabelle (carte, eroi, storia) si segnano con N_()
// perche' lo strumento li trovi. Il registro resta in italiano.
#pragma once
#include "save.h"

enum { LANG_IT = 0, LANG_EN, LANG_ES, LANG_PT, LANG_ZH, LANG_JA, LANG_HI, NLANGS };
static_assert(NLANGS == SAVE_LANGS, "save.h e lang.h devono avere le stesse lingue");

#define N_(s) s

// Il testo nella lingua scelta (it e' il testo italiano, come nella tabella). format_arg: usato come
// formato di printf, il compilatore controlla gli argomenti sul testo italiano (le traduzioni hanno
// gli stessi %, lo controlla make_lang.py).
const char* tr(const char* it) __attribute__((format_arg(1)));
// Nella lingua l (i comandi per RetroArch).
const char* tr_in(int l, const char* it) __attribute__((format_arg(2)));
// Il testo e' nella tabella (prove).
bool tr_known(const char* it);
void lang_set(int l);
int lang_get(void);
// Nome della lingua l scritto in quella lingua ("Italiano", "English", "中文"...).
const char* lang_name(int l);
// Lingua con i caratteri cinesi o giapponesi: si va a capo tra un carattere e l'altro.
bool lang_cjk(void);
