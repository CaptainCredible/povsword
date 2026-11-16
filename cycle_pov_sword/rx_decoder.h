// Optical frame decoder: handshake lock -> boundary -> training (measure u)
// -> leader -> 12 x (ROW + 12 bits + parity) -> END, or for a wide image
// leader -> leader -> count -> count x (ROW + 12 bits + parity) -> END.
// Pure logic: fed (timestamp_us, lit) samples, no hardware access, so the
// same code runs on the ATtiny (micros() + isLit()) and in the host test.
#pragma once
#include <stdint.h>
#include "pov_protocol.h"

// RX_COL: still busy, and a wide frame's column just arrived in colBits
// (column number row - 1). Store it before feeding the next sample.
enum RxResult : uint8_t { RX_BUSY, RX_DONE, RX_FAIL, RX_COL };

enum RxState : uint8_t {
  RXS_HUNT,      // looking for the ~2 Hz handshake
  RXS_LOCKED,    // handshake locked, waiting for the long dark boundary
  RXS_TRAIN,     // collecting the 0101... training intervals
  RXS_LEADER,    // next interval must be the LEADER
  RXS_ROWMARK,   // next interval must be a ROW marker (or a 2nd LEADER: wide)
  RXS_BITS,      // 12 data bits + 1 parity bit
  RXS_END        // all rows/columns in; expect silence (END has no closing onset)
};

struct RxDecoder {
  uint32_t start;        // for the lock timeout
  uint32_t lastOnset;
  uint32_t period;       // measured handshake period, us
  uint32_t unit;         // measured u, us
  uint32_t train[TRAIN_BITS];
  uint16_t rowBits;
  uint16_t colBits;      // wide: the column just received (bit 11 = top)
  uint8_t  state;
  uint8_t  count;        // handshake matches / training intervals seen
  uint8_t  row;          // rows (square) or columns (wide) received
  uint8_t  cols;         // wide: column count from the header
  bool     wide;         // second LEADER seen: a wide frame
  bool     haveCount;    // wide: header received
  bool     started;      // first ROW marker seen
  uint8_t  bit;
  uint8_t  ones;
  bool     lit;
  bool     haveOnset;
  uint8_t  image[POV_IMAGE_BYTES];   // square frame's rows, valid after RX_DONE
};

void rxInit(RxDecoder &d, uint32_t now);

// Feed one sample. Call continuously (also while nothing changes: timeouts
// and END detection are driven from here).
RxResult rxFeed(RxDecoder &d, uint32_t now, bool lit);

inline bool rxLocked(const RxDecoder &d) { return d.state != RXS_HUNT; }
