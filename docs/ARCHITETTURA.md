# Com'è fatto Pupazzi

Per chi lavora sul codice: come è diviso, quali schemi usa (con i loro nomi noti), cosa succede a
ogni fotogramma, cosa tiene la partita identica su ogni console e come si prova. Il resto (regole,
comandi, decisioni) è nel README e in SPECIFICA.md.

## Cartelle

| Cartella | Cosa c'è |
|---|---|
| `src/platform/` | `core.cpp`: il core libretro (RetroArch chiama da qui: comandi, fotogrammi, sonoro, opzioni, registro, prestazioni); `plat`: registro e orologio; `sysinfo`: batteria e carico della CPU; `libretro.h` (di terzi) |
| `src/engine/` | il motore, che del gioco non sa nulla: matematica (`vmath.h`, `dmath.h`), mesh, i due renderer (`swr` software, `glr` OpenGL ES), testi e caratteri (`hud`), stile dell'interfaccia (`ui`), lingue (`lang`), sintetizzatore e mixer (`audio`) |
| `src/game/` | l'API del gioco per il core (`game.h`) e i salvataggi (`save`) |
| `src/game/sim/` | la partita: regole, eroi, mostri, boss, carte, mappe, percorsi, giocatori automatici. Deterministica |
| `src/game/screens/` | le schermate (menu, pausa, scelta delle carte), una per file |
| `src/game/view/` | la vista: telecamera, da dove vengono i suoni, musica, modelli, geometria delle mappe, disegno e HUD |
| `src/net/` | il gioco in rete nella stessa Wi-Fi |
| `tests/` | le prove (doctest) e gli script delle prove con più copie del gioco |
| `tools/` | il frontend di prova (`host.cpp`), i generatori di testi e caratteri, il finto Remote RetroPad |
| `third_party/` | componenti di terzi con le loro licenze: caratteri, doctest |
| `maps/`, `lang/` | le mappe di serie e i testi in 7 lingue (sorgenti dei dati incorporati nel core) |
| `console/`, `deploy.sh` | voce di menu e avvio sulla console, installazione dal PC |
| `docs/` | questo file e l'immagine di anteprima |

I sorgenti includono a partire da `src/` (`#include "game/sim/level.h"`).

## Livelli

```
                        platform/core.cpp  (RetroArch)
              ┌─────────────────┼──────────────────┐
              ▼                 ▼                  ▼
          game/view ───────►  game  ◄───────────  net
   (legge la partita:    (game.h: game/sim   (la cambia solo
    const Game&)          e game/screens)     con game_net_*)
              │                 │                  │
              └────────────────►▼◄─────────────────┘
                              engine
          (tutti usano anche platform/plat: registro e orologio)
```

- `engine/` non include niente del gioco: renderer, sonoro, testi e interfaccia servono qualunque
  gioco (il numero della versione, per esempio, `ui::version` lo riceve da chi chiama).
- `game/` è la partita: `game/sim` (le regole) e `game/screens` (menu, pausa, scelta delle carte),
  che cambiano lo stato e in rete girano uguali su tutte le console. Non sa che esiste una vista:
  annuncia quello che succede (suoni, movimenti della telecamera) con gli eventi e basta, e non
  costruisce modelli né geometria da disegnare. Misure, tempi e colori che le regole condividono con
  i disegni stanno in `game/sim/shapes.h`.
- `game/view/` legge la partita (`const Game&`) e non la cambia mai; per disegnare i menu chiama le
  funzioni `*_hud` delle schermate, che a loro volta leggono soltanto.
- `net/` cambia la partita solo attraverso le funzioni `game_net_*` di `game.h`; per il resto usa
  l'elenco delle mappe (per controllare che tutte le console abbiano le stesse) e i salvataggi.
- `platform/core.cpp` mette insieme tutto; nessuno lo include.

## Schemi usati

**Architettura a livelli** (vedi sopra): ogni cartella ha un compito e le dipendenze vanno in un
verso solo.

**Model-View.** La partita (`Game`, in `game/sim`) è il modello: dati semplici, deterministici,
uguali su ogni console. La vista (`View`, in `game/view`) è di questa console: telecamera,
inquadratura (televisore o schermo della console, e dalla 0.5.6 distanza, inclinazione e dettagli
scelti in GRAFICA), da dove arriva un suono, quale musica, la geometria della mappa e i modelli da
disegnare. `view_build_frame` legge la partita e prepara il fotogramma; quando la partita carica
un'altra mappa, la vista ne costruisce la geometria (`LevelMeshes`). In rete ogni console ha la sua
vista della stessa partita.

Le impostazioni di questa console (pausa: mira, volumi, GRAFICA) stanno in `save_data()`: la pausa,
che in rete gira uguale su tutte le console, le cambia solo sulla console di chi la comanda; il core
le legge a ogni fotogramma e le passa alla vista (`view_set_gfx`), al renderer e a RetroArch (la
risoluzione con `SET_GEOMETRY`). La partita non le legge mai: non cambiano il gioco.

**Observer.** La partita non chiama la vista: annuncia gli eventi con `GameEvents` (`game.h`):
un suono in un punto del mondo, un suono dell'interfaccia, un cambio di telecamera. La vista si
registra con `game_set_events(g, view_events(v))`; senza nessuno in ascolto (prove) gli eventi si
perdono e la partita va avanti uguale.

