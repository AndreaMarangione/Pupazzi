// Salvataggio dei progressi sulla console: il file di testo pupazzi.sav, righe chiave=valore.
//
//   save_open(cartella)  una volta all'avvio (il core la chiama in retro_load_game)
//   save_data()          i dati in memoria: il gioco li legge e li cambia direttamente
//   save_write()         quando c'e' qualcosa da tenere (fine mappa, impostazioni cambiate); il core
//                        la chiama anche all'uscita (retro_deinit) per cio' che non e' ancora scritto
//
// Il file si scrive in modo atomico: prima pupazzi.sav.tmp (fflush + fsync), poi lo si rinomina
// sopra pupazzi.sav, che quindi e' sempre quello vecchio o quello nuovo intero (funziona anche
// sulla FAT della scheda SD). save_write non tocca il disco se i dati sono quelli gia' scritti
// (o letti); quando scrive costa un fsync, sulla scheda anche decine di millisecondi: va chiamata
// per un evento, non a ogni fotogramma. Se non riesce lo scrive una volta nel registro e
// restituisce false: il gioco va avanti con i dati in memoria e riprova alla chiamata dopo.
//
// Contenuto (le righe che cominciano con # si ignorano, l'ordine non conta):
//   versione=1             formato di chi l'ha scritto per ultimo
//   storia_pianeti=0       (dalla 0.5) storia del giro dei pianeti gia' vista (0 o 1); storia_vista
//                          della 0.4 (la storia di prima) resta nel file e non conta
//   mira.1=1 .. mira.4=1   mira di ogni posto giocatore: 0 extra morbida, 1 morbida, 2 media, 3 rapida
//   antialias=1            0 o 1
//   volume_musica=6        da 0 a 10 (fino alla 0.4 predefinito 7)
//   volume_effetti=8       da 0 a 10
//   mappe.terra=1          (dalla 0.5) mappe aperte del pianeta, in ordine di campagna: finire una
//                          mappa apre la successiva (anche nei giri dopo restano aperte); la Terra ne
//                          ha almeno 1, un altro pianeta si apre (1) battendo il boss di quello prima.
//                          Da 0 a 99.
//   giro.terra=1           (dalla 0.5) giro del pianeta, da 1 a 99: battuto il boss con almeno 10
//                          stelle del giro in corso si passa al giro dopo (mostri piu' forti) e le
//                          stelle del pianeta ripartono da zero
//   stelle.terra1=5        stelle della mappa nel giro in corso del suo pianeta (nome del file senza
//                          .txt): somma di 1 (finita), 2 (esploratore salvato), 4 (senza ricominciare
//                          la mappa da capo; fino alla 0.4: senza mai finire tutti a terra); 0 dopo
//                          un giro nuovo
// I pianeti si scrivono in minuscolo, nell'ordine di WORLD_* in level.h: terra, luna, marte, venere,
// mercurio, giove, saturno, urano, nettuno; nel file ci sono la Terra e i pianeti gia' raggiunti
// (con una mappa aperta o un giro oltre il primo).
// Dalla 0.4: mappe_sbloccate=N (mappe aperte della prateria, in ordine) non vale per la Terra, che
// e' nuova e ricomincia dalla prima mappa: la 0.5 non la legge e non la scrive, ma se c'e' resta nel
// file com'era, come le chiavi sconosciute (una 0.4 rimessa sulla console la ritrova). Le stelle
// della prateria restano sotto i loro nomi (stelle.prateria1...), che non sono mappe della Terra.
// Valori fuori dai limiti si riportano nei limiti, righe non valide si ignorano. Le chiavi che
// questa versione non conosce si riscrivono uguali in fondo al file: versioni diverse del gioco
// non si cancellano i dati a vicenda (regola: una chiave non cambia mai significato, una novita'
// usa una chiave nuova).
#pragma once
#include <stdint.h>

#define SAVE_VERSION 1
#define SAVE_PLAYERS 4
#define SAVE_MAX_MAPS 64
#define SAVE_ID_LEN 24              // come Level::id: al massimo 23 caratteri tra a-z A-Z 0-9 _ -
#define SAVE_WORLDS 9               // pianeti, come NWORLDS in level.h
#define SAVE_GIRO_MAX 99

// Stelle di una mappa: finita, esploratore salvato, senza ricominciare la mappa da capo.
enum { SAVE_STAR_DONE = 1, SAVE_STAR_EXPLORER = 2, SAVE_STAR_NO_WIPE = 4 };
enum { SAVE_NUOVO = 0, SAVE_LETTO, SAVE_ILLEGGIBILE };   // esito di save_open

struct SaveMap {
  char id[SAVE_ID_LEN];
  uint8_t stars;                    // SAVE_STAR_*
};

struct SaveData {
  int version;                      // formato del file letto (0: nessun file)
  bool story_seen;
  uint8_t aim[SAVE_PLAYERS];        // da 0 a 3 (predefinita 1, morbida)
  bool antialias;                   // predefinito acceso
  uint8_t music_vol, sfx_vol;       // da 0 a 10 (predefiniti 6 e 8)
  uint8_t maps_open[SAVE_WORLDS];   // mappe.<pianeta>: mappe aperte in ordine (Terra da 1, gli altri da 0)
  uint8_t giro[SAVE_WORLDS];        // giro.<pianeta>: da 1 a SAVE_GIRO_MAX
  int nmaps;                        // mappe con le stelle (quelle che non ci sono hanno 0 stelle)
  SaveMap maps[SAVE_MAX_MAPS];
};

// Ricorda la cartella e legge il file se c'e': manca -> valori predefiniti (SAVE_NUOVO);
// illeggibile -> valori predefiniti (SAVE_ILLEGGIBILE) e al primo salvataggio il file vecchio
// resta come pupazzi.sav.illeggibile. dir NULL o vuota: solo valori predefiniti, niente file.
// Si puo' richiamare: riparte da zero e rilegge.
int save_open(const char* dir);
// Com'e' andata save_open, per il registro: "nuovo", "letto" o "illeggibile: ..." con i dettagli.
const char* save_status(void);
// Percorso del file; "" prima di save_open (e allora save_write non fa nulla).
const char* save_path(void);
// Sempre valido: prima di save_open contiene i valori predefiniti.
SaveData* save_data(void);
// Stelle della mappa (0 se non c'e' o se il nome non e' valido).
uint8_t save_stars(const char* map_id);
// Aggiunge le stelle (OR con quelle che ci sono gia'): solo in memoria, poi serve save_write.
void save_add_stars(const char* map_id, uint8_t bits);
// Mette le stelle della mappa a bits (giro nuovo: 0), solo in memoria come save_add_stars.
void save_set_stars(const char* map_id, uint8_t bits);
// Nome del pianeta w nel file ("terra", "luna"...; "" fuori dai limiti).
const char* save_world_id(int w);
// Scrive il file (riporta nei limiti i valori in memoria). true se il file e' aggiornato.
bool save_write(void);
