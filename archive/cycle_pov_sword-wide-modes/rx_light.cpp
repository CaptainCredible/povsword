#include "rx_light.h"

void lightInit(RxLight &l, uint32_t now) {
  for (uint8_t i = 0; i < LIGHT_BLOCKS; i++) {
    l.blkMin[i] = 0xFFFF;
    l.blkMax[i] = 0;
  }
  l.blkStart = now;
  l.blk = 0;
  l.valid = false;
  l.frozen = false;
  l.lit = false;
}

bool lightUpdate(RxLight &l, uint32_t now, uint16_t adc) {
  if (!l.frozen) {
    // Sliding window of block extremes: old ambient light ages out after
    // ~LIGHT_BLOCKS blocks, while a full handshake period always fits inside.
    if (now - l.blkStart >= LIGHT_BLOCK_US) {
      l.blk = (l.blk + 1) % LIGHT_BLOCKS;
      l.blkMin[l.blk] = 0xFFFF;
      l.blkMax[l.blk] = 0;
      l.blkStart = now;
    }
    if (adc < l.blkMin[l.blk]) l.blkMin[l.blk] = adc;
    if (adc > l.blkMax[l.blk]) l.blkMax[l.blk] = adc;

    uint16_t lo = 0xFFFF, hi = 0;
    for (uint8_t i = 0; i < LIGHT_BLOCKS; i++) {
      if (l.blkMin[i] < lo) lo = l.blkMin[i];
      if (l.blkMax[i] > hi) hi = l.blkMax[i];
    }
    uint16_t swing = hi - lo;
    l.valid = swing >= LIGHT_MIN_SWING;
    if (l.valid) {
      uint16_t mid = lo + swing / 2;
      uint16_t h = swing >> LIGHT_HYST_SHIFT;
      l.tLow = mid - h;
      l.tHigh = mid + h;
    }
  }

  if (!l.valid) {
    l.lit = false;
  } else if (l.lit) {
    if (adc > l.tHigh) l.lit = false;
  } else {
    if (adc < l.tLow) l.lit = true;
  }
  return l.lit;
}
