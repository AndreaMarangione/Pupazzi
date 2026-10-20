# Pupazzi — 0.5.3

Gioco cooperativo da 1 a 4 giocatori per l'Anbernic RG35XX Pro (Stock OS MOD), ispirato a
Brawl Stars: il giro del **sistema solare**, pianeta dopo pianeta, attraversando mappe fatte di
zone piene di mostri. La specifica completa, decisa insieme, è in `SPECIFICA.md`.

Le versioni prima: la 0.3 ha dato la grafica in stile cartone, le mappe, i 4 personaggi iniziali,
le bombe e i menu; la 0.3.1 e la 0.3.2 la mira a ventaglio, la levetta destra più morbida (quattro
livelli), l'antialias, salto e bomba su R2 e L2; la 0.4 le carte, le 3 stelle per mappa, i
salvataggi e il sonoro.

La 0.5 cambia tema: niente più pupazzi e burattinaio, si parte dalla **Terra** con le sue
meraviglie (oggi **Le Piramidi**, **La Grande Muraglia**, **Chichén Itzá** e il boss nel **Colosseo**).
Mappe più grandi di due terzi e disegnate morbide, senza quadretti; i quattro eroi **Astro**,
**Mochi**, **Marzio** e **Zolfo**; mostri e boss nuovi; **3 vite** a testa per mappa; i **giri**
di ogni pianeta, che si aprono con 10 stelle; le icone dei giocatori in alto a sinistra; visuale
fino a 30 m; musiche nuove; i controller via Wi-Fi senza ritardo. La Luna e gli altri pianeti
arrivano con le prossime versioni.

La 0.5.1, dopo la prova della 0.5: visuale minima più lontana (22 m, anche da soli); acqua che
non si attraversa più nemmeno saltando e mappe tutte giocabili (alle Piramidi dal Nilo si passava
nelle altre zone), con il paesaggio attorno ridotto e controlli automatici su ogni mappa; il
**Gladiatore** che insegue e combatte; **Chichén Itzá** al posto di Machu Picchu, con i **cenote**
in cui si cade e si perde una vita; **scorpioni** e **arcieri di terracotta** al posto di
sciacalli e lama.

La 0.5.2 rifà tutta l'interfaccia, scelta da Andrea sui mockup: stile **Orbita** con i colori
**Tramonto**. Pannelli prugna con due angoli tagliati e il bordo che sfuma dall'ambra al rosa, voce
scelta con la stessa sfumatura e un alone caldo, titoli bianchi con l'alone, caratteri Chakra Petch
con gli accenti veri, tutto dentro il margine che i televisori tagliano; anche l'HUD in partita. La
pausa non elenca più i tasti: li mostra la voce **COMANDI**, in un riquadro a parte. La scelta della
mappa è al centro dello schermo, con un pianeta alla volta.

La 0.5.3 aggiunge sostanza: **quattro salvataggi** (prima schermata con NUOVA PARTITA, CARICA
SALVATAGGIO, GESTISCI SALVATAGGI per duplicarli ed eliminarli, LINGUA, ESCI), **sette lingue**
(italiano, inglese, spagnolo, portoghese, cinese, giapponese, hindi) e la **visuale più vicina
sullo schermo della console**: da 15 a 30 m, e da 22 a 30 m quando l'HDMI è collegato (lo capisce da
solo, anche se lo si attacca o stacca a partita iniziata).

## Installazione

Console accesa, in Wi-Fi, con SSH attivo. Dal PC, dentro questa cartella:

    ./deploy.sh installa            # IP predefinito 192.168.1.228
    ./deploy.sh installa 192.168.1.50

Senza SSH: scheda SD nel PC e `./deploy.sh sd /percorso/della/scheda/Roms`.

Sulla console compare **App Center → APPS → Pupazzi**. Un aggiornamento della MOD svuota la
cartella APPS: dopo basta rifare `./deploy.sh installa`. Per togliere tutto: `./deploy.sh rimuovi`
(il salvataggio resta, vedi sotto).

## Comandi

| Tasto | In partita | Nei menu |
|---|---|---|
| levetta sinistra o croce | muoversi | scegliere |
| levetta destra | mirare | — |
| R2 | salto (tenuto: più alto) | — |
| L2 | lasciare una bomba | — |
| R1 tenuto | colpo normale | — |
| L1 | colpo speciale, quando è carico | — |
| A | — | conferma (anche la carta scelta) |
| B | — | indietro |
| START (pressione breve) | pausa, da qualunque giocatore | conferma |
| SELECT (pressione breve, giocatore 1) | riquadro delle prestazioni | |
| SELECT+START tenuti (giocatore 1) | esce e torna al menu della console | |

**Mira**: inclinando la levetta destra l'eroe si gira in quella direzione e a terra un ventaglio
mostra dove vanno i colpi e fin dove arrivano; R1 spara lì e i colpi coprono il ventaglio.
Lasciando la levetta la mira resta dov'era; poi, camminando, si spara dritto davanti. Per gli
speciali che arrivano lontano (salto di Mochi, eruzione di Zolfo) più si inclina la levetta, più
lontano arrivano: quando lo speciale è carico, a terra compare anche il cerchio giallo dove atterra.

Quanto è pronta la levetta destra si sceglie nella pausa, voce **MIRA** (← → oppure A), ognuno
per il suo controller: **extra morbida** (la più calma: filtra di più e serve inclinarla di più),
**morbida** (quella di partenza), **media**, **rapida** (segue la levetta quasi subito). Resta
salvata, per ogni controller (giocatore 1, 2, 3, 4).

