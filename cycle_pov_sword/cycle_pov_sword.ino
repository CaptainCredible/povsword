#include <avr/pgmspace.h>
#include "pov_protocol.h"
#include "rx_hw.h"
#include "image_store.h"

// ============================ Image receiving ================================
// The sword shows one image: the last one received over the optical link
// (12x12, or up to POV_MAX_COLS columns wide), or userImage below if none has
// been received yet.
//
// To upload, press Transmit on the sender page and hold the LDR (PB4) on the
// square while the sword is running (UPLOAD_WATCH), or hold the programming
// button (PB0 to GND) while switching on. A failed attempt keeps the old image.

// Watch the LDR while displaying for the sender's handshake, so no button is
// needed. Costs a little battery (ADC always on).
const bool UPLOAD_WATCH = true;

// Debug mode: boot straight into receive mode (as if PB0 were held) and show
// the raw LDR reading (0..1023) as a bar from the bottom LED up while waiting
// and receiving: 0 LEDs = 0, all 12 = 1023. The reading is LOW when lit, so
// the bar shrinks as light hits the LDR. A failed transfer just restarts the
// wait; the first good image is saved and shown.
//
// After every receive attempt (debug, button or watch):
//   success: all LEDs blink 3 times, then the new image is shown.
//   failure: one LED shows how far it got (1.5 s), then a bar shows how many
//            rows (wide image: columns, up to 12) arrived intact (1.5 s).
//            LED 1 = bottom:
//              1 handshake seen, long dark gap after it wrong
//              2 training burst unreadable   5 unreadable bit
//              3 leader missing              6 extra pulses after last row
//              4 row marker missing          7 row parity error
//            Nothing is shown if it never locked onto the handshake.
#define DEBUG false

// ============================ LED / hardware maps ============================
// 12 LEDs charlieplexed on PB0-PB3. Tables live in flash to save RAM.
// LED physical order along the sword: 1 2 7 8 11 12 3 4 9 10 5 6
const uint8_t ledOrder[12] PROGMEM = {0,1,6,7,10,11,2,3,8,9,4,5};

// LED mappings: {anode_pin, cathode_pin}
const uint8_t anode[12]   PROGMEM = {0,1,0,2,0,3,1,2,1,3,2,3};
const uint8_t cathode[12] PROGMEM = {1,0,2,0,3,0,2,1,3,1,3,2};

// ============================ POV bitmaps (in flash) ========================
// Each uint16_t is one column; bit 0 = bottom LED ... bit 11 = top LED.
// The sender page's "Copy as code" produces this format.

#define COLS(a) (sizeof(a) / sizeof(a[0]))

// #14 "smiley2" by Flaggermus, 2026-10-17
// 14 columns
/*
const uint16_t userImage[14] PROGMEM = {
  0x080,  // 0b000010000000
  0x0C0,  // 0b000011000000
  0x0A0,  // 0b000010100000
  0x090,  // 0b000010010000
  0xF8C,  // 0b111110001100
  0x482,  // 0b010010000010
  0x481,  // 0b010010000001
  0x482,  // 0b010010000010
  0xF8C,  // 0b111110001100
  0x090,  // 0b000010010000
  0x0A0,  // 0b000010100000
  0x0C0,  // 0b000011000000
  0x080,  // 0b000010000000
  0x000   // 0b000000000000
};
*/

// Shown until an image has been received. "smiley2", 14 columns
const uint16_t userImage[14] PROGMEM = {
  0x1C0,  // 0b000111000000
  0x3E0,  // 0b001111100000
  0x7F0,  // 0b011111110000
  0x7F8,  // 0b011111111000
  0x7FC,  // 0b011111111100
  0x3FE,  // 0b001111111110
  0x3FE,  // 0b001111111110
  0x7FC,  // 0b011111111100
  0x7F8,  // 0b011111111000
  0x7F0,  // 0b011111110000
  0x3E0,  // 0b001111100000
  0x1C0,  // 0b000111000000
  0x000,  // 0b000000000000
  0x000   // 0b000000000000
};

// Boot animation: a bar filling up the blade.
const uint16_t start[12] PROGMEM = {
  0x001, 0x003, 0x007, 0x00F, 0x01F, 0x03F,
  0x07F, 0x0FF, 0x1FF, 0x3FF, 0x7FF, 0xFFF
};

// Columns in the received image (read from EEPROM as they are shown), or 0 to
// show userImage instead.
uint8_t storedCols;

bool watching = false; // UPLOAD_WATCH is polling the LDR

