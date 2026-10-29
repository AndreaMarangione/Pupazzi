# Pupazzi — specifica del gioco

Documento di lavoro: lo scrive Claude mentre Andrea e Claude decidono il gioco insieme.
Ogni voce è segnata così:

- **[deciso]** stabilito da Andrea (con la data);
- **[proposta]** proposta di Claude, da confermare;
- **[da decidere]** argomento ancora aperto;
- **[vincolo]** limite tecnico che non dipende da una scelta di gioco.

Ciò che è indicato come *prototipo* è il comportamento della versione 0.2.0, provata sulla
console l'11/10/2026; *0.3*, *0.4* indicano come quella versione realizza una voce (valori da
provare sulla console e da confermare).

Ultimo aggiornamento: 29/10/2026 (lista della 0.5.4: nomi dei salvataggi, antialias migliore, CPU
nel riquadro delle prestazioni, via la storia, banner delle carte, bonus nelle casse, batteria,
cancelli a battente; il gioco in rete si farà molto più avanti).

## 1. Il gioco in breve

- Nome: **Pupazzi** [deciso 13/10; confermato il 17/10 con il tema nuovo].
- [deciso 17/10] **Tema ufficiale: il sistema solare.** Ogni mondo è un pianeta, Luna compresa; i
  personaggi sono creature fantasiose dei pianeti, e dalla Terra viene un astronauta con un
  fucilone; nemici e boss sono di ogni pianeta. Niente storia dei burattini (sostituisce la storia
  del 15/10, qui sotto).
- [deciso 17/10, proposta accettata] Storia in una riga: Astro parte dalla Terra con gli amici di
  Luna, Marte e Venere per il giro dei pianeti; ogni pianeta ha i suoi mostri e un boss; battuto
  il boss, l'eroe di quel pianeta entra in squadra. Via fili e Burattinaio; il prigioniero di ogni
  mappa diventa un esploratore chiuso in una capsula nascosta (la sua stella resta).
- [deciso 29/10] **Niente storia raccontata**: via la schermata della storia; dal menu si va dritti
  alle mappe. Il tema resta quello qui sopra.
- Sparatutto cooperativo in 3D con vista dall'alto, ispirato a Brawl Stars [deciso 11/10].
- Da 1 a 4 giocatori, con due modalità [deciso 13/10]: **Insieme**, tutti nella stessa squadra
  contro i nemici del computer (la campagna), e **Contro**, 2 contro 2 (capitolo 2).
- Si attraversano livelli fatti di zone eliminando tutti i nemici; il primo livello è facile, poi
  sempre più difficili [deciso 11/10].
- Ogni personaggio ha la sua arma e il suo attacco speciale [deciso 11/10] (vedi capitolo 6).
- Personaggi reattivi, niente movimenti molli o goffi [deciso 11/10]; il prototipo è giudicato
  "perfetto" [11/10].
- Solo per uso privato con gli amici, non verrà mai pubblicato [deciso].
- Personaggi, nomi e grafica originali: nessun elemento preso da Brawl Stars [vincolo].
- [deciso 15/10, sostituito il 17/10 dal tema del sistema solare] **Storia**: "Pupazzi contro il Burattinaio". Il Burattinaio ha messo i fili agli
  animali dei quattro mondi e li manovra contro i pupazzi; i pupazzi tagliano i fili e liberano gli
  animali (i mostri non muoiono: scappano via liberi). I pupazzi da sbloccare sono prigionieri del
  Burattinaio e si liberano giocando; i boss sono i guardiani dei mondi finiti sotto i fili; alla
  fine c'e' il Burattinaio nel suo teatro.
