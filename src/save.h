// Salvataggi sulla console, file di testo con righe chiave=valore (dalla 0.5.3 piu' file):
//
//   pupazzi.sav             impostazioni di questa console: mira di ogni posto, antialias, volumi, lingua
//   partita1.sav .. partita4.sav    le partite (i quattro salvataggi della prima schermata)
//
//   save_open(cartella)     una volta all'avvio (il core la chiama in retro_load_game): legge le
//                           impostazioni; nessuna partita e' attiva finche' il gioco non ne crea o
//                           ne carica una (save_slot_new, save_slot_load)
//   save_data()             i dati in memoria (impostazioni e partita attiva): il gioco li legge e
//                           li cambia direttamente
//   save_write()            quando c'e' qualcosa da tenere (fine mappa, impostazioni cambiate); il
//                           core la chiama anche all'uscita (retro_deinit) per cio' che non e' ancora
//                           scritto. Scrive le impostazioni e la partita attiva, ognuna solo se cambiata
//
// Ogni file si scrive in modo atomico: prima NOME.tmp (fflush + fsync), poi lo si rinomina sopra
// NOME, che quindi e' sempre quello vecchio o quello nuovo intero (funziona anche sulla FAT della
// scheda SD). Una scrittura costa un fsync, sulla scheda anche decine di millisecondi: va fatta per
// un evento, non a ogni fotogramma. Se non riesce lo scrive una volta nel registro e restituisce
// false: il gioco va avanti con i dati in memoria e riprova alla chiamata dopo.
//
// pupazzi.sav (le righe che cominciano con # si ignorano, l'ordine non conta):
//   versione=2             formato di chi l'ha scritto per ultimo (1: fino alla 0.5.2, tutto in un file)
//   mira.1=1 .. mira.4=1   mira di ogni posto giocatore: 0 extra morbida, 1 morbida, 2 media, 3 rapida
//   antialias=1            0 spento, 1 il migliore che la GPU regge, 4 o 8 campioni (dalla 0.5.4; prima
//                          0 o 1: le versioni di prima leggono ogni numero diverso da 0 come acceso)
//   volume_musica=6        da 0 a 10 (fino alla 0.4 predefinito 7)
//   volume_effetti=8       da 0 a 10
//   lingua=it              it, en, es, pt, zh, ja, hi (dalla 0.5.3)
//   salvataggi=1           (dalla 0.5.3) niente da prendere dal file della 0.5.2 (gia' diventato
//                          partita1.sav, o non c'era); 0 se non ci si e' riusciti: si riprova all'avvio
// partitaN.sav:
//   versione=2
//   nome=ANDREA            (dalla 0.5.4) nome della partita: fino a 12 tra A-Z 0-9 spazio - ' ! ? .
//                          (le minuscole diventano maiuscole); senza, si mostra "PARTITA N"
//   mappe.terra=1          mappe aperte del pianeta, in ordine di campagna: finire una mappa apre la
//                          successiva (anche nei giri dopo restano aperte); la Terra ne ha almeno 1, un
//                          altro pianeta si apre (1) battendo il boss di quello prima. Da 0 a 99.
//   giro.terra=1           giro del pianeta, da 1 a 99: battuto il boss con almeno 10 stelle del giro in
//                          corso si passa al giro dopo (mostri piu' forti) e le stelle ripartono da zero
//   stelle.terra1=5        stelle della mappa nel giro in corso del suo pianeta (nome del file senza
//                          .txt): somma di 1 (finita), 2 (esploratore salvato), 4 (senza ricominciare
//                          la mappa da capo); 0 dopo un giro nuovo
//   tempo=3600             secondi di gioco (dalla 0.5.3)
// I pianeti si scrivono in minuscolo, nell'ordine di WORLD_* in level.h: terra, luna, marte, venere,
// mercurio, giove, saturno, urano, nettuno; nel file ci sono la Terra e i pianeti gia' raggiunti.
//
// Dalla 0.5.2 alla 0.5.3: se pupazzi.sav ha ancora i progressi (mappe.*, giro.*, stelle.*,
// storia_pianeti) e non c'e' nessuna partita, diventano partita1.sav; in pupazzi.sav restano (una
// 0.5.2 rimessa sulla console li ritrova) e la riga salvataggi=1 dice che sono gia' stati presi.
// Se c'e' gia' una partita i progressi vecchi non si prendono piu'.
// Dalla 0.4: mappe_sbloccate, storia_vista e le stelle della prateria restano dove sono, come le
// chiavi sconosciute; dalla 0.5.4 anche storia_pianeti (la storia non c'e' piu').
//
// Un file che non si legge tutto (rovinato, errore della scheda, troppo grande) alla prima scrittura
// resta come NOME.illeggibile; se non lo si riesce a mettere da parte non lo si sostituisce.
// In ogni file: valori fuori dai limiti si riportano nei limiti, righe non valide si ignorano. Le
// chiavi che questa versione non conosce si riscrivono uguali in fondo al file: versioni diverse del
// gioco non si cancellano i dati a vicenda (regola: una chiave non cambia mai significato, una
// novita' usa una chiave nuova). Nel file di una partita le impostazioni non contano (si ignorano).
#pragma once
#include <stdint.h>

#define SAVE_VERSION 2
#define SAVE_PLAYERS 4
#define SAVE_MAX_MAPS 64
#define SAVE_ID_LEN 24              // come Level::id: al massimo 23 caratteri tra a-z A-Z 0-9 _ -
#define SAVE_WORLDS 9               // pianeti, come NWORLDS in level.h
#define SAVE_GIRO_MAX 99
#define SAVE_SLOTS 4                // partite
#define SAVE_LANGS 7                // lingue (lang.h)
#define SAVE_NAME_LEN 13            // nome di una partita: al massimo 12 caratteri (save_name_char)

