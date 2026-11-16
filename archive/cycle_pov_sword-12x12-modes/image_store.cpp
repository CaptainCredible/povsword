#include <avr/eeprom.h>
#include "image_store.h"

#define EE_MAGIC ((uint8_t *)(EE_RX_BASE))
#define EE_DATA  ((uint8_t *)(EE_RX_BASE + 1))
#define EE_CRC   ((uint8_t *)(EE_RX_BASE + 1 + POV_IMAGE_BYTES))

// CRC-8, polynomial 0x07.
static uint8_t crc8(const uint8_t *p, uint8_t n) {
  uint8_t crc = 0;
  while (n--) {
    crc ^= *p++;
    for (uint8_t i = 0; i < 8; i++) crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1;
  }
  return crc;
}

bool imageSave(const uint8_t image[POV_IMAGE_BYTES]) {
  uint8_t crc = crc8(image, POV_IMAGE_BYTES);

  // update_* only rewrites bytes that changed, to spare EEPROM wear.
  eeprom_update_byte(EE_MAGIC, 0x00);
  eeprom_update_block(image, EE_DATA, POV_IMAGE_BYTES);
  eeprom_update_byte(EE_CRC, crc);

  uint8_t check[POV_IMAGE_BYTES];
  eeprom_read_block(check, EE_DATA, POV_IMAGE_BYTES);
  if (crc8(check, POV_IMAGE_BYTES) != crc || eeprom_read_byte(EE_CRC) != crc) return false;

  eeprom_update_byte(EE_MAGIC, EE_RX_MAGIC);   // last: commits the image
  return true;
}

bool imageLoad(uint8_t image[POV_IMAGE_BYTES]) {
  if (eeprom_read_byte(EE_MAGIC) != EE_RX_MAGIC) return false;
  eeprom_read_block(image, EE_DATA, POV_IMAGE_BYTES);
  return crc8(image, POV_IMAGE_BYTES) == eeprom_read_byte(EE_CRC);
}

uint16_t imageColumn(const uint8_t image[POV_IMAGE_BYTES], uint8_t col) {
  uint16_t pattern = 0;
  for (uint8_t row = 0; row < POV_N; row++) {
    uint8_t k = row * POV_N + col;
    if (image[k >> 3] & (0x80 >> (k & 7))) pattern |= 1 << (POV_N - 1 - row);
  }
  return pattern;
}
