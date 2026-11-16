// Received 12x12 image: EEPROM storage and conversion to display columns.
#pragma once
#include <stdint.h>
#include "pov_protocol.h"

// Store a packed image. Writes data + CRC first, verifies, and only then
// writes the magic byte, so a power cut mid-write leaves no valid image.
// Returns false if the read-back didn't match (magic left invalid).
bool imageSave(const uint8_t image[POV_IMAGE_BYTES]);

// Load the packed image. False if the magic or CRC doesn't match.
bool imageLoad(uint8_t image[POV_IMAGE_BYTES]);

// Column `col` (0 = leftmost) as a display pattern: bit 11 = top row,
// bit 0 = bottom row, matching displayPattern() in the sketch.
uint16_t imageColumn(const uint8_t image[POV_IMAGE_BYTES], uint8_t col);