// Stelle di una mappa: finita, esploratore salvato, senza ricominciare la mappa da capo.
enum { SAVE_STAR_DONE = 1, SAVE_STAR_EXPLORER = 2, SAVE_STAR_NO_WIPE = 4 };
enum { SAVE_NUOVO = 0, SAVE_LETTO, SAVE_ILLEGGIBILE };   // esito di una lettura

struct SaveMap {
  char id[SAVE_ID_LEN];
  uint8_t stars;                    // SAVE_STAR_*
};

struct SaveData {
  int version;                      // formato del file letto per ultimo (0: nessun file)
  // Impostazioni (pupazzi.sav).
  uint8_t aim[SAVE_PLAYERS];        // da 0 a 3 (predefinita 1, morbida)
  uint8_t antialias;                // 0, 1 (il migliore, predefinito), 4 o 8 campioni
  uint8_t music_vol, sfx_vol;       // da 0 a 10 (predefiniti 6 e 8)
  uint8_t lang;                     // LANG_* (lang.h), predefinita l'italiano
  // Partita (partitaN.sav).
  char name[SAVE_NAME_LEN];         // nome=: "" finche' non gliene si da' uno (si mostra PARTITA N)
  uint8_t maps_open[SAVE_WORLDS];   // mappe.<pianeta>: mappe aperte in ordine (Terra da 1, gli altri da 0)
  uint8_t giro[SAVE_WORLDS];        // giro.<pianeta>: da 1 a SAVE_GIRO_MAX
  int nmaps;                        // mappe con le stelle (quelle che non ci sono hanno 0 stelle)
  SaveMap maps[SAVE_MAX_MAPS];
  uint32_t seconds;                 // tempo di gioco
};

// Ricorda la cartella e legge pupazzi.sav se c'e': manca -> valori predefiniti (SAVE_NUOVO);
// illeggibile -> valori predefiniti (SAVE_ILLEGGIBILE) e alla prima scrittura il file vecchio resta
// come pupazzi.sav.illeggibile. dir NULL o vuota: solo valori predefiniti, niente file. Nessuna
// partita attiva (i suoi dati sono quelli predefiniti). Si puo' richiamare: riparte da zero.
int save_open(const char* dir);
// Com'e' andata la lettura delle impostazioni, per il registro: "nuovo", "letto" o "illeggibile: ...".
const char* save_status(void);
// Percorso di pupazzi.sav; "" prima di save_open (e allora save_write non fa nulla).
const char* save_path(void);
// Sempre valido: prima di save_open contiene i valori predefiniti.
SaveData* save_data(void);
// Stelle della mappa nella partita attiva (0 se non c'e' o se il nome non e' valido).
uint8_t save_stars(const char* map_id);
// Stelle della mappa in dati letti a parte (save_slot_peek).
uint8_t save_stars_in(const SaveData* d, const char* map_id);
// Aggiunge le stelle (OR con quelle che ci sono gia'): solo in memoria, poi serve save_write.
void save_add_stars(const char* map_id, uint8_t bits);
// Mette le stelle della mappa a bits (giro nuovo: 0), solo in memoria come save_add_stars.
void save_set_stars(const char* map_id, uint8_t bits);
// Nome del pianeta w nel file ("terra", "luna"...; "" fuori dai limiti).
const char* save_world_id(int w);
// Scrive le impostazioni e la partita attiva (riporta nei limiti i valori in memoria). true se i
// file sono aggiornati.
bool save_write(void);

// --- partite (da 0 a SAVE_SLOTS - 1) ---
// Partita attiva, -1 nessuna.
int save_slot(void);
// Percorso del file della partita i ("" prima di save_open).
const char* save_slot_path(int i);
// Legge la partita i in out (solo i dati della partita; le impostazioni di out restano quelle
// predefinite) senza toccare quella attiva. SAVE_NUOVO: il file non c'e' (posto libero).
int save_slot_peek(int i, SaveData* out);
// La partita i diventa quella attiva, letta dal suo file. false se il file non c'e'. Se e'
// illeggibile si parte dai valori predefiniti (e il file vecchio, alla prima scrittura, resta come
// partitaN.sav.illeggibile): true.
bool save_slot_load(int i);
// Partita nuova (valori predefiniti, con il nome name se c'e') al posto i, attiva e subito scritta
// (anche se non riesce a scriverla resta attiva in memoria). false se i e' fuori dai limiti o se la
// scrittura non riesce.
bool save_slot_new(int i, const char* name = 0);
// Cambia il nome della partita i (attiva o no) e la scrive; il resto del file resta com'e'. false se
// il posto e' libero, il file non si legge o non si scrive, o il nome non e' valido.
bool save_slot_rename(int i, const char* name);
// Il carattere c puo' stare nel nome di una partita (A-Z, 0-9, spazio, - ' ! ? .).
bool save_name_char(char c);
// Copia la partita from al posto to (prima si scrive quella attiva, se e' from). false se non riesce.
bool save_slot_copy(int from, int to);
// Toglie il file della partita i; se era quella attiva non c'e' piu' una partita attiva.
bool save_slot_delete(int i);
// Com'e' andata l'ultima lettura di una partita, per il registro.
const char* save_slot_status(void);
// Codice della lingua l nel file ("it", "en"...; "" fuori dai limiti).
const char* save_lang_id(int l);
