#include "rx_decoder.h"

enum Sym : uint8_t { SYM_BAD, SYM_0, SYM_1, SYM_ROW, SYM_LEADER };

// Classify an onset-to-onset interval against the measured unit.
static uint8_t classify(uint32_t dt, uint32_t u) {
  uint32_t h = dt * 2;   // compare in half-units
  if (h < SYM_MIN_HALF_U * u)    return SYM_BAD;
  if (h < SYM_BIT1_HALF_U * u)   return SYM_0;
  if (h < SYM_ROW_HALF_U * u)    return SYM_1;
  if (h < SYM_LEADER_HALF_U * u) return SYM_ROW;
  if (h < SYM_MAX_HALF_U * u)    return SYM_LEADER;
  return SYM_BAD;
}

// a within +/-25% of b
static bool near(uint32_t a, uint32_t b) {
  return a * 4 >= b * 3 && a * 4 <= b * 5;
}

// Cluster the training intervals into short/long, check they match the
// 0101... pattern, and solve for u. Returns 0 on failure.
static uint32_t solveUnit(const uint32_t *t) {
  uint32_t lo = t[0], hi = t[0], sum = 0;
  for (uint8_t i = 0; i < TRAIN_BITS; i++) {
    if (t[i] < lo) lo = t[i];
    if (t[i] > hi) hi = t[i];
    sum += t[i];
  }
  uint32_t mid = (lo + hi) / 2;
  for (uint8_t i = 0; i < TRAIN_BITS; i++) {
    bool expect = (TRAIN_PATTERN >> (TRAIN_BITS - 1 - i)) & 1;
    if ((t[i] > mid) != expect) return 0;
  }

  uint32_t u = sum / TRAIN_UNITS;
  if (u < UNIT_MIN_US || u > UNIT_MAX_US) return 0;

  // Confirm every training interval decodes correctly at this u.
  for (uint8_t i = 0; i < TRAIN_BITS; i++) {
    bool expect = (TRAIN_PATTERN >> (TRAIN_BITS - 1 - i)) & 1;
    if (classify(t[i], u) != (expect ? SYM_1 : SYM_0)) return 0;
  }
  return u;
}

void rxInit(RxDecoder &d, uint32_t now) {
  d.start = now;
  d.lastOnset = now;
  d.state = RXS_HUNT;
  d.count = 0;
  d.row = 0;
  d.lit = false;
  d.haveOnset = false;
  for (uint8_t i = 0; i < POV_IMAGE_BYTES; i++) d.image[i] = 0;
}

RxResult rxFeed(RxDecoder &d, uint32_t now, bool lit) {
  bool onset = lit && !d.lit;
  d.lit = lit;
  uint32_t dt = now - d.lastOnset;

  // ---- no onset: timeouts, and END detection by silence ----
  if (!onset) {
    switch (d.state) {
      case RXS_HUNT:
        if (now - d.start > RX_LOCK_TIMEOUT_US) return RX_FAIL;
        break;
      case RXS_LOCKED:
      case RXS_TRAIN:
        if (dt > 3 * d.period) return RX_FAIL;
        break;
      case RXS_END:
        // The sender follows END with plain dark, so END's interval never
        // closes; >= 5u of silence after the last parity bit is the END.
        if (dt * 2 >= SYM_LEADER_HALF_U * d.unit) return RX_DONE;
        break;
      default:
        if (dt * 2 >= SYM_MAX_HALF_U * d.unit) return RX_FAIL;
        break;
    }
    return RX_BUSY;
  }

  d.lastOnset = now;

  // ---- onset: dt is the interval of the symbol that just ended ----
  switch (d.state) {
    case RXS_HUNT:
      if (!d.haveOnset || dt < HS_PERIOD_MIN_US || dt > HS_PERIOD_MAX_US) {
        d.count = 0;
      } else if (d.count && !near(dt, d.period)) {
        d.count = 1;
        d.period = dt;
      } else {
        d.period = d.count ? (d.period + dt) / 2 : dt;
        d.count++;
      }
      d.haveOnset = true;
      if (d.count >= HS_LOCK_COUNT) d.state = RXS_LOCKED;
      break;

    case RXS_LOCKED:
      if (near(dt, d.period)) {                       // still handshaking
        if (++d.count > HS_MAX_INTERVALS) return RX_FAIL;
        break;
      }
      if (dt * 2 < d.period * 3 || dt > d.period * 3) return RX_FAIL;
      d.state = RXS_TRAIN;                            // boundary seen; this
      d.count = 0;                                    // onset is training bit 0
      break;

    case RXS_TRAIN:
      d.train[d.count++] = dt;
      if (d.count == TRAIN_BITS) {
        d.unit = solveUnit(d.train);
        if (!d.unit) return RX_FAIL;
        d.state = RXS_LEADER;
        d.row = 0;
      }
      break;

    case RXS_LEADER:
      if (classify(dt, d.unit) != SYM_LEADER) return RX_FAIL;
      d.state = RXS_ROWMARK;
      break;

    case RXS_ROWMARK:
      if (classify(dt, d.unit) != SYM_ROW) return RX_FAIL;
      d.state = RXS_BITS;
      d.bit = 0;
      d.ones = 0;
      d.rowBits = 0;
      break;

    case RXS_BITS: {
      uint8_t s = classify(dt, d.unit);
      if (s != SYM_0 && s != SYM_1) return RX_FAIL;
      d.rowBits = (d.rowBits << 1) | (s == SYM_1);
      d.ones += (s == SYM_1);
      if (++d.bit < POV_N + 1) break;

      if ((d.ones & 1) != ROW_PARITY) return RX_FAIL; // parity over all 13
      uint16_t bits = d.rowBits >> 1;                 // drop the parity bit
      for (uint8_t c = 0; c < POV_N; c++) {
        if (bits & (1 << (POV_N - 1 - c))) {
          uint8_t k = d.row * POV_N + c;
          d.image[k >> 3] |= 0x80 >> (k & 7);
        }
      }
      d.state = (++d.row == POV_N) ? RXS_END : RXS_ROWMARK;
      break;
    }

    case RXS_END:
      // Anything arriving before 5u of silence means we lost count.
      return classify(dt, d.unit) == SYM_LEADER ? RX_DONE : RX_FAIL;
  }
  return RX_BUSY;
}