- 0.4: la storia si racconta in 8 righe la prima volta che si sceglie Insieme ("C'ERA UNA
  VOLTA..."; A mostra tutto, poi va avanti; si salva che e' stata vista). Sopra ogni mostro una
  croce di legno con 3 fili (vespe: un filo e una croce piu' piccola); quando il mostro resta senza
  vita i fili si tagliano, la croce vola via e l'animale fa festa per mezzo secondo e scappa
  lontano dai pupazzi, poi sparisce in uno sbuffo. L'albero gigante ha una croce grande con 4
  fili; vinto, resta nell'arena libero e i mostri rimasti scappano. Fine della prateria: "il
  Burattinaio e' scappato verso il deserto...". Tutto questo sparisce con la 0.5.

## 2. Multiplayer

- La console che fa girare il gioco va alla TV in HDMI; le altre console fanno da controller via
  Wi-Fi con Remote RetroPad (giocatore N sulla porta 55400+N−1) [deciso]. HDMI provato l'11/10;
  controller via Wi-Fi provati il 15/10 con la 0.4: "funzionava davvero male", comandi che si
  incantano (vedi il vincolo più sotto).
- Si può giocare anche sullo schermo della console [prototipo].
- Si entra in partita premendo un tasto, anche a partita iniziata o in pausa [prototipo].
- [deciso 13/10] **Collegamento**: per ora Remote RetroPad di RetroArch (provato in due, funziona).
  Più avanti un'app nostra per le console-controller: si collega da sola, senza IP e porta, e
  sul suo schermo mostra vita, colpi e speciale del proprio personaggio.
- [deciso 13/10] I pad collegati alla console principale (USB o Bluetooth) contano come giocatori,
  insieme alle console-controller.
- [deciso 13/10] Chi entra a mappa iniziata sceglie il personaggio in una finestra sopra il gioco;
  gli altri continuano a giocare.
- [deciso 13/10] Dalla pausa ognuno può lasciare la partita; il suo posto si libera.
- [deciso 13/10] Controller che si scollega: se i suoi tasti restano bloccati sullo stesso stato (non
  a riposo) per 30 secondi, il personaggio si ferma finché non arrivano comandi nuovi.
- [deciso 13/10] Colpi e bombe non tolgono vita ai compagni.
- [deciso 13/10] A fine mappa: statistiche per giocatore (nemici eliminati, danni, cadute) e il
  migliore della mappa.
- [deciso 13/10] I progressi si salvano sulla console principale: i personaggi sbloccati valgono per
  tutti.
- [deciso 13/10] Per ora solo rete di casa; il gioco via internet si valuterà in futuro.
- [vincolo] Remote RetroPad (dal codice di RetroArch 1.22): il controller manda un messaggio UDP
  per ogni tasto o asse che cambia e solo quando cambia; la console principale ne legge uno solo
  per giocatore a ogni fotogramma. Con le levette in movimento continuo (movimento e mira) i
  messaggi si accumulano e i comandi arrivano in ritardo; un messaggio perso lascia un tasto
  bloccato finché non cambia di nuovo. Prova da fare subito con la 0.2.0 (giocatore 2 mosso con
  la levetta sinistra per mezzo minuto): se il ritardo cresce, l'app per le console-controller
  (tutto lo stato in un solo messaggio a ogni fotogramma) va anticipata alla 0.3.
  - Provato il 15/10: il ritardo cresce, il personaggio continua a muoversi da solo.
  - [proposta, fatta nella 0.5] Il gioco legge lui i messaggi dei controller sulle stesse porte, tutti
    quelli arrivati a ogni fotogramma (RetroArch non le apre più): niente ritardo, e sulle altre
    console non cambia nulla. Resta il limite dei messaggi persi (raro in casa), coperto dai 30
    secondi qui sopra.
- [deciso 29/10] Il gioco in rete con ognuno il suo schermo (qui sotto) si farà **molto più avanti**:
  non è più la 0.5.4 (capitolo 14).
- [deciso 20/10] **Gioco in rete, ognuno con il suo schermo**: oltre alla TV con le console-controller
  (che resta), ogni console fa girare il gioco e lo mostra sul suo schermo, **con la telecamera
  centrata sul proprio eroe**.
  - [proposta] Voce GIOCO IN RETE: una console crea la partita, le altre (fino a 4 in tutto) la
    trovano da sole nella stessa Wi-Fi e si uniscono; tutte con la stessa versione del gioco (lo
    controlla il gioco). Un giocatore per console, menu come ora, ognuno col suo pad; i progressi si
    salvano sulla console che ha creato la partita.
  - [proposta] Tra le console passano solo i tasti: il gioco fa la stessa partita su ogni console
    (verificato a ogni versione su ARM a 32 e 64 bit e sul PC). Comandi con circa 50 ms di ritardo
    per assorbire gli scatti della Wi-Fi; se una console si scollega il suo eroe esce e gli altri
    continuano.
  - [proposta] Con una telecamera per eroe, in rete niente limite di distanza tra i giocatori (i
    cancelli tengono comunque tutti nella stessa zona); ai bordi dello schermo una freccia nel colore
    di ogni compagno fuori vista. Ogni console usa la sua visuale (capitolo 4).
- [deciso 17/10] **Vite in più giocatori**: ognuno ha 3 vite per mappa; chi cade rientra dopo 5
  secondi accanto a un compagno e perde una vita; finite le vite non rientra più. Se cadono tutti
  nello stesso momento (anche con vite rimaste) la mappa ricomincia da capo. Da soli: capitolo 4.
- [deciso 17/10] **Icone dei giocatori** in alto a sinistra: quando uno cade compare la barra rossa,
  che sparisce quando rientra (se rientra).
  - [proposta] Per ognuno la faccia del suo personaggio nel colore del giocatore e i cuori delle
    vite; a terra una barra rossa che si svuota nei 5 secondi del rientro; senza vite la faccia
    diventa grigia con una X. Si vedono anche da soli (le vite).

### Modalità

- **Insieme** [deciso 13/10]: da 1 a 4 giocatori nella stessa squadra contro i nemici del
  computer; è la campagna dei capitoli 4 e 8.
- **Contro** [deciso 13/10]: 2 contro 2.
  - [deciso 13/10] Regole: partita di 2 minuti; chi cade rientra dopo 3 secondi nella base della sua
    squadra; vince la squadra con più eliminazioni; in parità si va ai supplementari e vince la
    prima eliminazione.
  - [deciso 13/10] Arene apposite e simmetriche, una per mondo per cominciare; ogni arena sta tutta
    nello schermo, con la telecamera ferma.
  - [deciso 13/10] Con meno di 4 giocatori i posti vuoti li prende il computer.
  - [deciso 13/10] Le squadre si scelgono prima della partita; anello rosso o blu sotto i personaggi.
  - [deciso 13/10] Personaggi, bombe e talismani sono gli stessi della campagna; le bombe fanno danno
    agli avversari.

## 3. Comandi [deciso 13/10, salto e bomba spostati il 15/10]

| Tasto | Azione |
|---|---|
| levetta sinistra o croce | muoversi |
| R2 | salto (tenuto più a lungo salta più in alto) |
| R1 tenuto | colpo normale |
| L1 | colpo speciale |
| L2 | lasciare una bomba |
| levetta destra | mira |
| START (pressione breve) | pausa |
| SELECT+START tenuti | uscita |
| SELECT (pressione breve) | riquadro delle prestazioni (strumento di prova); dalla 0.5.4 anche il carico della CPU [deciso 29/10] |

- [deciso 15/10] Salto su R2 e bomba su L2 (fino alla 0.3.1 erano B e A).
- Nei menu A conferma e B torna indietro (capitolo 10); in partita A, B, X e Y restano liberi.
- 0.4: nella scelta delle carte su/giu' sposta il cursore, A (o START breve) prende la carta.
- START tenuto due secondi apre il menu di RetroArch [vincolo].

## 4. Regole di base

- **Telecamera**: una sola per tutti, dall'alto e inclinata, segue il gruppo; nessuno può
  allontanarsi oltre quanto la telecamera inquadra [deciso 11/10].
  - [deciso 15/10] Visuale più larga; va bene che tutto sia un po' più piccolo.
  - 0.3.2: telecamera da 15 a 21 m invece che da 11 a 17 (si allontana quando il gruppo si
    allarga): a 15 m si vedono circa 19×19 m di terreno, quasi il doppio di prima; ai bordi della
    zona si ferma mostrando poco oltre.
  - [deciso 17/10] La visuale si allontana ancora, per avere più spazio quando si è lontani, e il
    limite di lontananza tra i personaggi è quanto la visuale.
  - [proposta, fatta nella 0.5] Telecamera da 15 a 30 m: quando il gruppo è sparso si vedono circa
    33×22 m invece di 22×15; il limite tra i personaggi diventa quel riquadro (31×21 m, oggi
    20×14). Una zona intera (capitolo 8) sta nella visuale più larga. Le cose lontane si
    disegnano con meno dettaglio se la console ne ha bisogno.
  - [deciso 18/10, dopo la prova della 0.5] Con i personaggi vicini (anche da soli) la visuale è
    troppo vicina: la distanza minima si allontana. *0.5.1*: da 22 a 30 m.
  - [deciso 20/10] Sullo schermo della console (HDMI staccato) la visuale è troppo lontana: senza
    HDMI vicina, con l'HDMI come ora.
    - [proposta, fatta nella 0.5.3] Il gioco legge da solo se l'HDMI è collegato (`/sys/class/extcon/hdmi/state`, lo
      stesso file del RetroArch della console) e cambia anche a partita in corso. Senza HDMI da 15
      m con il gruppo vicino fino a 30 m con il gruppo largo (il limite tra i personaggi resta
      31×21 m); con l'HDMI da 22 a 30 m.
- **Mira** [deciso 13/10]: si mira con la levetta destra, come in Brawl Stars; niente mira
  automatica (cambia la decisione dell'11/10).
  - [proposta] Levetta destra inclinata: il personaggio si gira e spara in quella direzione; a
    terra una striscia mostra dove va il colpo e fin dove arriva.
  - [proposta] Levetta destra lasciata: si spara dritto davanti, dove guarda il personaggio.
  - [proposta] Colpi a parabola: più si inclina la levetta, più lontano arriva il colpo.
  - 0.3: le tre proposte sono realizzate così; la direzione tiene conto dell'inclinazione della
    telecamera (si mira "come sullo schermo"); quando lo speciale è carico, a terra compare anche
    dove arriva (cerchio giallo).
  - [deciso 14/10] La mira è **a ventaglio per tutti** i personaggi: a terra si vede il ventaglio e
    i colpi del colpo normale coprono quell'area (niente più mira a linea).
  - [deciso 14/10] Levetta destra **più morbida**: zona morta circolare, direzione filtrata contro i
    tremolii, mira che resta ferma quando si lascia la levetta. Ognuno sceglie nella pausa quanto è
    pronta: morbida, media (di partenza) o rapida.
  - 0.3.1: si mira oltre metà inclinazione (morbida 0,55, media 0,50, rapida 0,45); filtro che
    a levetta quasi ferma taglia sotto 0,8 / 1,5 / 3 Hz e si apre con la velocità (uno scatto di
    90 gradi arriva in 6 / 4 / 3 fotogrammi); quando l'inclinazione crolla (levetta lasciata) la
    mira torna a com'era prima del rilascio; per 12 fotogrammi dopo il rilascio servono due
    letture di fila per mirare di nuovo (il rimbalzo della molla non conta). La scelta vale per
    la sessione; con la 0.4 si salva.
  - [deciso 15/10] Dopo la prova: la migliore è **morbida**, che diventa quella di partenza; si
    aggiunge **extra morbida**, quindi quattro livelli da scegliere nella pausa.
  - 0.3.2, extra morbida: si mira da 0,60 di inclinazione, filtro sotto 0,5 Hz a levetta quasi
    ferma che si apre più piano (scatto di 90 gradi in 9 fotogrammi, 150 ms; tremolio di ±3
    gradi ridotto a 0,4).
- [deciso 15/10] **Carte**: all'inizio di ogni mappa e a ogni cancello ogni giocatore sceglie 1
  carta su 3, che vale fino alla fine della mappa (colpi che rimbalzano, una bomba in piu', speciale
  piu' rapido, cura quando un compagno elimina un mostro...). Alcune si combinano tra due pupazzi
  (per esempio neve di Fiocco + fuoco di Brace = vapore). Elenco e valori: proposte di Claude, da
  provare.
  - 0.4: tre scelte per mappa (all'inizio, dopo la zona 1 e dopo la zona 2); il gioco resta fermo
    finche' tutti hanno scelto (dopo un minuto chi non ha scelto prende quella col cursore). Le 3
    carte sono a caso tra quelle che il giocatore non ha; se una combo e' possibile (c'e' il
    compagno giusto), meta' delle volte una delle tre e' una combo. Le carte valgono fino alla fine
    della mappa, anche nelle zone rifatte; si perdono lasciando la partita. Carte: Bombarolo (fino
    a 4 bombe, una subito), Passo svelto (+15% velocita'), Pelle dura (+30 vita massima), Ricarica
    lampo (barra del colpo +40% piu' rapida), Super carica (speciale +35% piu' rapido), Colpi
    pesanti (+25% danni del colpo normale), Lunga gittata (+25% gittata), Merenda (doppia vita da
    ogni mostro liberato), Scudo (in ogni zona ferma il primo colpo), Seconda vita (una volta per
    mappa ci si rialza subito con meta' vita), Calamita (talismani e bombe da 3 m), Bombe giganti
    (+50% danni, area piu' larga), Speciale generoso (lo speciale da' 25 vita ai compagni entro
    6 m; solo in due o piu'), Perforante (i colpi attraversano un mostro in piu'), Rimbalzo (un
    rimbalzo su siepi, massi e alberi); le ultime due non per Brace. Combo: Semi di fuoco e Semi
    di ghiaccio (Paglia con Brace o con Fiocco), Spine roventi e Spine di ghiaccio (Spina con
    Brace o con Fiocco), Vapore (Fiocco con Brace o viceversa, una sola per squadra). Fuoco: 4
    danni al secondo per 3 s; ghiaccio: -30% di velocita' per 1,5 s; vapore: il mostro rallentato
    che prende fuoco (o in fiamme che viene rallentato) scoppia, 30 danni nel raggio di 1,6 m, al
    massimo ogni 1,5 s. Il boss non brucia e non rallenta.
  - [proposta, fatta nella 0.5] Con gli eroi nuovi (capitolo 6) le combo cambiano nome: Colpi infuocati e
    Colpi gelati (Astro con Zolfo o con Marzio), Schegge roventi e Schegge gelate (Mochi con Zolfo
    o con Marzio), Vapore (Marzio con Zolfo); Seconda vita diventa Vita extra (una vita in più,
    capitolo 4); Merenda vale per ogni mostro sconfitto. Valori invariati.
- [deciso 15/10] Zone non tutte uguali: in ogni mappa una zona con un compito diverso (abbattere i
  pali da cui partono i fili, scortare un carretto, resistere 60 secondi) e un mini-boss alla fine
  di ogni mappa, non solo nell'arena (0.4.1).
- **Attacco normale (R1)**: colpi dritti finché la barra non si svuota; la barra si ricarica da
  sola dopo qualche istante; se si svuota del tutto non si spara finché non è di nuovo piena
  [deciso 11/10]. I valori dipendono dal personaggio.
- **Attacco speciale (L1)**: si sblocca dopo un certo numero di colpi a segno [deciso 11/10].
  Forma e numero di colpi dipendono dal personaggio.
- **Vita**: non si rigenera da sola; se ne recupera un po' con ogni nemico eliminato e con i
  talismani sparsi nella mappa; il talismano si prende solo se non si ha la vita piena
  [deciso 11/10].
- **Bombe** [deciso 13/10]: si trovano sparse nella mappa come i talismani; si raccolgono
  passandoci sopra e se ne portano fino a 3; si lasciano con L2 (con A fino alla 0.3.1); una
  bomba esplode 3 secondi dopo essere stata lasciata o colpita da qualcuno; l'esplosione fa
  molto danno ai nemici entro circa 3 m e spinge via tutti, ma i compagni non perdono vita.
  Tornano al loro posto come i talismani (zona che ricomincia, nuovo livello).
- **Caduta**: chi resta senza vita rientra dopo 5 secondi accanto a un compagno, con poca vita e
  un attimo di invulnerabilità; se cadono tutti, la zona ricomincia [deciso 11/10; con le vite dal
  17/10, qui sotto].
- [deciso 17/10] **Vite**: 3 per mappa.
  - Da soli: ogni volta che si cade si riparte dalla zona in cui si è (la zona ricomincia); finite
    le vite si riparte dalla mappa da capo.
  - In più giocatori: chi cade rientra accanto a un compagno e perde una vita; finite le vite non
    rientra più; se cadono tutti nello stesso momento si riparte dalla mappa da capo, anche con
    vite rimaste [deciso 17/10].
  - [proposta, fatta nella 0.5] La mappa da capo è come appena scelta: 3 vite a testa, carte da
    scegliere di nuovo, capsula di nuovo chiusa. La carta Seconda vita diventa "Vita extra": una
    vita in più. *0.5*: chi lascia la partita e rientra nella stessa mappa ritrova vite e
    statistiche (Vita extra già presa non torna tra le carte); se il boss va giù nello stesso
    momento in cui cade l'ultimo giocatore, la mappa è vinta.
  - Sostituisce il "niente vite da soli" del 13/10.
- A ogni nuovo livello e quando una zona ricomincia, tutti hanno la vita piena [deciso 11/10].
- Più giocatori ci sono, più nemici arrivano [deciso 11/10].
- [deciso 13/10, sostituito il 17/10 dalle vite] Da soli non ci sono vite: se si cade si rifà la zona, senza limiti, come nel
  prototipo.
- 0.3: le bombe sono realizzate come deciso (danno 110 al centro, metà al bordo, raggio 3 m); una
  bomba che esplode accende quelle vicine. Chi resta dietro quando il gruppo entra nella zona
  successiva viene portato dentro e il cancello si richiude.

Valori del prototipo (personaggio provvisorio): vita 100; velocità 5 m/s; salto di circa 1,15 m;
un colpo ogni 0,15 s, danno 12, gittata 8,5 m; barra da 10 colpi che riparte 0,5 s dopo
l'ultimo colpo e si riempie in 1,2 s; speciale dopo 8 colpi a segno: ventaglio di 7 colpi
perforanti da 30 che spingono indietro; +8 vita per nemico eliminato, +35 dal talismano;
rientro dopo 5 s con il 60% della vita.

## 5. Grafica [deciso 13/10]

Direzione: cartone animato in 3D come Brawl Stars, con personaggi e mondo originali, pensato per
restare leggibile a 640×480.

- **Personaggi**: pupazzi tozzi con la testa grande e forme arrotondate (non più cubi), colori
  pieni e saturi, contorno scuro; ognuno riconoscibile dalla sagoma e dal colore.
- **Luce**: da cartone animato, 2–3 toni netti al posto della luce piatta di ora; ombra a disco
  sotto i personaggi.
- **Mappe**: pavimento a piastrelle con texture semplici (erba, sabbia, pietra…); muri, casse,
  cespugli e altri elementi nello stesso stile.
- **Animazioni** generate dal codice: camminata, salto, sparo, colpito, a terra, vittoria, con
  schiacciamento e allungamento (squash & stretch) per dare vita ai movimenti.
- **Effetti**: lampi di sparo, scintille sui colpi, particelle, numeri dei danni, scossone dello
  schermo nei colpi forti.
- **HUD**: barre sopra i personaggi come nel prototipo, con una grafica più curata.
- [deciso 15/10] Immagine meno pixellata ("proviamoci").
- 0.3.2: antialias 4× sulla GPU (bordi lisciati), che si spegne dalla pausa se la console
  rallenta; testi disegnati a 640×480 con caratteri proporzionali dai bordi sfumati (DejaVu Sans
  Bold per i testi, Poppins Bold per i titoli) al posto del carattere 8×8 ingrandito; barre sopra
  i personaggi più sottili e in proporzione alla visuale larga.
- Provato il 15–16/10 (0.4): antialias sempre acceso a 60 fps fissi sulla Mali.
- [deciso 29/10] **Antialias migliore** ("si deve vedere meglio"): 8 campioni invece di 4 dove la GPU
  lo permette.
  - [proposta] In pausa ANTIALIAS: NO, 4X, 8X (di partenza il migliore che la GPU regge); se con 8
    campioni il gioco non sta nei 60 fotogrammi al secondo torna da solo a 4 e lo scrive nel
    registro.
- [deciso 19/10] **Interfaccia nuova**, il meno retrò possibile: grafica di oggi, semplicissima ma
  davvero figa, per tutte le schermate di ora e per le prossime. Tra 4 mockup fatti sulle immagini
  vere del gioco (Caramella, Vetro, Adesivo, Orbita) Andrea ha scelto **Orbita**; tra 4 varianti di
  colore (Tramonto, Sole, Luna, Menta) **Tramonto**:
  - pannelli prugna scuri quasi opachi con due angoli tagliati (in alto a sinistra e in basso a
    destra) e il bordo che sfuma dall'ambra (#FFB547) al rosa (#FF4F8B);
  - voce scelta con la stessa sfumatura, il testo scuro e un alone caldo; titoli bianchi con
    l'alone, l'ombra e i trattini con i puntini ai lati;
  - ciano (#6FE0FF) per le etichette (BOSS, IN ARRIVO) e le voci d'uscita; tacche inclinate per
    volumi e stelle del giro;
  - caratteri Chakra Petch (SemiBold e Bold) al posto di DejaVu e Poppins, con gli accenti veri;
  - margine per i televisori, che tagliano i bordi: 32×24 pixel (a 640×480) nei menu, 24×16 in
    partita.
  - *0.5.2*: tutte le schermate e l'HUD in partita nel nuovo stile; pannelli, aloni e sfumature
    calcolati per pixel, uguali nel renderer software e in quello della GPU.
- [deciso 17/10] **Mappe meno pixellate.**
  - [proposta, fatta nella 0.5] Via la scacchiera e i blocchi da 1 m: terreno con sfumature larghe e
    passaggi morbidi tra un materiale e l'altro (sabbia, prato, pietra, sentiero), bordi di acqua,
    sentieri e muri smussati e curvi, oggetti ruotati e messi fuori dalla griglia. Le mappe restano
    file di testo a caselle da 1 m: la forma morbida la calcola il gioco. Ogni mappa ha il suo
    stile (colori, muri, oggetti, paesaggio attorno).

Limiti:

- non si arriva al dettaglio di Brawl Stars (texture dipinte a mano, animazioni fatte da
  animatori): modelli, texture e animazioni li produce Claude con il codice;
- la console disegna a 640×480 anche sulla TV: contano forme grandi e leggibili, il dettaglio
  fine non si vedrebbe.

Margine: nel prototipo il gioco usa il 3–5% della CPU e circa 2.700 triangoli visibili; nella
prova tecnica la console ne ha disegnati 20.000 a 60 fps. La 0.3.0 sulla console: 10–25 mila
triangoli a 60 fps fissi; con la visuale larga della 0.3.2 se ne inquadrano circa il 40% in più.

## 6. Personaggi

- Tanti personaggi [deciso 13/10].
- Ognuno ha un colpo normale diverso da tutti gli altri e un colpo speciale suo, anche questo
  diverso da tutti gli altri [deciso 13/10].
- [deciso 17/10] **Gli eroi del sistema solare** (proposta di Claude, "vanno benissimo"): uno per
  pianeta, 9 in tutto; 4 subito (★), gli altri si sbloccano finendo il loro pianeta. I quattro
  iniziali tengono le armi dei personaggi della 0.4 (tabella più sotto), con l'aspetto nuovo.
  Sostituiscono i pupazzi e i 12 personaggi qui sotto.

  | Eroe | Pianeta | Chi è | Ruolo | Colpo normale (R1) | Speciale (L1) |
  |---|---|---|---|---|---|
  | Astro ★ | Terra | astronauta | equilibrato | fucilone: raffica di 3 colpi sul ventaglio, lungo (come Paglia) | 3 razzi a ricerca (come i corvi) |
  | Mochi ★ | Luna | coniglio lunare | resistente | 5 schegge di luna a ventaglio, corto (come Spina) | salto lunare: botta a terra e anello di schegge |
  | Marzio ★ | Marte | marziano del polo, di ghiaccio secco | controllo | 3 palle di ghiaccio che rallentano (come Fiocco) | tempesta di sabbia gelata attorno a sé |
  | Zolfo ★ | Venere | fiamma delle nubi di zolfo | lanciafiamme | fiamma che attraversa (come Brace) | eruzione: 5 bolle di lava |
  | Lesto | Mercurio | goccia d'argento vivo | veloce | gocce rapide | scatto che attraversa i mostri |
  | Nembo | Giove | nuvola di tempesta | danni ad area | fulmine che salta su 3 mostri | pioggia di fulmini in un cerchio |
  | Anello | Saturno | folletto degli anelli di ghiaccio | lanciatore | anello boomerang che va e torna | anelli che gli girano attorno |
  | Rotolo | Urano | palla di ghiaccio che rotola | mischia | schegge che si frantumano | valanga: rotola travolgendo |
  | Marea | Nettuno | creatura marina col tridente | curatore | bolle: danno ai mostri, vita ai compagni | onda che spinge via i mostri e cura |

- [deciso 13/10, sostituito il 17/10] Quanti: 12 per cominciare, altri aggiunti più avanti.
- [deciso 13/10, sostituito il 17/10] Chi sono: pupazzi abitanti dei 4 mondi, 3 per mondo (per esempio spaventapasseri,
  cactus, pupazzo di neve, golem di lava).
- [deciso 13/10] Come si ottengono: 4 disponibili subito, uno per mondo; gli altri si sbloccano
  finendo i mondi (dal 17/10: uno per pianeta, finendo il suo pianeta).
- [deciso 13/10] Ruoli: ognuno ha un ruolo utile alla squadra (resistente, cecchino, lanciatore con
  colpi che superano i muri, curatore, veloce, danni ad area…).
- [deciso 13/10] Oltre ai colpi cambiano vita e velocità; il salto è uguale per tutti (le mappe sono
  pensate su quello).
- [deciso 13/10] Nomi: li propone Claude, li approva Andrea.
- [deciso 13/10] Scelta prima di ogni mappa; due giocatori possono prendere lo stesso personaggio (si
  distinguono dal colore del giocatore).
- [deciso 13/10] Poteri extra oltre ai due colpi: per ora nessuno.
- [proposta del 13/10, sostituita il 17/10 dagli eroi qui sopra] I 12 personaggi (★ = disponibile subito; gli altri due di ogni mondo si sbloccano
  finendo quel mondo):

  | Nome | Mondo | Chi è | Ruolo | Colpo normale | Speciale |
  |---|---|---|---|---|---|
  | Paglia ★ | Prateria | spaventapasseri | equilibrato | raffica di 3 semi a medio raggio | 3 corvi che inseguono i nemici |
  | Fungo | Prateria | fungo | curatore | spore a corto raggio: danno ai nemici, un po' di cura ai compagni toccati | pioggia di spore che cura molto i compagni vicini |
  | Girasole | Prateria | girasole | cecchino | un seme lungo e potente, lento a ricaricare | raggio di sole che attraversa tutto lo schermo |
  | Spina ★ | Deserto | cactus | resistente | ventaglio di 5 spine a corto raggio | salto con esplosione di spine tutt'intorno |
  | Duna | Deserto | pupazzo di sabbia | lanciatore | palle di sabbia a parabola che passano sopra i muri ed esplodono | tempesta di sabbia che rallenta e danneggia in un'area |
  | Pepita | Deserto | pepita d'oro | veloce | monetine rapide a medio raggio | scatto dorato in avanti che attraversa e colpisce i nemici |
  | Fiocco ★ | Neve | pupazzo di neve | controllo | palle di neve che rallentano | bufera intorno a sé che rallenta molto e fa danno |
  | Ghiacciolo | Neve | ghiacciolo | danni ad area | cubetti che si frantumano in schegge quando colpiscono | lastra di ghiaccio che blocca i nemici per un attimo |
  | Muffola | Neve | guanto di lana | mischia | pugni potenti da vicino | valanga: rotola in avanti travolgendo i nemici |
  | Brace ★ | Vulcano | golem di lava | lanciafiamme | fiamma continua a corto raggio che attraversa i nemici | eruzione: 5 bolle di lava lanciate in avanti |
  | Fumetto | Vulcano | nuvola di vapore | elusivo | sbuffi di vapore che attraversano i nemici | nebbia: invisibile e intoccabile per 2 s, lascia una nube che fa danno |
  | Pomice | Vulcano | pietra pomice | supporto | sassi che rimbalzano sui muri | scudo di roccia per i compagni vicini |

- Dalla 0.3.1, i 4 personaggi iniziali (tutti con la mira a ventaglio):

  | | Vita | Velocità | Colpo normale | Barra | Speciale (si carica con i danni del colpo normale) |
  |---|---|---|---|---|---|
  | Paglia | 100 | 5,0 | raffica di 3 semi da 12 che spazza un ventaglio di 25° (sinistra, centro, destra), gittata 8 m | 5 raffiche | 3 corvi da 40 che inseguono (160 di danni) |
  | Spina | 140 | 4,7 | 5 spine da 9 su un ventaglio di 40°, gittata 5,2 m | 4 | salto fino a 6,5 m: 60 ad area e 12 spine (150) |
  | Fiocco | 95 | 5,0 | 3 palle di neve da 6 insieme su un ventaglio di 20°, ognuna rallenta del 40%, gittata 8,5 m | 8 | bufera di 4 m per 4 s: 15 al secondo, rallenta del 70% (140) |
  | Brace | 120 | 4,8 | fiamma continua su un ventaglio di 18° che attraversa, 4,5 a sbuffo, gittata 4,2 m | carburante 1,2 s | 5 bolle di lava da 50, raggio 1,6 m (180) |

  Nella 0.3.0 Paglia e Fiocco miravano a linea (semi da 10 tutti nella stessa direzione, una
  palla di neve da 14).

## 7. Nemici

- Sono mostri, diversi per ogni mondo, ciascuno tipico del mondo a cui appartiene [deciso 13/10].
  ("Robot" nelle parole di Andrea vuol dire nemici guidati dal computer.)
- [deciso 13/10] Ogni mondo ha 4 mostri e un boss, che si affronta nell'arena finale del mondo.
- [deciso 13/10] Comportamenti di base, vestiti in modo diverso in ogni mondo:
  - corridore: carica e colpisce da vicino;
  - tiratore: tiene le distanze e spara colpi lenti che si possono schivare;
  - sciame: piccoli, veloci, tanti, con poca vita;
  - grosso: lento, tanta vita, colpisce forte;
  - lanciatore: tira colpi a parabola che passano sopra i muri.
- [deciso 13/10] Ogni mondo aggiunge almeno un comportamento o un trucco nuovo legato al suo
  elemento (per esempio lo scorpione si nasconde sotto la sabbia, il lupo scivola sul ghiaccio, i
  mostri del vulcano non si bruciano con la lava).
- [deciso 17/10] Nemici e boss sono di ogni pianeta (proposta di Claude, "vanno benissimo"):

  | Pianeta | Mappe e ambiente | Mostri | Boss |
  |---|---|---|---|
  | Terra | meraviglie: Le Piramidi, Machu Picchu, La Grande Muraglia; arena il Colosseo [deciso 17/10] | sciacallo (corridore), lama sputatore (tiratore), scarabei (sciame), guerriero di terracotta (grosso) | Gladiatore gigante |
  | Luna | crateri, gravità bassa: salti più alti e lunghi | da decidere | verme dei crateri |
  | Marte | canyon e polo ghiacciato, tempeste di sabbia | da decidere | Fobos e Deimos, le due lune diventate mostri |
  | Venere | vulcani e laghi d'acido che bruciano | da decidere | drago di lava |
  | Mercurio | il lato al sole scotta, quello in ombra salva | da decidere | il Messaggero alato |
  | Giove | isole tra le tempeste, vento che spinge | da decidere | la Grande Macchia Rossa, un uragano vivo |
  | Saturno | blocchi di ghiaccio degli anelli che si muovono | da decidere | il Guardiano degli Anelli |
  | Urano | ghiaccio che fa scivolare | da decidere | lo yeti di metano |
  | Nettuno | correnti e venti che trascinano | da decidere | il Kraken |

  I mostri degli altri pianeti si decidono quando ci si arriva, come per la Terra.
- [deciso 18/10] Terra: lo sciacallo e il lama che sputa non convincono. Al posto loro lo
  **scorpione** (corridore: alza il pungiglione e carica) e l'**arciere di terracotta** (tiratore:
  frecce lente a parabola con il cerchio rosso dove cadono, insieme ai guerrieri di terracotta).
- [deciso 18/10] Il Gladiatore gigante "bellissimo", ma si deve muovere e combattere: insegue i
  giocatori, con attacchi più sensati. Proposta di Claude, nella 0.5.1: cammina verso chi gioca
  (più lento degli eroi); fendente a ventaglio davanti a sé; carica con lo scudo in linea retta
  (contro un muro resta stordito e prende più danni); giavellotti da lontano; spaccaterra; ai
  due terzi e a un terzo della vita ruggisce, chiama rinforzi e diventa più rapido. Ogni attacco
  è annunciato da un segno rosso a terra.
- Terra [deciso 17/10, proposta accettata]:
  - Le Piramidi: dune, sfinge, obelischi, oasi sul Nilo; sciacalli (si fermano, ringhiano e
    caricano) e scarabei (sciami che volano).
  - Machu Picchu: terrazze, case di pietra, nebbia; arrivano i lama sputatori (sputano palle lente).
  - La Grande Muraglia: colline, la muraglia da percorrere, torri; arrivano i guerrieri di
    terracotta (grossi, lenti, colpo di alabarda davanti a sé).
  - Il Colosseo: arena di sabbia con le botole da cui escono i mostri. Il Gladiatore gigante lancia
    giavellotti (cerchio rosso dove cadono), spacca il terreno in linea con la spada, chiama
    sciacalli e scarabei; più rapido a 2/3 e a 1/3 della vita.
  - Elemento speciale della Terra: papiri ed erba alta che nascondono (come l'erba alta della prateria).
- [deciso 13/10, sostituito il 17/10 dalla tabella qui sopra] Mostri e boss:

  | Mondo | Mostri | Boss |
  |---|---|---|
  | Prateria | cinghiale (corridore), rospo sputatore (tiratore), vespe (sciame), orso (grosso) | albero gigante |
  | Deserto | scorpione (corridore), serpente sputaveleno (tiratore), scarabei (sciame), mummia (grosso) | verme delle sabbie gigante |
  | Neve | lupo (corridore), pinguino lanciapalle (lanciatore), pipistrelli del gelo (sciame), orso polare (grosso) | yeti gigante |
  | Vulcano | masso rovente che rotola (corridore), diavoletto di magma (tiratore), salamandre (sciame), tartaruga vulcano (lanciatore) | drago di lava |

- [proposta] I boss:
  - Prateria, albero gigante: lancia ghiande che esplodono a parabola, fa spuntare radici in fila
    (segnalate a terra prima), chiama vespe e cinghiali; più rapido a 2/3 e a 1/3 della vita.
  - Deserto, verme delle sabbie: si immerge e riemerge sotto i giocatori (prima compare l'ombra),
    sputa palle di sabbia, colpisce con la coda.
  - Neve, yeti gigante: salta e crea onde d'urto ad anello che si schivano saltando, lancia palle
    di neve giganti che rotolano, chiama i lupi.
  - Vulcano, drago di lava: soffio di fuoco a ventaglio, vola sopra l'arena lasciando pozze di
    lava, palle di fuoco; nell'ultima fase la lava sale dai bordi.
- Prototipo: corridore e tiratore; i corridori più veloci superano il giocatore dal livello 9 e
  la media dal livello 11.
- 0.3, prateria: cinghiale (vita 70; si ferma, raspa con un "!" e carica in linea retta; contro un
  ostacolo resta stordito), rospo (50; saltella a distanza, gonfia il gozzo e sputa colpi lenti),
  vespe (18; volano sopra staccionate, casse e acqua, pungono), orso (280; si alza e dà una zampata
  davanti a sé). Albero gigante come proposto, con 2600 di vita da soli e di più in compagnia.
  Trucco della prateria: i mostri nell'erba alta si vedono solo da vicino o quando attaccano.
  Al giro successivo della campagna i mostri hanno il 60% di vita e il 25% di danno in più.

## 8. Mappe e livelli

- Più di 10 mappe [deciso 13/10].
- [deciso 17/10] **I mondi sono i pianeti**: Terra → Luna → Marte → Venere → Mercurio → Giove →
  Saturno → Urano → Nettuno (proposta accettata), ognuno con 3 mappe e l'arena del boss, quindi 36
  mappe. Sostituisce i 4 mondi del 13/10.
- **Struttura** [deciso 13/10]: campagna di 4 mondi; ogni mondo ha 3 mappe e un'arena finale con un
  boss, quindi 16 mappe. Ogni mappa è un livello di 3 zone in fila, collegate da cancelli (con
  una sola telecamera niente bivi). Finita la campagna si ricomincia dal primo mondo, più
  difficile. (Dal 17/10: 9 pianeti, e i giri sono di ogni pianeta, qui sotto.)
- [deciso 29/10] **Cancelli a battente** che si chiudono bene, come quelli veri.
  - [proposta] Due ante incernierate ai pilastri: a zona libera si aprono ruotando verso la zona dopo,
    quando tutti sono dentro si richiudono dietro di loro fino a combaciare al centro, con un colpo e
    un po' di polvere. Chi è ancora davanti a un'anta viene spinto via, nessuno resta incastrato.
    Ante nello stile di ogni mappa.
- [deciso 18/10] **La mappa disegnata è tutta visitabile e giocabile**: niente parti irraggiungibili
  dentro la mappa (nella 0.5, alle Piramidi, saltando nel Nilo si camminava nella parte non usata e
  si passava nelle altre zone); l'acqua non si attraversa nemmeno saltando; il paesaggio attorno
  si riduce a quello che la visuale può vedere; controlli automatici perché nelle mappe non ci
  siano bug del genere.
- [deciso 18/10] **Chichén Itzá al posto di Machu Picchu** (scelta di Andrea tra Taj Mahal, Chichén
  Itzá e Petra). Ordine della Terra, proposta di Claude: Le Piramidi (scorpioni e scarabei), La
  Grande Muraglia (arrivano arcieri e guerrieri di terracotta), Chichén Itzá (tutti i mostri
  insieme), il Colosseo. A Chichén Itzá i **cenote**: ci si cade dentro e si perde una vita [deciso
  18/10]; anche i mostri spinti dentro (bombe, colpi forti) cadono.
- [deciso 17/10] **Mappe più grandi**: due terzi più grandi di quelle della 0.4.
  - [proposta] Due terzi di superficie in più: zone di circa 31×21 m invece di 24×16, così una zona
    intera sta nella visuale più larga (capitolo 4).
- [deciso 17/10] **Giri**: per aprire il giro successivo di un pianeta bisogna avere almeno 10
  stelle; alla fine di ogni giro le stelle ripartono da zero; si vede sempre in che giro si è.
  - [proposta, fatta nella 0.5] Il giro è di ogni pianeta (scritto nel menu delle mappe, in partita e a fine
    mappa). Battuto il boss con almeno 10 stelle su 12 del giro in corso si apre il giro dopo, più
    difficile, con le stelle da capo. Il pianeta successivo si apre battendo il boss al primo giro.
  - [proposta, fatta nella 0.5] La terza stella diventa "senza ricominciare la mappa" (con le vite: da soli si può
    cadere ma non finire le vite, in più giocatori non si deve mai cadere tutti insieme).
- **Ambientazione** [deciso 13/10, sostituita il 17/10 dai pianeti]: 4 mondi all'aperto, classici da cartone animato:
  1. Prateria — elemento speciale: erba alta dove si nascondono anche i nemici;
  2. Deserto — sabbie mobili che rallentano;
  3. Neve — ghiaccio che fa scivolare;
  4. Vulcano — lava che brucia.
  Ostacoli a tema: rocce, tronchi, staccionate, cactus, pupazzi di neve, colonne di basalto…
  (Scartata il 13/10 l'ambientazione nei posti di casa visti in piccolo.)
- **Difficoltà** [deciso 13/10]: cresce di mappa in mappa con nemici ed elementi nuovi introdotti poco
  alla volta, non solo con i numeri. Prototipo: stessa mappa sempre, crescono solo numero, vita,
  danni e velocità dei nemici; da soli il muro arriva intorno al livello 7.
- **Elementi comuni** [deciso 13/10]:
  - muro: blocca il passo e i colpi;
  - muretto basso: blocca il passo ma non i colpi, si scavalca saltando;
  - cassa: ci si sale saltando; alcune si rompono a colpi e possono contenere un talismano (dalla
    0.5.4 le altre un bonus, capitolo 9);
  - cespuglio: chi ci sta dentro è nascosto ai nemici lontani;
  - acqua o buco: non si attraversa, i colpi passano;
  - trampolino: salto lungo;
  - piattaforma rialzata: ci si sale saltando.
- **Un elemento speciale per mondo** [deciso 13/10]: quale, dipende dall'ambientazione.
- **Misure** [deciso 13/10]: zone come quelle del prototipo (da circa 20×14 a 24×16 m), adatte alla
  telecamera unica. (Dal 17/10 due terzi più grandi, qui sopra.)
- **Come si disegnano** [deciso 13/10]: ogni mappa è un file di testo con una griglia di caratteri,
  un carattere per casella; le prepara Claude e le può disegnare anche Andrea.
- [deciso 13/10] Mondi e mappe si sbloccano in ordine; si può ripartire da qualunque mappa già
  raggiunta.
- [deciso 13/10] Nessuna scelta di difficoltà all'inizio: la difficoltà la fanno le mappe e i giri
  successivi della campagna.
- [deciso 15/10] **3 stelle per mappa**: mappa finita; prigioniero liberato (in ogni mappa c'e' un
  prigioniero in gabbia nascosto fuori strada: dietro una siepe, oltre un trampolino...); senza
  cadere tutti (nessuna zona rifatta). Le stelle sbloccano cappelli e colori per i pupazzi e la
  difficolta' Incubo (0.4.1). (Dal 17/10 le stelle aprono i giri, e prigioniero e gabbia diventano
  esploratore e capsula; cappelli e Incubo non ci sono più.)
- Provato il 15–16/10 (0.4): tutti e 4 i prigionieri trovati (quello dello Stagno solo al terzo
  giro), 12 stelle su 12 la prima sera.
  - 0.4: la gabbia (carattere `p` nelle mappe) e' una cassa che regge 150 danni (tre casse) e si
    rompe anche con le bombe; dentro un pupazzetto che saltella e, con un pupazzo entro 10 m, grida
    "AIUTO!" ogni 3 secondi circa. Liberato dice "GRAZIE!", fa festa e corre verso la partenza.
    Dove: "Il prato" nell'angolo in alto a destra della zona 2, dietro l'erba alta; "Lo stagno"
    nella zona 3, in un angolo chiuso da una staccionata (si passa saltando sulla balla di
    fieno); "Il bosco" nella zona 1, tra gli alberi in fondo a sinistra; l'arena tra i massi in
    fondo. Liberato anche dopo l'ultimo mostro (durante la festa di fine mappa), la stella conta
    lo stesso. A fine mappa le stelle compaiono una alla volta, con "NUOVA!" per quelle mai prese;
    restano salvate (si sommano: rifacendo la mappa si prendono quelle che mancano). Nel menu
    delle mappe le stelle di ogni mappa e il totale (su 12).
  - 0.4: le mappe si aprono in ordine: all'inizio solo "Il prato"; finire una mappa apre la
    successiva; le altre dicono "chiusa". Il giro successivo (piu' difficile) non si salva: si
    gioca di seguito dopo l'arena.
- 0.3, la prateria: "Il prato" (cinghiali e vespe), "Lo stagno" (arrivano i rospi, fiume con il
  guado, laghetto e trampolini), "Il bosco" (arriva l'orso, tanta erba alta), arena "La grande
  quercia". Formato del file e caratteri in `maps/LEGGIMI.txt`; le mappe modificate si mettono
  nella cartella `mappe/` della console e sostituiscono quelle di serie (con un errore il gioco
  usa quella di serie e dice nel menu cosa non va). Nella 0.3 tutte le mappe sono aperte; dalla
  0.4 si aprono in ordine (sopra).

## 9. Progressione e bonus

- [deciso 13/10] **Crescita dei personaggi**: ogni personaggio sale di livello giocando (esperienza
  dai mostri eliminati e dalle mappe finite), fino al livello 10; ogni livello dà un po' più di
  vita e di danno. I livelli restano salvati. In Contro i livelli non contano: tutti alla pari.
- [deciso 13/10] **Bonus da raccogliere**, che durano una decina di secondi: scudo, colpi più forti,
  velocità. Escono dalle casse che si rompono e ogni tanto dai mostri eliminati.
  - [deciso 29/10] Nelle casse che si rompono (adesso vuote) c'è un bonus, uno a caso tra quattro:
    **velocità** (+40% per 10 s), **furia** (danni ×2 per 10 s), **scudo** (nessun danno per 6 s),
    **carica** (speciale subito pieno). Si prende passandoci sopra; icona e tempo che resta nella
    tessera del giocatore. Le casse col talismano restano col talismano; dai mostri per ora niente.
  - [proposta] Lo scudo nel gioco si chiama INVINCIBILE: SCUDO è già il nome di una carta.
- [deciso 13/10] Niente monete e niente negozio.
- [deciso 15/10] Salire di livello sblocca carte nuove per quel pupazzo, cosi' i livelli non sono
  solo numeri (0.4.1).
- [deciso 17/10] Giri di ogni pianeta aperti con le stelle: capitolo 8.
- Talismani e bombe: capitolo 4.

## 10. Menu e schermate

- [deciso 13/10] Percorso: titolo → Insieme o Contro → mondo e mappa (Insieme) o arena e squadre
  (Contro) → scelta dei personaggi, ognuno col suo pad → partita → fine mappa con le statistiche.
- [deciso 13/10] Nei menu A conferma e B torna indietro.
- [deciso 29/10] **Batteria della console** in alto a destra, nei menu e in partita.
  - [proposta] Icona con la carica letta dalla console, fulmine quando è in carica, rossa e
    lampeggiante sotto il 15%; in partita la scritta dei mostri si sposta a sinistra.
- Pausa: capitolo 3; chi entra a partita iniziata: capitolo 2.
- 0.3.2: pausa con RIPRENDI, MIRA (extra morbida, morbida, media, rapida: si cambia con ← → o A,
  vale per chi ha aperto la pausa), ANTIALIAS (sì o no, per tutti; ASSENTE se la GPU non lo
  permette), LASCIA LA PARTITA, TORNA AL MENU; sotto, l'elenco dei comandi.
- 0.4: Insieme porta alla storia la prima volta, poi direttamente alle mappe; Contro dice "arriva
  con la versione 0.6". Menu delle mappe con le stelle di ogni mappa, "chiusa" per quelle non
  ancora aperte (A spiega "si apre finendo la mappa prima") e il totale delle stelle. Fine mappa:
  colonne liberati, danni, cadute; poi le tre stelle una alla volta (A vale dopo l'ultima);
  nell'arena il titolo e' "BOSS LIBERATO!". Pausa con 7 voci: RIPRENDI, MIRA, ANTIALIAS, MUSICA,
  EFFETTI, LASCIA LA PARTITA, TORNA AL MENU (MIRA, ANTIALIAS e volumi si salvano). La scelta delle
  carte: un riquadro per giocatore, affiancati (con 4 giocatori i nomi lunghi si accorciano).
- [deciso 19/10] Stile nuovo per tutte le schermate (capitolo 5). **Come si gioca?**: la struttura
  resta, si rifà la scelta Insieme/Contro. **Pausa**: via l'elenco dei tasti; una voce del menu li
  mostra in un riquadro a parte. **Scegli la mappa**: il riquadro al centro dello schermo, rifatto.
  Il pianeta Terra del menu delle mappe va bene così per ora.
  - *0.5.2*: Insieme e Contro sono due schede affiancate (Contro con IN ARRIVO). Pausa: RIPRENDI,
    COMANDI, MIRA, ANTIALIAS, MUSICA, EFFETTI, LASCIA LA PARTITA, TORNA AL MENU; COMANDI apre il
    riquadro dei tasti (A o B lo chiudono, la pausa resta). Mappe: il pianeta con il suo giro, le
    sue mappe numerate (stelle oppure CHIUSA, il boss con l'etichetta), le stelle del giro con le
    tacche e quante ne servono ("10 → GIRO 2"), i pianeti in arrivo; gli avvisi (mappa chiusa,
    Contro) compaiono al posto del piede. Scelta degli eroi: sopra le teste due righe con chi è,
    colpo (R1) e speciale (L1). Carte e fine mappa nello stesso stile, dentro il margine; in pausa e
    durante le carte la riga in alto della partita si nasconde.
- [deciso 20/10] **Prima schermata**: NUOVA PARTITA, CARICA SALVATAGGIO, GESTISCI SALVATAGGI (una
  schermata a parte per duplicare ed eliminare i salvataggi), LINGUA; resta ESCI. Salvataggi nel
  capitolo 12.
- [deciso 29/10] **Nome dei salvataggi** modificabile: si scrive subito dopo NUOVA PARTITA e si cambia
  con RINOMINA in GESTISCI SALVATAGGI.
  - [proposta] Tastiera a schermo (lettere, numeri e qualche segno, al massimo 12 caratteri): croce
    o levetta per muoversi, A scrive, B cancella, START conferma. Nome di partenza "PARTITA N". Il
    nome si vede in CARICA e GESTISCI, sopra pianeta e giro.
- [deciso 29/10] **Carte**: non compaiono più di colpo. Prima un banner "RITIRA LE TUE CARTE" con A per
  scegliere; ognuno preme A e si apre il suo riquadro con le 3 carte (il gioco resta fermo come
  prima). Una A tenuta da prima non conta.
- [deciso 20/10] **Lingua**: dalla prima schermata una pagina per scegliere tra inglese, italiano,
  spagnolo, portoghese (del Portogallo), cinese (semplificato), giapponese e hindi.
  - [proposta, fatta nella 0.5.3] Ogni lingua scritta nella sua lingua (English, Italiano, Español,
    Português, 中文, 日本語, हिन्दी); cambia subito tutto il gioco e resta salvata nella console.
    Traduzioni di Claude, di tutti i testi (menu, storia, carte, messaggi, nomi delle mappe di serie
    e dei pianeti; i nomi degli eroi trascritti in cinese, giapponese e hindi); caratteri Noto Sans
    CJK per cinese e giapponese e Poppins per l'hindi (licenza libera), solo quelli che servono.
    Restano in italiano il registro e il formato delle mappe personalizzate con i suoi errori.
- [deciso 17/10] Icone dei giocatori in alto a sinistra (capitolo 2) e giro sempre in vista
  (capitolo 8).
  - [proposta, fatta nella 0.5] La riga in alto (pianeta, mappa, giro, zona, mostri; nell'arena la vita
    del boss) si sposta a destra delle icone. Menu delle mappe: per il pianeta il giro e le stelle
    del giro ("10 per il giro 2"); fine dell'arena: "GIRO 2 APERTO!" oppure quante stelle mancano.

## 11. Suoni e musica [deciso 15/10: dalla 0.4, prima era rimandato alla 1.0]

- Effetti per colpi, esplosioni, salti, raccolte, mostri, cancelli e menu; una musica per i menu,
  una per mondo, una per i boss; tutto generato dal codice (niente suoni presi altrove).
- Volume della musica e degli effetti nella pausa, salvato.
- Provato il 15–16/10 (0.4): in HDMI la musica è stata abbassata da 7 a 1, gli effetti da 8 a 7.
- [proposta, fatta nella 0.5] Musiche nuove per la Terra: una per ogni meraviglia (Piramidi, Machu Picchu,
  Muraglia, con i colori di quei posti) e una per il Colosseo; effetti nuovi per il fucilone e i
  razzi di Astro, i mostri e la capsula.
- *0.5*: musiche nuove, più leggere e sotto gli effetti (5–9 dB meno delle vecchie dove stanno gli
  effetti): titolo "spaziale" (do maggiore, 96 battiti al minuto), Le Piramidi (re hijaz, 116),
  Machu Picchu (mi minore pentatonico, flauti e charango, 112), La Grande Muraglia (sol
  pentatonico, guzheng ed erhu, 104), il Colosseo (si minore armonico, fanfara e tamburi, 142),
  vittoria (do maggiore). Effetti nuovi per eroi, mostri, gladiatore e capsula. Musica di partenza
  6 (effetti 8). Avvio del sonoro da circa 441 ms a pochi millisecondi (livelli degli effetti
  già misurati nel codice).
- 0.4: sintetizzatore nel core (48 kHz stereo), 47 effetti, fino a 24 voci insieme
  (gli effetti uguali ravvicinati si fondono, i piu' deboli lasciano il posto), limitatore a
  -1 dBFS. Musiche: titolo e menu (fa maggiore, 104 battiti al minuto), prateria (re misolidio,
  128), boss (si minore, 144), vittoria a fine mappa (re maggiore, 3,6 s). Gli effetti si sentono
  a sinistra o a destra secondo dove sono rispetto al centro dell'inquadratura e piu' piano
  fuori dallo schermo; in pausa e nella scelta delle carte la musica si abbassa. Volumi da 0 a
  10: musica 7 e effetti 8 di partenza.

## 12. Salvataggi e record

- [deciso 13/10] Salvataggio automatico sulla console principale, nella cartella dei salvataggi di
  RetroArch (un aggiornamento della MOD svuota APPS, quella cartella no): personaggi sbloccati,
  livelli dei personaggi, mappe raggiunte, record.
- [deciso 15/10] Dalla 0.4: mappe aperte, stelle, storia gia' vista, mira, antialias e volumi.
- 0.4: file di testo `pupazzi.sav` (righe chiave=valore, commentato) nella cartella dei salvataggi
  che RetroArch passa al core: sulla console il RetroArch del produttore la porta su
  `/mnt/mmc/saves_RA/` (`/mnt/sdcard/saves_RA/` con la seconda scheda), con la configurazione di
  serie nella sottocartella `Pupazzi/`; se manca e non si puo' creare, si salva tra i risultati
  del gioco. Si scrive a fine mappa, dopo la storia, alla
  chiusura della pausa se si e' cambiato qualcosa e all'uscita; scrittura atomica (file
  temporaneo, fsync, rinomina). Lettura tollerante: righe rovinate ignorate, valori riportati nei
  limiti, chiavi sconosciute conservate (versioni diverse non si cancellano i dati a vicenda); un
  file senza niente di valido si tiene da parte come `pupazzi.sav.illeggibile` (dalla 0.5.3 anche
  uno che la scheda non riesce a leggere, o troppo grande; se non lo si riesce a mettere da parte
  non lo si sostituisce).
- Provato il 15–16/10: `/mnt/mmc/saves_RA/Pupazzi/pupazzi.sav`, creato al primo avvio e riletto a
  ogni avvio dopo.
- [deciso 20/10] **Più salvataggi**: si crea una partita nuova, si carica un salvataggio, e in una
  schermata a parte si duplicano e si eliminano quelli che ci sono.
  - [proposta] 4 salvataggi. Per ognuno: pianeta e giro, stelle, mappe aperte, tempo di gioco.
    Nuova partita nel primo posto libero (se sono pieni chiede quale sovrascrivere); Carica con
    l'elenco (voce grigia se non ce n'è nessuno); Duplica nel primo posto libero, Elimina con la
    conferma. Nel salvataggio solo la partita: lingua, volumi, mira e antialias restano unici per
    la console. Il salvataggio di oggi diventa il numero 1.
  - [deciso 29/10] Ogni partita ha un nome (capitolo 10): chiave `nome=` nel suo file.
  - *0.5.3*: impostazioni in `pupazzi.sav` (con `lingua=` e `salvataggi=1`), partite in
    `partita1.sav` ... `partita4.sav` (con `tempo=`, i secondi di gioco); al primo avvio i progressi
    del `pupazzi.sav` della 0.5.2 (o della 0.4) diventano `partita1.sav`, una volta sola (se la
    scrittura non riesce resta `salvataggi=0` e si riprova all'avvio dopo).
- [proposta, fatta nella 0.5] Per ogni pianeta il giro raggiunto e le stelle del giro in corso; le stelle
  e le mappe della prateria (0.4) non valgono più per la Terra.
- *0.5*: chiavi nuove `giro.<pianeta>` e `mappe.<pianeta>` (mappe aperte in ordine), `stelle.<mappa>`
  del giro in corso, `storia_pianeti` (la storia nuova); le chiavi della 0.4 (`mappe_sbloccate`,
  `storia_vista`, `stelle.prateria*`) restano nel file e non contano. Con il salvataggio della 0.4
  restano mira, antialias e volumi.

## 13. Vincoli tecnici [vincolo]

- Anbernic RG35XX Pro: 4 core Cortex-A53 a 1,5 GHz, GPU Mali-G31, 1 GB di RAM; Stock OS MOD
  4.0.3; il gioco è un core per il RetroArch a 32 bit della console.
- 640×480 a 60 fotogrammi al secondo fissi, anche in HDMI; disegno con la GPU (OpenGL ES 2).
- Antialias: sulla Mali con l'estensione `EXT_multisampled_render_to_texture` (i campioni
  restano nella memoria interna della GPU), altrimenti con OpenGL ES 3; senza nessuno dei due il
  gioco va senza antialias.
- Il gioco non cambia mai risoluzione mentre gira: con la Mali RetroArch si riavvierebbe.
- Partita deterministica: la stessa partita, bit per bit, su ogni processore (base per un
  eventuale gioco in rete e per i replay).

## 14. Piano di sviluppo [deciso 13/10]

Il gioco completo si consegna a tappe; ognuna si prova sulla console prima della successiva.

| Versione | Contenuto |
|---|---|
| 0.3 | grafica nuova in stile cartone; prateria completa (3 mappe e boss); i 4 personaggi iniziali; bombe; menu — consegnata il 13/10, provata il 14/10 |
| 0.3.1 | mira a ventaglio per tutti; levetta destra più morbida con tre livelli nella pausa — consegnata il 14/10, provata |
| 0.3.2 | visuale più larga; immagine meno pixellata (antialias, testi nuovi); salto R2 e bomba L2; mira extra morbida (quattro livelli, di partenza morbida) — consegnata il 15/10, da provare |
| 0.4 | suoni e musica; carte ai cancelli, anche combinate; 3 stelle per mappa con il prigioniero nascosto; salvataggi (mappe che si aprono in ordine); mostri con i fili che vengono liberati; storia all'inizio — consegnata il 15/10, provata il 15 e il 16/10 (con dentro la 0.3.2) |
| 0.4.1 | zone con compiti diversi e mini-boss; scenette col Burattinaio; Fungo e Girasole giocabili, liberati dall'albero gigante; livelli che sbloccano carte; bonus; cappelli e difficoltà Incubo — superata il 17/10: compiti nelle zone, mini-boss, livelli e bonus dopo la Terra, il resto non c'è più |
| 0.5 | [deciso 17/10] tema del sistema solare: la Terra (Piramidi, Machu Picchu, Grande Muraglia, Colosseo) e i 4 eroi iniziali al posto di prateria e pupazzi; visuale più larga con il limite tra i personaggi uguale alla visuale; vite; icone dei giocatori; stelle che aprono i giri di ogni pianeta; mappe due terzi più grandi e meno pixellate; comandi delle altre console senza ritardo — consegnata il 17/10, da provare |
| 0.5.1 | [deciso 18/10] visuale minima più lontana; mappe tutte giocabili senza acqua attraversabile, paesaggio ridotto; gladiatore che insegue e combatte; Chichén Itzá con i cenote al posto di Machu Picchu; scorpione e arciere di terracotta al posto di sciacallo e lama — consegnata il 18/10 |
| 0.5.2 | [deciso 19/10] interfaccia nuova (stile Orbita, colori Tramonto) in tutte le schermate e nell'HUD; pausa con la voce COMANDI; mappe al centro — consegnata il 19/10, provata il 20/10 ("molto bella") |
| 0.5.3 | [deciso 20/10] visuale vicina senza HDMI; più salvataggi (nuova, carica, gestisci: duplica ed elimina); 7 lingue — consegnata il 20/10, da provare |
| 0.5.4 | [deciso 29/10] nomi dei salvataggi; antialias migliore; CPU nel riquadro delle prestazioni; via la storia; banner prima delle carte; bonus nelle casse; batteria in alto a destra; cancelli a battente |
| 0.6 e dopo | un pianeta per versione, nell'ordine del viaggio: Luna, Marte, Venere, Mercurio, Giove, Saturno, Urano, Nettuno; compiti nelle zone, mini-boss, livelli e bonus; modalità Contro |
| molto più avanti | [deciso 20/10, rimandato il 29/10] gioco in rete con ognuno il suo schermo e la telecamera sul proprio eroe |
| dopo | app per le console-controller; gioco via internet (da valutare) |

## 15. Registro delle decisioni

- **11/10/2026**: gioco tipo Brawl Stars, cooperativo da 1 a 4; personaggi reattivi; comandi;
  una sola telecamera; per iniziare forme a tinta unita senza texture; primo livello facile poi
  sempre più difficili; suoni più avanti; regole di mira, attacchi, vita e livelli.
- **11/10/2026**, dopo la prova della 0.2.0: reattività "perfetta"; vita piena a ogni nuovo
  livello e a ogni zona rifatta; più giocatori, più nemici.
- **13/10/2026**: nome definitivo "Pupazzi"; prima di sviluppare oltre si scrive la specifica
  completa; grafica: cartone animato 3D come Brawl Stars, originale, leggibile a 640×480
  (capitolo 5); più di 10 mappe; struttura delle mappe, difficoltà, elementi, misure e formato
  (capitolo 8). Scartata l'ambientazione nei posti di casa; decisa quella all'aperto: prateria,
  deserto, neve, vulcano. Personaggi: tanti, ognuno con colpo normale e speciale diversi da tutti
  gli altri (capitolo 6). Bombe da raccogliere nella mappa, che esplodono 3 secondi dopo essere
  state lasciate o colpite (capitolo 4). Accettate tutte le proposte su personaggi e bombe:
  12 personaggi abitanti dei mondi, 4 subito e gli altri da sbloccare, ruoli, vita e velocità
  diverse, salto uguale, nomi proposti da Claude, scelta prima di ogni mappa, doppioni ammessi,
  nessun potere extra; bombe con A, fino a 3, danno ai soli nemici. Comandi: B salto, R1 colpo
  normale, L1 colpo speciale (non più R2), A bombe. Multiplayer: accettati i punti su
  collegamento, pad, ingresso e uscita, controller scollegati, compagni, statistiche, progressi;
  internet in futuro; due modalità, Insieme (contro i nemici del computer) e Contro (2 contro 2),
  con le regole proposte per Contro. Nemici: mostri tipici di ogni mondo; accettate tutte le
  proposte del capitolo 7 (4 mostri e un boss per mondo, comportamenti, trucchi per mondo, elenco).
  Accettati: niente vite da soli, sblocco in ordine, niente scelta di difficoltà, crescita dei
  personaggi, bonus, niente monete, salvataggi, menu, bombe che tornano, piano a tappe. Niente
  mira automatica: si mira con la levetta destra, come in Brawl Stars.
- **13/10/2026**: via alla 0.3 ("ok puoi cominciare"); consegnata la 0.3.0 con le proposte di
  Claude per i dettagli della mira, i 4 personaggi iniziali e l'albero gigante, da provare.
- **14/10/2026**, dopo la prova della 0.3.0: gioco "fluidissimo", non si blocca mai; le rifiniture
  di gioco si fanno quando è costruito tutto. Mira a ventaglio per tutti, con i colpi che coprono
  il ventaglio; levetta destra più morbida (era troppo sensibile), con tre livelli nella pausa;
  accettate le proposte per Paglia (raffica aperta sul ventaglio) e Fiocco (3 palle di neve). Si
  consegna come 0.3.1, poi la 0.4 come da piano.
- **15/10/2026**, dopo la prova della 0.3.1: delle tre mire la migliore è morbida, che diventa
  quella di partenza; si aggiunge la mira extra morbida (quattro livelli nella pausa). Visuale più
  larga (va bene tutto un po' più piccolo); immagine meno pixellata ("proviamoci"); salto su R2 e
  bomba su L2. Si consegna come 0.3.2.
- **15/10/2026**, proposta di Claude per rendere la campagna "un gioco figo serio" (oggi una mappa e'
  "pulisci 3 zone" e alla fine non resta niente in mano; il gioco e' muto): accettati tutti i punti.
  Storia del Burattinaio con i fili e i mostri che si liberano; carte a ogni cancello, anche
  combinate tra pupazzi; 3 stelle per mappa con il prigioniero nascosto; zone con compiti diversi e
  mini-boss; suoni e musica subito. Piano nuovo: 0.4 (stessa sera) sonoro, carte, stelle con il
  prigioniero e salvataggi, fili e mostri liberati, storia all'inizio; 0.4.1 il resto; il deserto
  passa alla 0.5 e il resto del piano scala di una tappa. La 0.3.2 si prova direttamente dentro la 0.4.
- **15/10/2026**: via alla 0.4 ("Ok parti con la 0.4"); consegnata lo stesso giorno con le proposte di
  Claude per elenco e valori delle carte, posizione dei prigionieri, testo della storia, suoni e
  musiche: tutto da provare.
- **17/10/2026**, dopo la prova della 0.4 (15 e 16/10, in HDMI, fino a 4 giocatori): 60 fps fissi,
  salvataggi a posto; Remote RetroPad inutilizzabile (comandi che si incantano). Punch list di
  Andrea: visuale ancora più lontana e limite tra i personaggi uguale alla visuale; 3 vite (da soli
  si riparte dalla zona, finite le vite dalla mappa; in più giocatori finite le vite non si rientra
  e se si cade tutti insieme si riparte dalla mappa, anche con vite rimaste); icone dei giocatori in
  alto a sinistra con la barra rossa per chi è a terra; almeno 10 stelle per aprire il giro
  successivo di un mondo, stelle da capo a ogni giro, giro sempre in vista; tema ufficiale del
  sistema solare (un pianeta per mondo, Luna compresa; eroi fantasiosi dei pianeti; dalla Terra un
  astronauta con un fucilone; niente burattini; nemici e boss di ogni pianeta); mappe due terzi più
  grandi e meno pixellate; la Terra con le meraviglie e il boss nel Colosseo. Accettate le proposte
  di Claude su eroi, pianeti e Terra ("vanno benissimo"); il nome resta Pupazzi. Via alla 0.5.
- **17/10/2026**, sera: consegnata la 0.5 con le proposte di Claude per i dettagli: mappa da capo come
  appena scelta; terza stella "senza ricominciare la mappa"; giro di ogni pianeta con le sue mappe
  aperte (dalla prima, anche per chi aveva finito la prateria); difficoltà dei giri come i giri della
  0.4 (+60% di vita e +25% di danni a giro); facce degli eroi con i cuori in alto a sinistra;
  telecamera da 15 a 30 m; terreno morbido costruito sugli angoli delle caselle; modelli, musiche
  ed effetti nuovi; comandi delle altre console letti dal gioco (RetroArch non apre più le porte).
  Tutto da provare sulla console.
- **18/10/2026**, dopo la prova della 0.5: visuale minima più lontana (anche da soli); bug dell'acqua
  alle Piramidi (saltando nel Nilo si camminava nella mappa non usata e si passava nelle altre
  zone): la mappa disegnata deve essere tutta giocabile, ridotta, e senza bug del genere; il
  gladiatore deve muoversi, inseguire e attaccare in modo sensato; Machu Picchu brutta: al suo
  posto Chichén Itzá, con i cenote in cui si cade e si perde una vita; al posto di sciacalli e
  lama, scorpioni e arcieri di terracotta. Via alla 0.5.1.
- **19/10/2026**: prima di proseguire si sistema l'interfaccia generale, il meno retrò possibile
  ("una grafica ai tempi d'oggi che sia semplicissima ma allo stesso tempo davvero figa"), per tutte
  le schermate di ora e le prossime: Come si gioca? da rifare solo nella scelta Insieme/Contro;
  pausa senza l'elenco dei tasti, che si leggono da una voce del menu in un riquadro; Scegli la
  mappa con il riquadro al centro. Il pianeta Terra va bene per ora. Dai 4 mockup scelto lo stile
  Orbita, dalle 4 varianti di colore Tramonto. Via alla 0.5.2.
- **20/10/2026**, dopo la prova della 0.5.2 ("è molto bella"): si aggiunge sostanza. Più salvataggi
  (prima schermata con nuova partita, carica salvataggio, gestisci salvataggi per duplicarli ed
  eliminarli); lingua da scegliere nella prima schermata tra inglese, italiano, spagnolo,
  portoghese del Portogallo, cinese semplificato, giapponese e hindi; gioco in rete con ognuno il
  suo schermo e la telecamera centrata sul proprio eroe; visuale vicina quando si gioca sullo
  schermo della console, come ora con l'HDMI. Ordine proposto: 0.5.3 visuale, salvataggi e lingue;
  0.5.4 gioco in rete. Si comincia al via.
- **29/10/2026**: lista per la 0.5.4. Nome dei salvataggi modificabile (anche subito dopo nuova
  partita); antialias "x2", cioè che si veda meglio; nel riquadro di SELECT anche il carico della
  CPU; via la storia; le carte non devono comparire all'improvviso: prima un banner "ritira le tue
  carte" e A per scegliere; un bonus nelle casse che si rompono (adesso vuote): velocità, furia,
  scudo, carica; il simbolo della batteria in alto a destra; cancelli a battente che si chiudono
  bene. Il gioco in rete si farà molto più avanti. Via alla 0.5.4.
