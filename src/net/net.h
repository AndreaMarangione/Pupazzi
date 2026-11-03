// LAN game on the same Wi-Fi (0.5.4), each player on their own screen. Only in the core (not in the
// game tests): UDP sockets, game search, waiting room, inputs.
//
// One console creates the game (it answers searches on port 55410, or on the next 9 if that one is
// busy); the others look for it by sending a message to the whole network, join (same version and
// same maps, checked by the host) and wait for it to start. Up to 4 consoles, one player per
// console: the host is player 1, the others get the first free slot.
//
// During a game only the inputs (buttons and sticks of each frame) travel between the consoles: the
// game runs the same game, bit for bit, on every console. The host decides: on each of its frames
// it gathers everyone's inputs (its own right away, those of the other consoles in the order they
// read them) and sends them to all; the others run a frame when they receive it, with 1-8 buffered
// frames to absorb the Wi-Fi hiccups (the buffer grows when they run out and shrinks when frames
// are left over). So the host never waits for anyone, and the inputs of the other consoles reach
// the game after the round-trip time plus the buffer (at home usually 40-60 ms). Each message
// repeats what the other side has not confirmed yet: a lost message loses no inputs.
//
// Every half second the host also sends the game hash (game_state_hash): if it comes out different
// on a console, that console leaves the game (it only happens with versions of the game that do
// not run the same game). A console that disconnects (or does not answer for 10 seconds) leaves:
// its hero leaves the game, the others carry on. If the host leaves, the others go back to the
// title. Progress is saved only on the host; the others play with the host's progress
// (save_guest_begin) and keep their own saves as they are.
//
// Environment variables for tests:
//   PUPAZZI_RETE=crea[:N]      creates the game right away (save 1) and starts when there are N
//                              other consoles (1 if omitted)
//   PUPAZZI_RETE=unisciti      searches and joins the first game it finds (unisciti!: even an
//                              incompatible one, to test the refusals)
//   PUPAZZI_RETE_DOPO=n        create or join after n frames (before that you play alone: with
//                              PUPAZZI_MAPPA and PUPAZZI_BOT the quick game)
//   PUPAZZI_RETE_PORTA=n       first port instead of 55410
//   PUPAZZI_RETE_PERDITA=p     drops p percent of the outgoing messages
//   PUPAZZI_RETE_RITARDO=a[+b] delays the outgoing messages by a ms, plus up to b ms at random
//   PUPAZZI_RETE_ATTESA=ms     after how long a console that does not answer leaves (10000)
//   PUPAZZI_RETE_VERSIONE=v    pretends to be version v (refusal test)
#pragma once
#include <stdint.h>
#include "game/game.h"

void net_init(Game& game);           // retro_load_game: environment variables, automatic game (tests)
void net_shutdown(void);             // retro_unload_game, retro_deinit, retro_reset: says goodbye and closes
// Every frame, before the game: reads the network, follows search and waiting room (game_net_view),
// starts or ends the LAN game (game_net_begin, game_net_end).
void net_frame_begin(void);
bool net_session(void);              // LAN game in progress on this console
// This console's inputs for this frame (only during a game); tap: START pressed and
// released right away.
void net_local_pad(const Pad& pad, bool tap);
// The next frame of the game, if it can be run now (false: waiting for the network, or enough
// has already been run for this frame). consoles: slots with a connected console; silent:
// consoles that have not answered for a while (only for the HUD).
bool net_step(Pad pads[MAX_PLAYERS], bool taps[MAX_PLAYERS], uint8_t* consoles, uint8_t* silent);
void net_step_done(uint64_t hash);   // after game_update: hash check
void net_frame_end(void);            // end of the frame: sends
void net_request(int req, int arg);  // menu requests (NR_* in game.h)
uint32_t net_frame_no(void);         // last LAN game frame run (for the traces)

// For the performance overlay and the log.
struct NetStats {
  bool on;                           // LAN game in progress
  bool host;
  int rtt_ms;                        // round-trip time (average); with more consoles the worst one
  int buffer;                        // buffered frames (the other consoles)
  int target;                        // target buffer
  unsigned stalls;                   // times it ran out of frames
  int players;                       // consoles in the game
};
void net_stats(NetStats* s);