**State.** Ogni schermata è uno stato con tre funzioni: cosa fa quando si apre, a ogni passo e cosa
disegna (`struct Screen` in `game/screens/screens.h`). La tabella `SCREENS` le elenca nell'ordine di
`SC_*`; `go_screen` cambia schermata e chiama l'ingresso; `screens_update` e `screens_hud` passano
alla schermata in corso. Al posto dei grandi `switch` di prima. `SC_PLAY` è la partita stessa: lì
contano `play_update` (regole), la pausa e la scelta delle carte (`pause.cpp`, `cards.cpp`).

**Strategy.** I due renderer fanno la stessa cosa in due modi: ricevono la stessa descrizione del
fotogramma (`FrameDesc`, `engine/render.h`) e la disegnano con la GPU (`glr`) o con la CPU (`swr`).
Il core sceglie una volta quale usare (`VideoOut` in `core.cpp`, che disegna il fotogramma e lo passa
a RetroArch) e poi chiama sempre la stessa interfaccia.

**Niente stato globale nel gioco.** Tutto lo stato di una partita sta in un oggetto `Game`, creato
dal core (`game_create`) e passato a ogni funzione (`Game& g`); le prove ne creano uno loro. Dentro
`Game`, la `GameSession` tiene ciò che sopravvive a `game_init` (eventi, giocatori automatici,
campi delle distanze, vista della rete, avvisi dei menu). Restano a livello di modulo solo i servizi
del processo, uno per console: il file dei salvataggi (`save.cpp`), l'elenco delle mappe
(`maps.cpp`), i modelli (`models.cpp`), i buffer di lavoro del disegno, il sonoro, i renderer, il
core e la rete.

Il codice del core va linkato senza la libreria C++ (niente `new`, `delete`, eccezioni, funzioni
virtuali): oggetti creati con `calloc`, interfacce fatte di puntatori a funzione.

## Un fotogramma

`retro_run` (`core.cpp`), 60 volte al secondo:

1. legge i pad (e i comandi dalle altre console usate come controller);
2. in rete: scambia i comandi di questo fotogramma (`net_frame_begin`, `game_net_frame`, poi
   `net_frame_end`);
3. `game_update`: un passo di 1/60 di secondo della partita (regole, oppure la schermata in corso);
4. `view_step`: la musica che va con quello che c'è a schermo;
5. le impostazioni della GRAFICA alla vista e la risoluzione; l'immagine dentro il fotogramma (con
   il margine TV più piccola, con il nero attorno);
6. `view_build_frame`: telecamera, oggetti da disegnare e HUD, per l'immagine;
7. il renderer scelto (`VideoOut`) disegna l'immagine nel fotogramma (`FrameDesc`: `w`, `h`
   l'immagine, `fw`, `fh`, `ox`, `oy` il fotogramma e dove sta; l'HUD, scritto in pixel a 640×480,
   in scala con l'altezza dell'immagine) e lo passa a RetroArch;
8. il sonoro del fotogramma (800 campioni a 48 kHz).

## Partita identica su ogni console

Il gioco in rete e le prove contano sul fatto che la stessa partita dia lo stesso risultato bit per
bit su x86, ARM a 32 e a 64 bit, con ogni compilatore:

- passi fissi di 1/60 di secondo, mai il tempo vero;
- `-ffp-contract=off` (niente moltiplica-e-somma fusi) e trigonometria propria (`engine/dmath.h`);
- il generatore casuale della partita chiamato sempre in istruzioni separate (l'ordine di
  valutazione degli argomenti non è fissato in C++);
- `game_state_hash`: l'impronta della partita; in rete si confronta ogni 30 fotogrammi, nelle prove
  a ogni fotogramma (`PUPAZZI_TRACE=1`).

## Prove

- `make test`: le prove in `tests/` (doctest, `third_party/doctest/`). Ogni programma è un elenco di
  casi che girano in ordine e scrivono ogni controllo ("ok" o "ERRORE"); alla fine il riepilogo.
- `make test-gl`, `make test-audio`, `make test-net`, `make test-remote`: renderer GPU (Mesa), sonoro,
  gioco in rete con più copie del gioco, comandi dalle altre console.
- Il frontend di prova (`build/native/host`) carica il core come RetroArch: con `--hashes` scrive
  l'impronta di ogni immagine, con `--wav` il sonoro, con `--in` i tasti da premere. Con
  `PUPAZZI_TRACE=1` il registro ha l'impronta della partita a ogni fotogramma: confrontando impronte,
  immagini e sonoro prima e dopo una modifica si vede se il gioco è rimasto identico (così è stato
  verificato il riordino del codice).
- La CI su GitHub fa tutte queste prove a ogni modifica di `main`, e la release prima di pubblicare.

## Stile del codice

- Commenti in inglese; testi del gioco, registro e messaggi delle prove in italiano (i testi del
  gioco passano da `tr()` per le altre lingue).
- C++17 senza eccezioni né RTTI; due spazi di rientro, righe di solito entro i 140 caratteri;
  blocchi brevi su una riga sola e tabelle allineate a mano (`.editorconfig` per gli editor).
  Niente formattatore automatico: clang-format riscriverebbe a modo suo molte righe e le tabelle.
- Una schermata nuova: un valore in `SC_*` (`game_int.h`), un file in `game/screens/` con le sue
  funzioni, una riga in `SCREENS`. Un evento nuovo: un campo in `GameEvents` e la sua risposta in
  `view.cpp`.
