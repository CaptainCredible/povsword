// ATtiny85 side of the optical receiver: boot button, LDR/ADC, receive loop.
#pragma once
#include <stdint.h>
#include "pov_protocol.h"

// Read the programming button on PB0 (debounced). Call first thing in setup(),
// before the charlieplex matrix is driven. Leaves PB0-PB3 as plain inputs.
bool bootButtonPressed();

// LDR on PB4 via ADC2: set up once, then readLdr() gives the averaged raw
// reading, 0..1023 (LOW = lit).
void ldrBegin();
uint16_t readLdr();

// Debounced LDR state (true = white square seen). Valid during receiveImage().
bool isLit();

// Why the last receiveImage() failed, for showing on the LEDs:
//   0 never locked onto a handshake (no signal, or not enough contrast)
//   1 handshake locked, but the long dark gap after it was wrong
//   2 training burst unreadable (couldn't measure the unit)
//   3 leader missing
//   4 row marker missing
//   5 unreadable bit (pulse spacing out of range)
//   6 extra pulses after the last row
//   7 row parity error
uint8_t receiveFailCode();
uint8_t receiveRowsDone();   // complete, parity-checked rows of the last attempt

// Run the receive state machine until a frame is decoded (true, image filled)
// or it fails / times out (false, image untouched). Blocking. onSample, if
// given, is called with every raw LDR reading (keep it to a few us).
// resume = continue the transfer rxWatchPoll() has locked onto.
bool receiveImage(uint8_t image[POV_IMAGE_BYTES], void (*onSample)(uint16_t raw) = 0,
                  bool resume = false);

// Watch the LDR for the sender's handshake while the display runs.
// rxWatchPoll() never waits on the ADC (a few us, ~100 us every 4th reading),
// so call it often (every LED step). It returns true once the handshake is
// locked: then call receiveImage(image, ..., true) to receive the rest.
void rxWatchBegin();
bool rxWatchPoll();
