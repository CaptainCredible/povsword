// LDR level -> lit/dark, with self-calibrating threshold and hysteresis.
// Pure logic (no hardware access) so it can be tested on a PC.
#pragma once
#include <stdint.h>
#include "pov_protocol.h"

struct RxLight {
  uint16_t blkMin[LIGHT_BLOCKS];   // per-block extremes of the raw ADC reading
  uint16_t blkMax[LIGHT_BLOCKS];
  uint32_t blkStart;
  uint8_t  blk;
  uint16_t tLow;     // go-lit when reading drops below this
  uint16_t tHigh;    // go-dark when reading rises above this
  bool     valid;    // enough swing seen to have a threshold
  bool     frozen;   // stop calibrating (set once the handshake is locked)
  bool     lit;
};

void lightInit(RxLight &l, uint32_t now);

// Feed one ADC reading (0..1023, LOW = lit). Returns the debounced lit state.
bool lightUpdate(RxLight &l, uint32_t now, uint16_t adc);