**Antialias**: con la GPU i bordi vengono lisciati (4 campioni per pixel). Dalla pausa, voce
**ANTIALIAS**, si spegne e si riaccende per tutti, e resta salvato; il riquadro delle prestazioni
(SELECT) scrive "GPU+AA" quando è acceso. Se la GPU non lo permette la voce dice ASSENTE e si
gioca senza. Se il gioco si chiudesse da solo nei primi secondi con l'antialias, alla partenza
dopo riparte senza e lo salva spento (file `out/aa_in_prova.txt` e riga "antialias spento
all'avvio" nel registro); dalla pausa si riaccende.

Tenere premuto START due secondi apre il menu di RetroArch come negli altri giochi.

## Menu

Prima schermata (gli eroi a sinistra, le voci a destra):

- **NUOVA PARTITA**: una partita nuova nel primo dei quattro posti liberi; se sono tutti pieni si
  sceglie quale sovrascrivere (con conferma).
- **CARICA SALVATAGGIO**: i quattro posti, ognuno con il pianeta più lontano raggiunto e il suo
  giro, le stelle, le mappe aperte e il tempo di gioco; A carica e si va avanti.
- **GESTISCI SALVATAGGI**: gli stessi posti; A su una partita apre **DUPLICA** (una copia nel primo
  posto libero) ed **ELIMINA** (con conferma: A sì, B no).
- **LINGUA**: italiano, inglese, spagnolo, portoghese (del Portogallo), cinese semplificato,
  giapponese, hindi; vale subito e resta salvata.
- **ESCI**: torna al menu della console.

Poi **Insieme** (Contro arriva con la 0.6) → la prima volta la **storia** (A mostra tutte le
righe, poi A va avanti) → mappa → ogni giocatore sceglie il suo eroe col suo pad (A per entrare,
← → per scegliere, A per "pronto") → partita → fine mappa con le statistiche (mostri sconfitti,
danni, cadute e il migliore) e le stelle → si sceglie di nuovo e si passa alla mappa successiva.
Dopo il Colosseo si torna alla prima mappa della Terra (vedi **Giri**).

Nel menu delle mappe, al centro: il pianeta con il suo **giro**, le mappe numerate con le stelle
del giro in corso (si aprono in ordine: all'inizio c'è solo la prima, ogni mappa finita apre la
successiva, le altre dicono "chiusa"; A su una mappa chiusa dice come si apre), le stelle del giro
con le tacche e quante ne servono per il giro dopo ("★ 4/12 ... 10 → GIRO 2") e i pianeti in arrivo.

Durante la partita chi preme un tasto entra con una piccola finestra per scegliere l'eroe, mentre
gli altri continuano a giocare. Dalla pausa (RIPRENDI, COMANDI, MIRA, ANTIALIAS, MUSICA, EFFETTI,
LASCIA LA PARTITA, TORNA AL MENU) ognuno può rivedere i tasti (COMANDI: un riquadro con tutti i
comandi, A o B lo chiudono), scegliere la sua mira, accendere o spegnere l'antialias, cambiare i
volumi, lasciare la partita o tornare al menu delle mappe.

## Come si gioca

- **Storia**: Astro parte dalla Terra con gli amici della Luna, di Marte e di Venere: il giro dei
  pianeti comincia. Ogni pianeta ha i suoi mostri e un boss; in ogni mappa c'è una capsula
  nascosta con un esploratore da salvare.
- **Mappe**: 3 zone in fila, ognuna grande quanto la visuale più larga (31×21 m). Quando una zona
  è libera il cancello si apre; appena qualcuno entra nella zona dopo, il cancello si richiude
  alle spalle del gruppo (chi è rimasto indietro viene portato dentro).
- **Vite**: 3 a testa per mappa (la carta VITA EXTRA ne dà una in più).
  - Da soli: ogni caduta toglie una vita e la zona ricomincia; alla terza caduta la mappa
    ricomincia da capo.
  - In più giocatori: chi cade rientra dopo 5 secondi accanto a un compagno finché ha vite; senza
    vite resta fuori fino alla fine della mappa. Se cadono **tutti nello stesso momento**, anche
    con vite rimaste, la mappa ricomincia da capo.
  - Mappa da capo: 3 vite a testa, carte da riscegliere, zona 1, capsula di nuovo chiusa. Chi
    lascia la partita e rientra nella stessa mappa ritrova le vite che aveva (e le sue
    statistiche); chi entra nuovo ne ha 3.
  - Se il boss va giù nello stesso momento in cui cade l'ultimo giocatore, la mappa è vinta.
- **Carte**: all'inizio della mappa e dopo la prima e la seconda zona il gioco si ferma e ognuno,
  sul suo riquadro, sceglie 1 carta su 3 (su/giù e A, oppure START breve). La carta vale fino alla
  fine della mappa, anche se una zona si rifà; in fondo al riquadro le carte già prese. Elenco
  più sotto.
- **Stelle**: ogni mappa ne dà 3: mappa finita; **esploratore** salvato; mappa finita senza
  ricominciarla da capo. La capsula dell'esploratore è nascosta fuori strada (tra i papiri, tra i
  bambù, dietro le felci della giungla, dietro le colonne cadute dell'arena): quando ci si
  passa vicino lui chiama "AIUTO!". La capsula si apre a colpi o con una bomba (regge tre volte una
  cassa).
- **Giri**: ogni pianeta ha il suo giro, sempre scritto in alto in partita, nei menu e a fine
  mappa. Battuto il boss con almeno **10 stelle su 12** del giro in corso si apre il giro dopo: i
  mostri hanno più vita (+60% a giro), fanno più danni (+25%) e sono un po' più veloci, e le
  stelle del pianeta ripartono da zero (le mappe restano aperte). Con meno di 10 stelle il giro
  resta quello: si rifanno le mappe per le stelle che mancano.
- **Ondate**: i mostri escono dal terreno, mai troppo vicino ai giocatori; più giocatori, più mostri.
- **Vita**: non si ricarica da sola; un po' torna con ogni mostro sconfitto, tanta con i
  **talismani** verdi (si prendono solo se non si ha la vita piena). Qualcuno sta sopra le
  piattaforme o dentro le anfore con il segno verde.
- **Bombe**: si raccolgono passandoci sopra, fino a 3 (pallini sopra la barra). L2 le lascia: dopo
  3 secondi esplodono, fanno tanto danno ai mostri entro circa 3 m e spingono via tutti; i
  compagni non perdono vita. Anche una bomba della mappa colpita da un colpo esplode dopo 3 secondi.
- **Terra**: muri (non si scavalcano), muretti e colonne spezzate (si saltano, i colpi passano),
  blocchi di pietra, piattaforme e cammini di ronda (ci si sale saltando), anfore, vasi e casse che
  si rompono, acqua di oasi, laghetti e canali (non si attraversa nemmeno saltando, i colpi
  passano), **tamburi** che lanciano lontano, **cespugli** che nascondono ai mostri lontani,
  **papiri**, **erba alta**, **bambù** e **felci** dove si nascondono anche i mostri: si vedono solo
  da vicino.
- **Cenote** (a Chichén Itzá): pozzi profondi pieni d'acqua; chi ci cade dentro perde una vita
  (come una caduta: rientra accanto a un compagno o si rifà la zona). Si possono saltare dove sono
  stretti. Anche i mostri spinti dentro da bombe e colpi forti ci cadono; da soli non ci entrano.

### Eroi

| | Vita | Colpo normale (R1) | Speciale (L1) |
|---|---|---|---|
| **Astro**, astronauta della Terra | 100 | fucilone: raffica di 3 colpi che spazza il ventaglio (sinistra, centro, destra), lungo | 3 razzi che inseguono ognuno un mostro |
| **Mochi**, coniglio della Luna | 140 | ventaglio di 5 schegge di luna, largo e corto | salto lunare dove si mira: botta a terra e anello di schegge |
| **Marzio**, marziano di ghiaccio secco | 95 | 3 palle di ghiaccio insieme sul ventaglio: ognuna rallenta | tempesta gelata di 4 m attorno a sé: rallenta molto e fa danno |
| **Zolfo**, fiamma delle nubi di Venere | 120 | fiamma continua che attraversa i mostri, ventaglio stretto e corto | eruzione: 5 bolle di lava che esplodono dove cadono |

La barra arancione è il colpo normale: si ricarica da sola poco dopo l'ultimo colpo, ma se la si
svuota del tutto si spara di nuovo solo quando è piena (diventa rossa). La barra gialla si
riempie con i danni fatti dal colpo normale: piena, compare "L1".

### Carte

Le 3 carte proposte sono a caso tra quelle che ognuno non ha ancora; quando in squadra c'è il
compagno giusto, una volta su due una delle tre è una **combo** (in arancione).

| Carta | Effetto |
|---|---|
| Bombarolo | si portano fino a 4 bombe e se ne ha subito una in più |
| Passo svelto | +15% di velocità |
| Pelle dura | +30 di vita massima, subito piena |
| Ricarica lampo | la barra del colpo si ricarica il 40% più in fretta |
| Super carica | lo speciale si carica il 35% più in fretta |
| Colpi pesanti | +25% di danni del colpo normale |
| Lunga gittata | +25% di gittata del colpo normale |
| Merenda | ogni mostro sconfitto rende il doppio della vita |
| Scudo | in ogni zona uno scudo ferma il primo colpo preso |
| Vita extra | una vita in più per questa mappa, subito |
| Calamita | talismani e bombe si prendono da 3 m |
| Bombe giganti | le proprie bombe: +50% di danni su un'area più larga |
| Speciale generoso | usando lo speciale i compagni vicini (entro 6 m) recuperano 25 di vita; solo in due o più |
| Perforante | i colpi normali attraversano un mostro in più (non per Zolfo: la fiamma attraversa già) |
| Rimbalzo | i colpi normali rimbalzano una volta su muri, massi e alberi (non per Zolfo) |
| Colpi infuocati (combo) | Astro, con Zolfo in squadra: i colpi incendiano (4 di danno al secondo per 3 s) |
| Colpi gelati (combo) | Astro, con Marzio in squadra: i colpi rallentano del 30% per 1,5 s |
| Schegge roventi (combo) | Mochi, con Zolfo in squadra: le schegge incendiano |
| Schegge gelate (combo) | Mochi, con Marzio in squadra: le schegge rallentano |
| Vapore (combo) | Marzio o Zolfo, con l'altro in squadra: un mostro rallentato che prende fuoco (o in fiamme che viene rallentato) scoppia in vapore, 30 di danni a chi gli sta attorno; vale per tutta la squadra, la prende uno solo |

Il boss non brucia e non rallenta.

### Mostri della Terra

- **Scorpione** (dalle Piramidi): si avvicina, si ferma alzando il pungiglione con un "!" sopra e
  carica in linea retta; se finisce contro un ostacolo resta stordito.
- **Scarabei** (dalle Piramidi): piccoli, veloci, a sciami; volano sopra muretti, casse e acqua.
- **Arciere di terracotta** (dalla Grande Muraglia): tiene le distanze, si inginocchia e tende
  l'arco, poi tira frecce lente a parabola, mirando dove si sta andando: a terra un cerchio rosso
  dove cadono; una volta su due tre frecce a ventaglio (si vedono tre frecce sull'arco).
- **Guerriero di terracotta** (dalla Grande Muraglia): lento e resistentissimo; alza l'alabarda e
  colpisce davanti a sé.
- A **Chichén Itzá** tutti insieme, a ondate più grandi.
- **Gladiatore gigante** (boss del Colosseo): insegue chi gli è più vicino camminando (più lento
  degli eroi) e attacca secondo la distanza, sempre annunciato da un segno rosso a terra:
  - **fendente**: da vicino, un ventaglio rosso davanti a sé, poi la spadata che butta via;
  - **carica con lo scudo**: una corsia rossa con le frecce, poi corre lungo la corsia e travolge
    chi ci resta; se finisce contro un muro o un ostacolo resta **stordito** e prende più danni;
  - **giavellotti** da lontano (cerchi rossi dove cadono), da 1 a 3;
  - **spaccaterra**: la spada a terra, la crepa corre verso chi mira.
  A due terzi e a un terzo della vita ruggisce, chiama scorpioni e scarabei dalle botole e diventa
  più rapido.

## Cosa si vede a schermo

La telecamera va da 15 a 30 m sullo schermo della console e da 22 a 30 m con l'HDMI collegato
(0.5.1-0.5.2: da 22 a 30 sempre; 0.5: da 15 a 30): quando il gruppo si allarga si vede una zona
intera, e i giocatori non si possono allontanare più di quanto si vede. L'HDMI si controlla ogni
secondo (`/sys/class/extcon/hdmi/state`): attaccarlo o staccarlo in partita cambia solo la visuale
minima; il registro lo dice (righe "HDMI:" e "schermo:").

In alto a sinistra una tessera per ogni giocatore: la faccia del suo eroe nel suo colore e i
cuori delle vite. Chi è a terra ha la faccia spenta e una barra rossa che si svuota fino al
rientro; chi ha finito le vite ha la faccia grigia con una X. Accanto: pianeta e mappa, giro, zona
e mostri che mancano; nel Colosseo la vita del boss. Sopra ogni eroe: numero del giocatore, vita,
barra del colpo, barra dello speciale, bombe; sopra i mostri colpiti la loro vita. I numeri
bianchi sono i danni del colpo normale, quelli gialli i danni di speciali e bombe, i rossi i
danni presi, i verdi la vita recuperata.

## Sonoro

Effetti per colpi, speciali, esplosioni, salti, raccolte, mostri, capsula, cancelli e menu; sei
musiche: titolo e menu, una per ogni mappa della Terra (Egitto, Cina, i Maya, l'arena di Roma per
il boss) e la vittoria a fine mappa, tutto generato dal gioco mentre gira. I suoni lontani dal
centro dello schermo si sentono più a sinistra o a destra e più piano. In pausa e durante la scelta
delle carte la musica si abbassa. Volumi da 0 a 10 nella pausa, voci **MUSICA** (di partenza 6) e
**EFFETTI** (8): ← → cambiano di uno, A sale e dopo 10 torna a 0.

Le musiche della 0.5 sono più leggere di quelle della 0.4 e stanno sotto gli effetti: chi aveva
abbassato molto la musica la rialzi a 4-5.

## Salvataggi

Il gioco salva da solo nella cartella dei salvataggi di RetroArch: sulla console
`/mnt/mmc/saves_RA/Pupazzi/` con la configurazione di serie (`/mnt/sdcard/saves_RA/...` con la
seconda scheda), dove stanno anche i salvataggi degli altri giochi, fuori da APPS (che un
aggiornamento della MOD svuota); se non si potesse usare, in `pupazzi/out/`. Il percorso esatto è
nella riga "salvataggio:" del registro. Dalla 0.5.3 i file sono due tipi:

- `pupazzi.sav`: le impostazioni di questa console (mira di ogni controller, antialias, volumi,
  lingua);
- `partita1.sav` ... `partita4.sav`: le quattro partite (mappe aperte e giro di ogni pianeta,
  stelle di ogni mappa nel giro in corso, storia già vista, tempo di gioco). Un posto libero è un
  file che non c'è.

Si scrive a fine mappa, dopo la storia, quando si chiude la pausa dopo aver cambiato qualcosa,
quando si cambia lingua e all'uscita, sempre in modo sicuro (prima un file a parte, poi lo scambio:
se la console si spegne a metà resta quello di prima). I file sono di testo e spiegano da soli cosa
contengono; un file in cui non c'è niente di valido, o che la scheda non riesce a leggere, si vede
come ILLEGGIBILE: caricandolo si riparte da zero e il file vecchio resta da parte come
`partitaN.sav.illeggibile` (non si cancella mai: se non lo si riesce a mettere da parte la partita
non si scrive). Per liberare un posto basta eliminarlo dal gioco (o cancellare il file).

La prima volta la partita della 0.5.2 (o della 0.4) diventa la **partita 1**, con tutti i progressi;
in `pupazzi.sav` i dati vecchi restano (una 0.5.2 rimessa sulla console li ritrova) e la riga
`salvataggi=1` dice che sono già stati presi: se poi si eliminano tutte le partite non tornano.
Se la partita 1 non si riesce a scrivere resta `salvataggi=0` e si riprova all'avvio dopo.
Con un salvataggio della 0.4 restano mira, antialias e volumi; la storia nuova si vede la prima
volta e la Terra riparte dalla prima mappa.

## Lingue

La lingua si sceglie dalla prima schermata (voce LINGUA, con il mappamondo) e vale per tutto:
menu, carte, messaggi in partita, nomi delle mappe di serie e dei pianeti; i nomi degli eroi sono
trascritti in cinese, giapponese e hindi. Restano in italiano il registro e gli errori delle mappe
personalizzate (come il loro formato). Nel menu dei comandi di RetroArch i tasti hanno i nomi nella
lingua scelta (in inglese per cinese, giapponese e hindi, che i caratteri di RetroArch di solito non
hanno).

Tutti i testi stanno in `lang/testi.txt`, un blocco per testo con l'italiano e le sei traduzioni:
per correggere una traduzione si cambia lì, poi `python3 tools/make_lang.py` (rigenera
`src/lang_data.h` e controlla che non manchi nulla e che i `%d` siano gli stessi) e, se ci sono
caratteri cinesi, giapponesi o parole hindi nuove, `python3 tools/make_font.py` (vedi **Compilare**).

## Mappe personalizzate

Ogni mappa è un file di testo con una griglia di caratteri: le istruzioni sono in
`maps/LEGGIMI.txt` (e sulla console in `pupazzi/mappe/LEGGIMI.txt`), compresa la riga `stile:`
che sceglie l'aspetto (piramidi, muraglia, chichenitza, colosseo). Per provarne una modificata:

    ./deploy.sh mappa terra1.txt        # la mette sulla console, al posto di quella di serie
    ./deploy.sh togli-mappa terra1      # torna quella di serie

Se la mappa ha un errore il gioco usa quella di serie e il menu delle mappe dice cosa non va.
Dalla 0.5.1 il gioco controlla anche che la mappa sia chiusa e tutta giocabile: bordo di muri,
zone che si toccano solo ai cancelli, ogni casella raggiungibile a piedi, cenote lontani da
partenze e cancelli (le regole sono in `LEGGIMI.txt`). Una copia di `terra2.txt` o `terra3.txt`
fatta con la 0.5 è un'altra mappa (dalla 0.5.1 la 2 è la Muraglia e la 3 Chichén Itzá): va tolta.

## Altre console come controller

Sulla console che fa girare il gioco non c'è nulla da impostare. Sulle altre console, in Remote
RetroPad, oltre all'IP della console principale va scelta la porta:

| Giocatore | Porta |
|---|---|
| 1 | 55400 |
| 2 | 55401 |
| 3 | 55402 |
| 4 | 55403 |

Dalla 0.5 queste porte le apre il gioco e non più RetroArch (`run.sh` tiene spento il suo Remote
RetroPad; sulle altre console non cambia nulla): a ogni fotogramma il gioco legge tutti i comandi
arrivati. RetroArch ne leggeva uno solo e con le levette in movimento restava sempre più indietro:
il personaggio continuava a muoversi da solo. Il registro dice quali porte sono aperte (o perché
una non si apre), da dove arriva il primo comando di ogni giocatore e, a fine sessione, quanti ne
sono arrivati ("comandi dalla rete giocatore 2: ..."). Dopo una pausa di RetroArch i comandi
arrivati nel frattempo si scartano: i controller ripartono da fermi.

Se un controller resta bloccato sullo stesso comando per 30 secondi (per esempio un tasto rimasto
"premuto" per la rete), il suo personaggio si ferma finché non arrivano comandi nuovi.

Prove senza una seconda console: dal PC `python3 tools/remote_send.py 192.168.1.228 2 "lx=1@0-2"`
fa da Remote RetroPad del giocatore 2 (levetta sinistra tutta a destra per 2 secondi); `python3
tools/test_remote.py` prova i comandi dalla rete con il frontend di prova (`PUPAZZI_REMOTO=0` non
apre le porte, `PUPAZZI_REMOTO_PORTA` cambia la prima).

## Registri

    ./deploy.sh risultati

Scarica tutto in `risultati/<data>/` e prepara `DA_MANDARE.txt`, l'unico file che serve in caso di
problemi. Il registro dice anche, la prima volta, se dalle levette di ogni giocatore arriva qualcosa
("levetta destra ricevuta"): se la mira non funziona su un controller, si capisce da lì. A fine
sessione, per ogni levetta destra usata: fin dove arriva su ogni asse, quanto si muove da ferma al
centro, quanto trema tenuta inclinata e la mira scelta (righe "levetta destra giocatore N").
All'avvio scrive se l'antialias c'è e in che modo (righe "GL: antialias ..."), poi ogni cambio
fatto dalla pausa. Dalla 0.4: dove sta il salvataggio (riga "salvataggio:"), le carte proposte e
prese, gli esploratori salvati, le stelle di ogni mappa ("stelle della mappa N: ...") e le mappe che
si aprono; ogni 10 secondi, nella riga "stato", quanto costa il sonoro ("audio": millisecondi per
fotogramma, voci che suonano e suoni scartati perché ce n'erano troppi insieme). Dalla 0.5: le vite
perse e quelle rimaste (anche nella riga "stato"), le zone rifatte, le mappe ricominciate da capo
e perché, il giro che si apre, e i comandi arrivati dalle altre console. Dalla 0.5.1: chi cade
nel cenote ("giocatore N caduto nel cenote") e le mosse del gladiatore.

## Sul PC

    retroarch -L dist/pc/pupazzi_libretro.so

## Compilare

    make            # core per questo PC e strumenti di prova (serve solo g++)
    make test       # prove automatiche (testi delle lingue: make_lang.py e test_lang; mira e carte: test_aim;
                    # salvataggi: test_save; vite e giri: test_rules; mappe chiuse e giocabili: test_maps;
                    # gladiatore: test_boss)
    make test-gl    # prove del renderer GPU su un OpenGL ES software (Mesa)
    make test-audio # prove del sonoro; scrive ogni suono e ogni musica in build/wav/ per ascoltarli
    python3 tools/test_remote.py   # comandi dalle altre console (Remote RetroPad) con il frontend di prova

Il frontend di prova registra anche il sonoro di una partita: `build/native/host
build/native/pupazzi_libretro.so --frames 3600 --wav partita.wav`.

Per la console serve zig come compilatore incrociato (provato con la versione 0.16.0):

    python3 -m venv .zig && .zig/bin/pip install ziglang
    make dist ZIG=".zig/bin/python -m ziglang"     # ARM 32 e 64 bit e PC, pronti in dist/

Nel pacchetto `dist/` è già compilato.

I caratteri dell'interfaccia (`src/font_hud.h` e `src/font_hud_data.h`) e i testi (`src/lang_data.h`)
sono già generati. Per rifarli: `python3 tools/make_lang.py` e `python3 tools/make_font.py`, che
vuole Pillow con raqm, `pip install uharfbuzz freetype-py` e i caratteri Noto Sans CJK del sistema
(pacchetto `fonts-noto-cjk`, oppure la cartella in `PUPAZZI_NOTO_CJK`).

## Com'è fatto

| File | Contenuto |
|---|---|
| `src/game.cpp` | schermate, campagna, giri, zone, ondate, cancelli, vite, stelle, musica, telecamera |
| `src/player.cpp` | eroi: movimento, mira, colpi, speciali, bombe, cadute e rientri, ingresso e uscita |
| `src/combat.cpp` | colpi, danni, esplosioni, bombe, talismani, casse e capsula, esploratore |
| `src/monsters.cpp` | mostri della Terra e Gladiatore gigante |
| `src/cards.cpp` | carte: proposte, scelta, effetti (scudo, vita extra, bruciature, vapore) |
| `src/flow.cpp` | percorsi dei mostri attorno agli ostacoli |
| `src/menu.cpp` | menu, storia, stelle e giri, pausa, finestre per entrare a partita iniziata |
| `src/game_draw.cpp` | animazioni, oggetti da disegnare, HUD con le tessere dei giocatori |
| `src/audio.cpp` | sonoro: sintetizzatore, effetti, musiche, mixer con limitatore |
| `src/save.cpp` | salvataggi: impostazioni in `pupazzi.sav`, quattro partite; lettura tollerante, scrittura atomica |
| `src/lang.cpp`, `lang/testi.txt` | lingue: i testi in 7 lingue (`src/lang_data.h`, generato da `tools/make_lang.py`) |
| `src/hud.cpp`, `font_hud.h` | testi (latino, cinese, giapponese, hindi), disegni (cuori, facce degli eroi, stelle) e forme (pannelli con gli angoli tagliati, aloni, sfumature) sopra l'immagine, uguali nei due renderer; caratteri generati da `tools/make_font.py` in `font_hud_data.h` |
| `src/ui.cpp` | stile dell'interfaccia (Orbita, colori Tramonto): pannelli, voce scelta, titoli, tasti, piede dei menu, margine per i televisori |
| `src/level.cpp`, `maps.cpp` | mappe: lettura del testo, zone, collisioni |
| `src/level_mesh.cpp` | geometria delle mappe: terreno morbido, stili della Terra, paesaggio attorno |
| `maps/` | le mappe di serie (incorporate nel core da `tools/embed_maps.sh`) |
| `src/models.cpp`, `mesh.cpp` | modelli in stile cartone con il contorno scuro |
| `src/bot.cpp` | giocatori automatici, solo per le prove |
| `src/core.cpp` | interfaccia libretro, comandi dalle altre console, pausa, uscita, riquadro delle prestazioni, registro |
| `src/glr.cpp`, `swr.cpp` | renderer OpenGL ES 2 con antialias (console) e software (riserva e prove) |
| `tools/host.cpp` | frontend libretro minimo per le prove |
| `tools/test_aim.cpp` | prove della mira (direzione, soglia, tremolio, scatti, rilascio, ventagli, pausa) e delle carte |
| `tools/test_rules.cpp` | prove di vite, zone e mappe da capo, rientri, giri e stelle, mappe aperte |
| `tools/test_maps.cpp` | prove delle mappe: chiuse, tutte giocabili, niente passaggi tra le zone (anche saltando e spinti), cenote |
| `tools/test_boss.cpp` | prove del gladiatore: inseguimento, fendente, carica, stordito, giavellotti, fasi, niente blocchi |
| `tools/test_save.cpp` | prove dei salvataggi: file rotti, troncati, enormi, chiavi sconosciute, salvataggi della 0.4 e della 0.5.2, partite nuove, copiate, eliminate |
| `tools/test_lang.cpp` | prove delle lingue: ogni testo in ogni lingua si disegna, a capo e puntini (anche cinese, giapponese, hindi) |
| `tools/test_audio.cpp` | prove del sonoro: livelli, picchi, voci, tempi, equilibrio musica/effetti; scrive i WAV |
| `tools/remote_send.py`, `test_remote.py` | finto Remote RetroPad e prove dei comandi dalla rete |
| `console/`, `deploy.sh` | voce di menu, script di avvio, icona; installazione dal PC |

Variabili d'ambiente del core, per le prove: `PUPAZZI_RENDERER` (`gpu`, `software`), `PUPAZZI_RES`
(`320`, `640`, `960`), `PUPAZZI_BOT=n` (i primi n giocatori li guida il computer),
`PUPAZZI_MAPPA=n` (si parte subito dalla mappa n, 1-4), `PUPAZZI_GIOCATORI`, `PUPAZZI_PERSONAGGI`
(per esempio `0123`), `PUPAZZI_FRAMES=n`, `PUPAZZI_PERF=1`, `PUPAZZI_HUD` (`0`, `fisso`),
`PUPAZZI_TRACE` (stato della partita a ogni fotogramma), `PUPAZZI_MAPS` (cartella delle mappe
personalizzate), `PUPAZZI_OUT` (cartella del registro), `PUPAZZI_MIRA` (`0` extra morbida,
`1` morbida, `2` media, `3` rapida, per tutti), `PUPAZZI_AA` (`0` niente antialias; `ext` o `es3`
per forzare un modo; `prova` percorso della Mali con un solo campione, per le prove sul PC),
`PUPAZZI_SALVATAGGI` (cartella di `pupazzi.sav` al posto di quella di RetroArch), `PUPAZZI_REMOTO`
(`0`: non apre le porte delle altre console), `PUPAZZI_REMOTO_PORTA` (prima porta, al posto di 55400),
`PUPAZZI_UI_SEMPLICE=1` (con la GPU, l'interfaccia di riserva senza lo shader nuovo),
`PUPAZZI_LINGUA` (`it`, `en`, `es`, `pt`, `zh`, `ja`, `hi`: la lingua al posto di quella salvata),
`PUPAZZI_HDMI` (`1` o `0`: come con l'HDMI collegato o staccato, al posto di guardarlo).

## Stato delle verifiche

Fatto senza console, anche sui file veri del firmware (immagine della MOD 4.0.3) in emulazione:

- campagna intera giocata da 1, 2 e 4 giocatori automatici: tutte le zone delle 3 mappe, l'albero
  gigante con le sue tre fasi, il secondo giro più difficile; trampolini, casse, talismani, bombe,
  rientri, zone rifatte;
- menu, pausa, uscita dalla partita, ingresso a partita iniziata, "Esci" dal titolo, con comandi
  registrati;
- stessa partita, fotogramma per fotogramma, su x86, ARM a 32 bit e ARM a 64 bit;
- nessun errore di memoria o comportamento indefinito (AddressSanitizer, UBSan), anche con
  centomila mappe sbagliate o casuali date al lettore delle mappe;
- renderer GPU: stessa immagine qualunque stato grafico trovi, quasi identica al software;
- revisione completa del codice da un secondo esame indipendente: 8 difetti trovati e corretti;
- 0.3.1: mira provata con comandi scritti (direzione fedele anche vicino agli assi, soglia
  circolare, tremolio di ±3 gradi ridotto a meno di un grado, scatto di 90 gradi in 3-6
  fotogrammi, rilascio e rimbalzo della levetta senza scatti, ventagli, voce della pausa) e con
  comandi a caso su tutte le mappe sotto AddressSanitizer e UBSan; stessa partita su gcc, clang,
  PC, ARM32 e ARM64 anche con comandi scritti, pausa e cambio di mira; una revisione indipendente
  della nuova mira ha trovato 7 difetti, tutti corretti;
- 0.3.2: le prove della mira anche per extra morbida (scatto di 90 gradi in 9 fotogrammi,
  tremolio di ±3 gradi ridotto a 0,4); antialias provato su Mesa (OpenGL ES 3) e, per il modo
  della Mali, con le stesse chiamate a un solo campione (immagine identica a quella senza);
  stessa immagine qualunque stato grafico trovi, con e senza antialias; anche dentro il driver
  `gl` del RetroArch a 64 bit della console (senza antialias e percorso della Mali a un campione:
  i 4 campioni veri lì non si provano, il Mesa di quella copia del firmware in emulazione si ferma
  al primo triangolo anche in un programma di prova a parte); campagna con 1, 2 e 4 giocatori
  automatici con la telecamera nuova; stessa partita su gcc, clang, PC, ARM32 e ARM64;
  revisione indipendente: 6 difetti piccoli, tutti corretti;
- 0.4: campagna con 1, 2 e 4 giocatori automatici (30.000 fotogrammi: 4–5 mappe, carte scelte a
  ogni cancello, prigionieri liberati, stelle e mappe aperte salvate); stessa partita su gcc,
  clang, PC, ARM32 e ARM64 con carte e prigioniero; prove delle carte e dei salvataggi (file
  rotti, troncati, enormi, di altre versioni, cartelle che mancano); ogni suono e ogni musica
  misurati (nessun campione al fondo scala, limitatore a -1 dBFS, mai più di 24 voci, 0,1 ms per
  fotogramma sul PC); AddressSanitizer e UBSan puliti anche con menu, storia, carte, pausa con i
  volumi, ingresso e uscita dei giocatori; voce di menu e salvataggio nel RetroArch a 32 bit della
  console in emulazione; revisione indipendente: 6 difetti, tutti corretti.
- 0.5: campagna con 1, 2 e 4 giocatori automatici sulle mappe della Terra (30.000 fotogrammi: 3,
  4 e 5 mappe; con 4 il giro 2 si apre a 10 stelle e le stelle ripartono da zero); prove di vite,
  zone e mappe da capo, rientri, giri e salvataggio della 0.4; stessa partita su gcc, clang, PC,
  ARM32 e ARM64; comandi dalla rete con un finto Remote RetroPad (raffiche da 200 messaggi,
  levetta lasciata, pad e rete insieme, porte occupate); AddressSanitizer e UBSan puliti anche
  con vite e mappe da capo forzate; RetroArch a 64 bit della console con il driver `gl`; voce di
  menu, porte dei comandi e salvataggio (anche quello della 0.4) nel RetroArch a 32 bit della
  console in emulazione; revisione indipendente: 4 difetti, tutti corretti.
- 0.5.1: mappe chiuse e tutte giocabili, controllate al caricamento e con 4 giocatori a caso che
  saltano e vengono spinti in ogni zona (nessun passaggio tra le zone, nessuno dentro un ostacolo);
  cenote (cadute, rientri, mostri spinti dentro); gladiatore (inseguimento, fendente, carica,
  stordito, giavellotti, fasi, mai bloccato) battuto da 1, 2 e 4 giocatori automatici; campagna
  nell'ordine nuovo con 1, 2 e 4 giocatori automatici; stessa partita su gcc, clang, PC, ARM32 e
  ARM64; AddressSanitizer e UBSan puliti; RetroArch della console in emulazione, anche con il
  salvataggio della 0.4; revisione indipendente: nessun difetto bloccante, 10 piccoli corretti.
- 0.5.2: tutte le schermate nuove provate dal core vero (titolo, Come si gioca?, storia, mappe anche
  con note d'errore e nomi lunghi delle mappe personalizzate, scelta degli eroi da 1 a 4, pausa e
  riquadro dei comandi anche con chi entra, carte da 1 a 4 giocatori, fine mappa, HUD in partita);
  software e GPU confrontati su 13 schermate (al massimo 55 pixel con più di 24 livelli di
  differenza su 307.200); stessa partita su gcc, clang, PC, ARM32 e ARM64 (anche 21.000 fotogrammi di
  campagna); campagna con 1, 2 e 4 giocatori automatici; AddressSanitizer, UBSan e ThreadSanitizer
  puliti (l'interfaccia ora si disegna a strisce, in parallelo); RetroArch a 64 bit della console con
  il driver `gl`; voce di menu nel RetroArch a 32 bit in emulazione; interfaccia semplificata di
  riserva provata (se il driver della console rifiutasse lo shader nuovo si gioca lo stesso, invece
  dello schermo nero); revisione indipendente: nessun difetto bloccante, 8 tra medi e piccoli
  corretti.

Si può sapere solo sulla console: la nuova interfaccia sullo schermo e sulla TV (testi da 12–16
pixel, margini) e quanto costa lo shader nuovo alla Mali in menu, pausa e carte; come si gioca
davvero, la fluidità con la Mali sulle mappe nuove con la visuale da 22 a 30 m (fino a 46 mila
triangoli inviati, a Chichén Itzá e nel Colosseo con 4 giocatori), quanto dura il gladiatore con
giocatori veri, il sonoro sulla TV e i comandi dalle altre console sul Wi-Fi vero.

## Note tecniche

- La console avvia il RetroArch a 32 bit (`/mnt/vendor/deep/retro/retroarch`): il core di
  riferimento è `arm32`. Quello a 64 bit c'è per confronto (`run.sh gpu64`).
- Con la Mali, RetroArch non sa ricreare il contesto grafico: il core non cambia mai risoluzione.
- Il gioco avanza a passi fissi di 1/60 di secondo, con `-ffp-contract=off`, trigonometria
  propria (`src/dmath.h`) e chiamate al generatore casuale sempre in istruzioni separate: stessa
  partita su ogni processore e con ogni compilatore (base per il gioco in rete e per i replay).
- La geometria fissa della mappa è divisa in blocchi da 8×8 caselle: la GPU disegna solo quelli
  inquadrati; eroi e mostri sono trasformati e illuminati sulla CPU in un solo buffer. Il terreno
  è costruito sugli angoli delle caselle (angoli arrotondati dove cambia il materiale, colori che
  variano piano su qualche metro); le collisioni restano sulle caselle.
- Antialias: sulla Mali con `EXT_multisampled_render_to_texture` (i campioni restano nella
  memoria della GPU e si riducono a un pixel quando il fotogramma esce), altrove con OpenGL ES 3
  (renderbuffer a più campioni e blit).
- Interfaccia (dalla 0.5.2): coordinate in pixel a 640×480; testi in Chakra Petch già alla misura
  giusta, con i bordi sfumati e gli aloni calcolati all'avvio (dalla 0.5.3 anche Noto Sans CJK per
  cinese e giapponese e Poppins per l'hindi, solo con i caratteri dei testi: le parole hindi sono
  già composte con HarfBuzz quando si genera il font, il gioco le cerca intere; in cinese e
  giapponese si va a capo tra un carattere e l'altro); pannelli, aloni e sfumature sono
  forme calcolate pixel per pixel dalla distanza dal bordo, con la stessa matematica nel renderer
  software (a strisce, in parallelo) e nello shader della GPU (un solo disegno per fotogramma, con
  l'interno pieno dei pannelli grandi senza calcoli). `make test-gl` confronta le due.
- `libretro.h` viene da libretro-common (licenza MIT); licenze dei caratteri in
  `third_party/fonts/LEGGIMI.txt`.
