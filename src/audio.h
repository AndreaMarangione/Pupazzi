// src/audio.h - Sonoro di Pupazzi: effetti sintetizzati e musica, tutto generato dal codice.
#pragma once
#include <stdint.h>
enum SoundId {
  SND_NONE = 0,
  SND_BULLET, SND_SHARD, SND_FROST, SND_FLAME, SND_LAVA, SND_ROCKET, SND_LEAP, SND_SLAM, SND_BLIZZARD, SND_ERUPT,
  SND_SPECIAL_READY, SND_JUMP, SND_LAND, SND_TRAMP, SND_BOMB_DROP, SND_EXPLODE, SND_EXPLODE_SMALL,
  SND_PICK_TALISMAN, SND_PICK_BOMB, SND_HURT, SND_DOWN, SND_REVIVE,
  SND_HIT, SND_DEFEAT, SND_SCORPION, SND_BOW, SND_SCARAB, SND_WARRIOR, SND_BOSS_ROAR, SND_JAVELIN, SND_SLASH,
  SND_CRATE_HIT, SND_CRATE_BREAK, SND_CAPSULE_HIT, SND_RESCUE, SND_HELP, SND_GATE, SND_ZONE_CLEAR, SND_WAVE, SND_STAR, SND_WIPE,
  SND_UI_MOVE, SND_UI_OK, SND_UI_BACK, SND_CARD_OPEN, SND_CARD_PICK, SND_PAUSE,
  // 0.5.1: SND_SCORPION e' lo scorpione che alza la coda, SND_BOW il tiro dell'arciere di terracotta;
  // freccia piantata a terra vicino a un giocatore; caduta nel cenote di Chichen Itza (si perde una vita)
  SND_ARROW_LAND, SND_SPLASH,
  // 0.5.1, il gladiatore cammina e attacca: passo (anche, piu' fitto, la corsa della carica), spada alzata
  // (avviso del fendente), fendente, preparazione della carica con lo scudo, carica che finisce contro un muro
  SND_BOSS_STEP, SND_BOSS_SWEEP_WARN, SND_BOSS_SWEEP, SND_BOSS_RUSH, SND_BOSS_IMPACT,
  // 0.5.4, bonus delle casse: VELOCITA', FURIA, INVINCIBILE, CARICA presi; un bonus che finisce
  SND_POWER_SPEED, SND_POWER_FURY, SND_POWER_INVINCIBLE, SND_POWER_CHARGE, SND_POWER_END,
  NSOUNDS
};
// Brani: titolo e menu, uno per ogni mappa della Terra (stile della mappa), il boss del Colosseo, la vittoria.
// MUS_CHICHENITZA dalla 0.5.1 e' il brano di Chichen Itza (il nome dell'identificativo cambia con la fusione).
enum MusicId { MUS_NONE = 0, MUS_TITLE, MUS_PIRAMIDI, MUS_CHICHENITZA, MUS_MURAGLIA, MUS_COLOSSEO, MUS_VICTORY, NMUSIC };
void audio_init(int rate);                       // una volta all'avvio (48000)
void audio_reset(void);                          // tutto zitto (ritorno al titolo)
void audio_play(int id, float pan, float vol);   // pan -1 sinistra .. 1 destra, vol 0..1 (distanza)
void audio_music(int id);                        // cambia brano con una dissolvenza breve; lo stesso brano continua; MUS_VICTORY suona una volta poi silenzio; MUS_NONE sfuma
void audio_set_volume(int music, int sfx);       // 0..10 ciascuno (10 = pieno)
void audio_duck(bool on);                        // pausa: musica piu' bassa
void audio_render(int16_t* out, int frames);     // stereo interlacciato, frames campioni per canale
const char* audio_sound_name(int id);
const char* audio_music_name(int id);

// --- aggiunte: per il registro del core e per tools/test_audio.cpp ---

struct AudioStats {
  int voices, voices_peak;        // voci degli effetti che suonano ora / massimo dall'ultimo azzeramento
  int music_voices;               // voci della musica che suonano ora
  unsigned plays, merged;         // richieste accettate; quante si sono fuse con una voce gia' accesa
  unsigned stolen, dropped;       // voci rubate (sfumate in 10 ms); richieste scartate (limite pieno di suoni piu' importanti)
  unsigned blocks, limited;       // blocchi di 64 campioni prodotti; quelli in cui il limitatore ha abbassato il volume
  float lim_min;                  // guadagno piu' basso usato dal limitatore (1 = mai intervenuto)
  float peak;                     // picco dell'uscita (1 = fondo scala)
  unsigned bad;                   // valori non validi (NaN o infiniti) trovati e azzerati
  float end_max;                  // inviluppo piu' alto di una voce nell'istante in cui si spegne (deve essere 0)
  int parse_errors;               // errori nei testi dei brani (dettagli in audio_music_text)
  int init_samples;               // campioni sintetizzati da audio_init per tarare il volume degli effetti
};
void audio_get_stats(AudioStats* st);
void audio_clear_stats(void);
// Durata del brano in secondi; *loop_start (se non nullo) = punto da cui riparte, negativo se suona una volta.
float audio_music_length(int id, float* loop_start);
// Testo del brano battuta per battuta (accordi, melodia, note fuori accordo sui tempi forti), con un
// riassunto e gli eventuali errori di lettura. Restituisce i caratteri scritti.
int audio_music_text(int id, char* buf, int size);
// Rifa' da zero la taratura di tutti gli effetti (ferma gli effetti accesi), scrive in buf la tabella CAL_TAB di
// audio.cpp e restituisce quanti suoni non corrispondono a quella in uso (0 = tabella giusta).
int audio_calibration_table(char* buf, int size);
