#include <avr/eeprom.h>
#include "image_store.h"

#define EE_MAGIC ((uint8_t *)(EE_RX_BASE))
#define EE_COUNT ((uint8_t *)(EE_RX_BASE + 1))
#define EE_COL(i) ((uint16_t *)(EE_RX_DATA + 2 * (uint16_t)(i)))
#define EE_CRC(n) ((uint8_t *)(EE_RX_DATA + 2 * (uint16_t)(n)))

static uint8_t crc;   // running CRC of the columns written since imageBegin()

// CRC-8, polynomial 0x07, one byte at a time.
static uint8_t crc8(uint8_t c, uint8_t b) {
  c ^= b;
  for (uint8_t i = 0; i < 8; i++) c = (c & 0x80) ? (c << 1) ^ 0x07 : c << 1;
  return c;
}

void imageBegin() {
  eeprom_update_byte(EE_MAGIC, 0x00);
  crc = 0;
}

void imagePut(uint8_t col, uint16_t pattern) {
  // update_* only rewrites bytes that changed, to spare EEPROM wear.
  eeprom_update_word(EE_COL(col), pattern);
  crc = crc8(crc8(crc, pattern & 0xFF), pattern >> 8);
}

// CRC over the stored columns (low byte first) followed by the count.
static uint8_t storedCrc(uint8_t count) {
  uint8_t c = 0;
  for (uint8_t i = 0; i < count; i++) {
    uint16_t w = eeprom_read_word(EE_COL(i));
    c = crc8(crc8(c, w & 0xFF), w >> 8);
  }
  return crc8(c, count);
}

bool imageCommit(uint8_t count) {
  if (count < 1 || count > POV_MAX_COLS) return false;
  uint8_t want = crc8(crc, count);
  eeprom_update_byte(EE_COUNT, count);
  eeprom_update_byte(EE_CRC(count), want);
  if (storedCrc(count) != want || eeprom_read_byte(EE_CRC(count)) != want) return false;
  eeprom_update_byte(EE_MAGIC, EE_RX_MAGIC);   // last: commits the image
  return true;
}

uint8_t imageStored() {
  if (eeprom_read_byte(EE_MAGIC) != EE_RX_MAGIC) return 0;
  uint8_t count = eeprom_read_byte(EE_COUNT);
  if (count < 1 || count > POV_MAX_COLS) return 0;
  return storedCrc(count) == eeprom_read_byte(EE_CRC(count)) ? count : 0;
}

uint16_t imageGet(uint8_t col) {
  return eeprom_read_word(EE_COL(col));
}

uint16_t squareColumn(const uint8_t rows[POV_IMAGE_BYTES], uint8_t col) {
  uint16_t pattern = 0;
  for (uint8_t row = 0; row < POV_N; row++) {
    uint8_t k = row * POV_N + col;
    if (rows[k >> 3] & (0x80 >> (k & 7))) pattern |= 1 << (POV_N - 1 - row);
  }
  return pattern;
}