// ================================= Setup ====================================
void setup() {
  // Programming button on PB0: must be read before the matrix is driven.
  bool received = false;
  if (DEBUG) {
    setAllOff();
    while (!received) {
      received = receiveImage(showLdrLevel);
      if (!received) showRxFailure();
    }
  } else if (bootButtonPressed()) {
    received = receiveImage();
    if (!received) showRxFailure();
  }
  if (received) showRxSuccess();

  setAllOff();
  storedCols = imageStored();

  if (UPLOAD_WATCH) {
    rxWatchBegin();
    watching = true;
  }

  scrollImage(start, COLS(start), 50);
}

// The LDR watch saw the sender's handshake mid-display: receive the rest
// with the light bar, then switch to the new image. A failed wide transfer has
// already overwritten the stored image, so re-check what's stored either way.
void uploadFromDisplay() {
  watching = false;
  setAllOff();
  if (receiveImage(showLdrLevel, true)) showRxSuccess();
  else                                  showRxFailure();
  storedCols = imageStored();
  rxWatchBegin();
  watching = true;
}

// ================================== Loop ====================================
void loop() {
  if (storedCols) scrollStoredImage(storedCols, 5);
  else            scrollImage(userImage, COLS(userImage), 5);
}

// ============================= Receive feedback =============================

// Called with every LDR reading while receiving (see DEBUG). Lights one LED of
// the bar per call, so the multiplexing doesn't slow the receiver down.
void showLdrLevel(uint16_t raw) {
  static uint8_t led = 0;
  uint8_t n = ((uint32_t)raw * 13) >> 10;   // 0..12 LEDs
  setLED(led, led < n);                     // LED 0 = bottom
  led = (led + 1) % 12;
}

// Receive result on the LEDs (codes listed at DEBUG).
void showRxFailure() {
  setAllOff();
  uint8_t code = receiveFailCode();
  if (!code) return;   // never locked: nothing to report
  displayPattern(1 << (code - 1), 1500);
  uint8_t done = receiveRowsDone();   // rows, or columns of a wide image
  displayPattern((1 << (done < 12 ? done : 12)) - 1, 1500);
  setAllOff();
}

void showRxSuccess() {
  for (uint8_t i = 0; i < 3; i++) {
    displayPattern(0xFFF, 200);
    setAllOff();
    delay(200);
  }
}

// =========================== Bitmap / low level =============================

// Scroll through a bitmap once (blocking). Reads columns from flash (PROGMEM).
void scrollImage(const uint16_t image[], uint8_t numCols, unsigned long frameDelay) {
  for (uint8_t col = 0; col < numCols; col++) {
    displayPattern(pgm_read_word(&image[col]), frameDelay);
  }
  setAllOff();
}

// Same as scrollImage(), for the received image: columns are read from EEPROM
// one at a time (any width, no RAM), followed by 2 blank columns as a gap.
void scrollStoredImage(uint8_t numCols, unsigned long frameDelay) {
  for (uint8_t col = 0; col < numCols; col++) {
    displayPattern(imageGet(col), frameDelay);
  }
  displayPattern(0, 2 * frameDelay);
  setAllOff();
}

// Display a 12-bit column (bit 0 = LED 0 ... bit 11 = LED 11) for `duration` ms,
// multiplexing rapidly so all lit LEDs appear on at once.
void displayPattern(uint16_t pattern, unsigned long duration) {
  unsigned long startTime = micros();

  while (micros() - startTime < (duration * 1000UL)) {
    for (uint8_t led = 0; led < 12; led++) {
      setLED(led, pattern & (1 << led));
      if (watching && rxWatchPoll()) uploadFromDisplay();
      delayMicroseconds(50);
    }
  }
}

// Set a single LED on or off.
void setLED(uint8_t led, bool on) {
  uint8_t remappedLED = pgm_read_byte(&ledOrder[led]);

  // All LEDs off: PB0-3 back to inputs. Preserve PB4-7.
  DDRB  &= 0b11110000;
  PORTB &= 0b11110000;

  if (!on) return;

  uint8_t a = pgm_read_byte(&anode[remappedLED]);
  uint8_t c = pgm_read_byte(&cathode[remappedLED]);

  DDRB  |= (1 << a);   // anode  -> OUTPUT HIGH
  PORTB |= (1 << a);
  DDRB  |= (1 << c);   // cathode -> OUTPUT LOW
  PORTB &= ~(1 << c);
}

// Turn all LEDs off (preserve PB4-7).
void setAllOff() {
  DDRB  &= 0b11110000;
  PORTB &= 0b11110000;
}
