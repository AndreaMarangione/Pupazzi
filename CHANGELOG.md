# Novità di Pupazzi

Tutte le modifiche importanti, versione per versione. Formato:
[Keep a Changelog](https://keepachangelog.com/it/1.1.0/); versioni: [SemVer](https://semver.org/lang/it/).
Le note di ogni release su GitHub si prendono da qui (la sezione con il numero della versione).

## [Non rilasciato]

### Cambiato

- Il gioco si costruisce e si prova da solo su GitHub a ogni modifica (CI); le release si fanno
  con un comando solo: GitHub compila, prova, prepara l'archivio e lo allega con queste note.
- I binari pronti (`dist/`) non sono più nel repository: stanno nell'archivio di ogni release.
- Il codice è riordinato con schemi noti, senza cambiare il gioco (partite, immagini e suoni
  identici, controllati fotogramma per fotogramma): cartelle per parte del programma, tutto lo
  stato della partita in un oggetto, partita separata dalla grafica (Model-View) con gli eventi
  (Observer), le schermate come stati (State).
- Le prove sono in `tests/` e usano doctest: alla fine di ogni programma il conto dei casi e dei
  controlli passati.

## [0.5.4] - 2026-10-30

### Aggiunto

- Gioco in rete nella stessa Wi-Fi: fino a 4 console, ognuna con il suo schermo e la telecamera sul
  suo eroe. Una console crea la partita (con il salvataggio scelto), le altre la trovano da sole e
  si uniscono; i progressi si salvano solo su chi l'ha creata.
- Bonus nelle casse e nelle anfore che si rompono: velocità, furia, invincibile, carica.
- Cancelli a battente nei quattro stili: si aprono cigolando verso chi aspetta e si richiudono
  sbattendo, con polvere e rimbalzo.
- Nome delle partite con la tastiera a schermo (anche subito dopo NUOVA PARTITA e con RINOMINA).
- Banner RITIRA LE TUE CARTE prima della scelta delle carte.
- Antialias fino a 8 campioni (NO, 4X, 8X), con il passaggio da solo a 4 se non si tengono i 60
  fotogrammi al secondo.
- Batteria in alto a destra; carico della CPU (e in rete il tempo di andata e ritorno) con SELECT.

### Rimosso

- La storia all'inizio della campagna.

### Corretto

- Revisione indipendente: chi tornava indietro appena passato il cancello poteva restare chiuso
  fuori; un messaggio di rete costruito apposta bloccava la console che aveva creato la partita;
  4 difetti piccoli.

## [0.5.3] - 2026-10-20

### Aggiunto

- Salvataggi a 4 posti: nuova partita, carica, gestisci (duplica, elimina), con il tempo di gioco.
- Sette lingue: italiano, inglese, spagnolo, portoghese, cinese, giapponese, hindi.
- Visuale più vicina sullo schermo della console, quella larga con l'HDMI (cambia da sola).

### Corretto

- Revisione indipendente: salvataggi illeggibili tenuti da parte, migrazione della 0.5.2 più
  sicura, testi delle carte che uscivano dai riquadri, a capo delle parole troppo lunghe.

## [0.5.2] - 2026-10-19

### Cambiato

- Interfaccia nuova in tutte le schermate e in partita: stile Orbita, colori Tramonto, caratteri
  Chakra Petch, forme calcolate per pixel uguali nel renderer software e in quello GPU.
- Pausa con la voce COMANDI che apre il riquadro dei tasti.

### Corretto

- Revisione indipendente: finestre di chi entra sopra la pausa, note d'errore delle mappe su più
  righe, nomi lunghi delle mappe accorciati.

## [0.5.1] - 2026-10-18

### Aggiunto

- Chichén Itzá al posto di Machu Picchu, con i cenote in cui si cade.
- Scorpione e arciere di terracotta al posto di sciacallo e lama.

### Cambiato

- Visuale da 22 a 30 m; gladiatore che insegue e combatte (fendente, carica, giavellotti, fasi).
- Mappe chiuse e tutte giocabili, controllate al caricamento; l'acqua blocca anche saltando.

### Corretto

- Si poteva camminare fuori dalla parte giocabile delle Piramidi saltando nel Nilo.

## [0.5.0] - 2026-10-17

### Aggiunto

- Tema del sistema solare: la Terra con Le Piramidi, Machu Picchu, La Grande Muraglia e il
  Colosseo con il Gladiatore gigante; eroi Astro, Mochi, Marzio e Zolfo.
- Vite (3 a testa per mappa), giri per pianeta con le stelle, tessere dei giocatori.
- Le altre console come controller senza ritardo (il gioco legge da solo il Remote RetroPad).
- Musiche ed effetti nuovi.

## [0.4.0] - 2026-10-15

### Aggiunto

- Carte a ogni cancello (anche combinate tra due eroi), 3 stelle per mappa con il prigioniero
  nascosto, salvataggio, storia, mostri appesi ai fili che si liberano.
- Sonoro: sintetizzatore nel gioco, effetti e musiche, volumi nella pausa.

## [0.3.2] - 2026-10-15

### Cambiato

- Visuale più larga, antialias 4X sulla GPU, testi rifatti a 640×480.
- Salto su R2 e bomba su L2; quattro livelli di mira (extra morbida in più).

## [0.3.1] - 2026-10-14

### Cambiato

- Mira a ventaglio per tutti; levetta destra più morbida, con la scelta della mira nella pausa.

## [0.3.0] - 2026-10-13

### Aggiunto

- Grafica in stile cartone, mappe da file di testo, quattro personaggi con colpo normale e
  speciale, mostri della prateria e boss albero gigante, bombe, talismani, menu completi.

## [0.2.0] - 2026-10-11

### Aggiunto

- Primo prototipo giocabile: due nemici, livelli a zone con i cancelli, HUD, pausa, riquadro delle
  prestazioni.

## [0.1.0] - 2026-10-08

### Aggiunto

- Prova tecnica: core per RetroArch con renderer software e GPU, stanza di prova, benchmark.
