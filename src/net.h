// Gioco in rete nella stessa Wi-Fi (0.5.4), ognuno sul suo schermo. Solo nel core (non nelle prove
// del gioco): prese UDP, ricerca delle partite, sala d'attesa, comandi.
//
// Una console crea la partita (risponde alle ricerche sulla porta 55410, o sulle 9 dopo se e'
// occupata); le altre la cercano mandando un messaggio a tutta la rete, si uniscono (stessa
// versione e stesse mappe, lo controlla chi ha creato la partita) e aspettano che cominci. Fino a 4
// console, un giocatore per console: chi crea e' il giocatore 1, gli altri il primo posto libero.
//
// In partita tra le console passano solo i comandi (tasti e levette di ogni fotogramma): il gioco
// fa la stessa partita, bit per bit, su ogni console. La console che ha creato la partita decide:
// a ogni suo fotogramma mette insieme i comandi di tutti (i suoi subito, quelli delle altre console
// nell'ordine in cui le hanno letti) e li manda a tutte; le altre fanno un fotogramma quando lo
// ricevono, con 1-8 fotogrammi di scorta per assorbire gli scatti della Wi-Fi (la scorta cresce
// quando restano senza e cala quando avanza). Cosi' chi ha creato non aspetta mai nessuno, e i
// comandi delle altre console arrivano al gioco con il tempo di andata e ritorno piu' la scorta
// (in casa di solito 40-60 ms). Ogni messaggio ripete quello che l'altra parte non ha ancora
// confermato: un messaggio perso non perde comandi.
//
// Ogni mezzo secondo chi ha creato la partita manda anche l'impronta del gioco (game_state_hash):
// se su una console viene diversa, quella console esce dalla partita (succede solo con versioni
// del gioco che non fanno la stessa partita). Una console che si scollega (o non risponde per 10
// secondi) esce: il suo eroe lascia la partita, gli altri continuano. Se esce chi l'ha creata, le
// altre tornano al titolo. I progressi si salvano solo su chi ha creato la partita; le altre
// giocano con i suoi (save_guest_begin) e tengono i loro salvataggi come sono.
//
// Variabili d'ambiente per le prove:
//   PUPAZZI_RETE=crea[:N]      crea subito la partita (salvataggio 1) e comincia quando ci sono N
//                              altre console (1 se manca)
//   PUPAZZI_RETE=unisciti      cerca e si unisce alla prima partita che trova (unisciti!: anche a
//                              una che non va, per provare i rifiuti)
//   PUPAZZI_RETE_PORTA=n       prima porta al posto di 55410
//   PUPAZZI_RETE_PERDITA=p     butta il p per cento dei messaggi in uscita
//   PUPAZZI_RETE_RITARDO=a[+b] ritarda i messaggi in uscita di a ms, piu' fino a b ms a caso
//   PUPAZZI_RETE_ATTESA=ms     dopo quanto una console che non risponde esce (10000)
//   PUPAZZI_RETE_VERSIONE=v    finge di essere la versione v (prova del rifiuto)
#pragma once
#include <stdint.h>
#include "game.h"

void net_init(void);                 // retro_load_game: variabili d'ambiente, partita automatica (prove)
void net_shutdown(void);             // retro_unload_game, retro_deinit, retro_reset: saluta e chiude
// A ogni fotogramma, prima del gioco: legge la rete, segue ricerca e sala d'attesa (game_net_view),
// comincia o finisce la partita in rete (game_net_begin, game_net_end).
void net_frame_begin(void);
bool net_session(void);              // partita in rete in corso su questa console
// I comandi di questa console per questo fotogramma (solo in partita); tap: START premuto e
// lasciato subito.
void net_local_pad(const Pad& pad, bool tap);
// Il prossimo fotogramma della partita, se si puo' fare adesso (false: si aspetta la rete, o per
// questo fotogramma si e' gia' fatto abbastanza). consoles: posti con una console collegata;
// silent: console che non rispondono da un po' (solo per l'HUD).
bool net_step(Pad pads[MAX_PLAYERS], bool taps[MAX_PLAYERS], uint8_t* consoles, uint8_t* silent);
void net_step_done(uint64_t hash);   // dopo game_update: controllo dell'impronta
void net_frame_end(void);            // fine del fotogramma: invii
void net_request(int req, int arg);  // richieste dei menu (NR_* in game.h)
uint32_t net_frame_no(void);         // ultimo fotogramma della partita in rete fatto (per le tracce)

// Per il riquadro delle prestazioni e il registro.
struct NetStats {
  bool on;                           // partita in rete in corso
  bool host;
  int rtt_ms;                        // tempo di andata e ritorno (media); con piu' console il peggiore
  int buffer;                        // fotogrammi di scorta (le altre console)
  int target;                        // scorta voluta
  unsigned stalls;                   // volte rimasti senza fotogrammi
  int players;                       // console in partita
};
void net_stats(NetStats* s);
