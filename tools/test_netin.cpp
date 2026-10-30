// Prove dei comandi che arrivano a chi ha creato la partita in rete (0.5.4, revisione): numeri dei
// comandi sbagliati o assurdi (un messaggio costruito apposta, o rovinato) non devono bloccare il
// gioco ne' far usare comandi vecchi; quelli giusti si usano in ordine, anche con i buchi, i
// doppioni e dopo un lungo stop. Si prova la parte di src/net.cpp che li legge, senza rete.
//   make build/native/test_netin && ./build/native/test_netin
#include "../src/net.cpp"
#include <signal.h>

static int g_fail = 0;

static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "  ok    " : "  ERRORE", what);
  if (!ok) g_fail++;
}

static void on_alarm(int) {
  printf("  ERRORE bloccato: host_take non finisce\n");
  fflush(stdout);
  _exit(3);
}

// Un messaggio INPUT del giocatore 2 con i comandi first..first+n-1 (tasti = numero del comando).
static void input(uint32_t first, int n) {
  Wr w;
  w.head(T_INPUT);
  w.u32(S.session);
  w.u32(0xABCDu);
  w.u32(0);
  w.u32(1234);
  w.u16(0);
  w.u32(first);
  w.u8((uint32_t)n);
  for (int k = 0; k < n; ++k) {
    NetPad p = neutral();
    p.btn = (uint16_t)(first + (uint32_t)k);
    w.pad(p);
  }
  sockaddr_in from;
  memset(&from, 0, sizeof(from));
  from.sin_family = AF_INET;
  handle(w.b, w.n, from);
}

static Peer& fresh_peer(void) {
  memset(&S, 0, sizeof(S));
  S.fd = -1;
  g_inited = true;
  S.role = R_HOST_PLAY;
  S.session = 0x1111u;
  Peer& p = S.peer[1];
  p.used = true;
  p.id = 0xABCDu;
  p.player = 1;
  p.acked = true;
  return p;
}

int main() {
  plat_log_open("/dev/null");
  signal(SIGALRM, on_alarm);
  alarm(10);
  char buf[200];
  {
    Peer& p = fresh_peer();
    input(0xFFFFFFFFu, 1);
    check(!p.any, "primo comando con un numero assurdo (2^32-1): scartato");
    // Uno a fotogramma, come arrivano di solito (con due o piu' in attesa se ne usano due insieme).
    input(0, 1);
    const NetPad a = host_take(p);
    input(0, 2);
    const NetPad b = host_take(p);
    input(0, 3);
    snprintf(buf, sizeof(buf), "poi i comandi 0, 1 in ordine, ripetuti nei messaggi dopo (presi %u e %u)", a.btn, b.btn);
    check(p.any && a.btn == 0 && b.btn == 1, buf);
    input(0xFFFFFFF0u, 4);
    input(1u << 30, 2);
    input(0, 2);
    const uint32_t top = p.top;
    const NetPad c = host_take(p);
    snprintf(buf, sizeof(buf), "numeri indietro (anche dopo il giro), troppo avanti e doppioni: scartati (preso %u, il piu' alto %u)", c.btn, top);
    check(c.btn == 2 && top == 2, buf);
    for (int f = 0; f < 20; ++f) host_take(p);
    check(true, "ripete l'ultimo senza bloccarsi quando non arriva nulla");
  }
  {
    // Chi ha creato la partita si era fermato: i comandi arrivano molto avanti (ma possibili).
    Peer& p = fresh_peer();
    input(0, 2);
    host_take(p);
    host_take(p);
    input(200, 3);
    uint32_t got = 0;
    for (int f = 0; f < 10; ++f) {
      const NetPad x = host_take(p);
      if (x.btn > got) got = x.btn;
    }
    snprintf(buf, sizeof(buf), "molto avanti (da 2 a 200): si va li' e si usano (ultimo usato %u, saltati %u)", got, p.skips);
    check(got == 202 && p.next == 203u, buf);
  }
  {
    // Numeri vicini al giro dei 32 bit (dopo anni di partita): si continua in ordine.
    Peer& p = fresh_peer();
    input(0, 1);
    host_take(p);
    p.next = p.top = 0xFFFFFFFEu;
    uint16_t seq[4];
    for (int k = 0; k < 4; ++k) {
      input(0xFFFFFFFEu + (uint32_t)k, 1);
      seq[k] = host_take(p).btn;
    }
    snprintf(buf, sizeof(buf), "attraverso il giro dei numeri: %04x %04x %04x %04x", seq[0], seq[1], seq[2], seq[3]);
    check(seq[0] == 0xFFFE && seq[1] == 0xFFFF && seq[2] == 0x0000 && seq[3] == 0x0001, buf);
  }
  if (g_fail == 0) printf("tutto bene\n");
  else printf("%d prove fallite\n", g_fail);
  return g_fail ? 1 : 0;
}
