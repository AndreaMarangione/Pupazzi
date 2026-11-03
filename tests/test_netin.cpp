// Tests of the commands that reach the host of a LAN game (0.5.4, review): wrong or absurd command
// numbers (a message crafted on purpose, or corrupted) must not block the game or make it use old
// commands; the right ones are used in order, even with gaps, duplicates and after a long stop.
// Tests the part of src/net/net.cpp that reads them, without a network.
//   make build/native/test_netin && ./build/native/test_netin
#include "net/net.cpp"
#include <signal.h>
#include "test.h"

static void on_alarm(int) {
  printf("  ERRORE bloccato: host_take non finisce\n");
  fflush(stdout);
  _exit(3);
}

// An INPUT message from player 2 with the commands first..first+n-1 (buttons = command number).
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

static char buf[200];

TEST_CASE("1) numeri assurdi scartati, gli altri in ordine") {
  Peer& p = fresh_peer();
  input(0xFFFFFFFFu, 1);
  EXPECT(!p.any, "primo comando con un numero assurdo (2^32-1): scartato");
  // One per frame, as they usually arrive (with two or more waiting, two are used together).
  input(0, 1);
  const NetPad a = host_take(p);
  input(0, 2);
  const NetPad b = host_take(p);
  input(0, 3);
  snprintf(buf, sizeof(buf), "poi i comandi 0, 1 in ordine, ripetuti nei messaggi dopo (presi %u e %u)", a.btn, b.btn);
  EXPECT(p.any && a.btn == 0 && b.btn == 1, buf);
  input(0xFFFFFFF0u, 4);
  input(1u << 30, 2);
  input(0, 2);
  const uint32_t top = p.top;
  const NetPad c = host_take(p);
  snprintf(buf, sizeof(buf), "numeri indietro (anche dopo il giro), troppo avanti e doppioni: scartati (preso %u, il piu' alto %u)", c.btn, top);
  EXPECT(c.btn == 2 && top == 2, buf);
  for (int f = 0; f < 20; ++f) host_take(p);
  EXPECT(true, "ripete l'ultimo senza bloccarsi quando non arriva nulla");
}

TEST_CASE("2) dopo un lungo stop i comandi arrivano molto avanti") {
  // The host had stopped: the commands arrive far ahead (but possible).
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
  EXPECT(got == 202 && p.next == 203u, buf);
}

TEST_CASE("3) numeri vicini al giro dei 32 bit") {
  // Numbers close to the 32-bit wraparound (after years of play): it goes on in order.
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
  EXPECT(seq[0] == 0xFFFE && seq[1] == 0xFFFF && seq[2] == 0x0000 && seq[3] == 0x0001, buf);
}

int main(int argc, char** argv) {
  plat_log_open("/dev/null");
  signal(SIGALRM, on_alarm);
  alarm(10);
  const int failed = run_tests(argc, argv);
  return failed;
}
