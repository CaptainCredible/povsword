// Received image: EEPROM storage as display columns, any width up to
// POV_MAX_COLS. Layout in pov_protocol.h (EEPROM section).
#pragma once
#include <stdint.h>
#include "pov_protocol.h"

// Writing an image: imageBegin(), imagePut() each column in order, then
// imageCommit(). Begin invalidates the stored image straight away, so a
// transfer that fails halfway leaves no image (the sword falls back to
// userImage) rather than a mix of old and new. imagePut() is safe to call
// mid-transfer: a column write takes ~7 ms, and the next pulse is >= 4u away.
void imageBegin();
void imagePut(uint8_t col, uint16_t pattern);

// Store the count and CRC, read everything back, and only then write the
// magic byte. False if the read-back doesn't match (image left invalid).
bool imageCommit(uint8_t count);

// Columns in the stored image, or 0 if there is no valid one (magic or CRC).
uint8_t imageStored();

// Column `col` of the stored image: bit 11 = top row, bit 0 = bottom row,
// matching displayPattern() in the sketch.
uint16_t imageGet(uint8_t col);

// Column `col` (0 = leftmost) of a square frame's packed rows.
uint16_t squareColumn(const uint8_t rows[POV_IMAGE_BYTES], uint8_t col);
