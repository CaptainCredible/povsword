#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include "pov_protocol.h"
#include "rx_hw.h"
#include "image_store.h"

// ============================ Image receiving ================================
// Hold the programming button (PB0 to GND) while switching on to enter receive
// mode, then hold the LDR (PB4) against the sender page and press Transmit.
// A good frame is saved to EEPROM and shown in mode 1; a failed or timed-out
// attempt leaves the stored image alone and boots normally.

// ================================ Settings ==================================
// Mode to run after flashing:
//   1 = received image (falls back to userImage below if none is stored)
//   2 = random smiley / frownie (picked once per boot)
//   3 = random "JA" / "NEI"     (picked once per boot)
//   4 = cycle all images, one per boot
//   5 = only smiley
//   6 = only paper string men
// Shorting MODE_PIN to GND while running advances the mode and lights its LED
// (mode 1 = bottom LED, mode 2 = next one up, ...) until the next power cycle,
// which then runs the new mode. Changing START_MODE and reflashing overrides
// whatever mode was last selected with the pin.
const uint8_t START_MODE = 1;
const uint8_t NUM_MODES  = 6;

#define MODE_PIN PB4   // the free pin; internal pull-up, short to GND to toggle

// PB4 is also the LDR input. With the LDR fitted, light on it pulls the pin
// low just like a short, so polling would change mode whenever the blade
// passes a lamp. Only enable this on a board without the LDR.
const bool MODE_PIN_ENABLED = false;

// Watch the LDR while displaying: pressing Transmit in the sender page with
// the LDR on the square starts an upload, no button needed. Costs a little
// battery (ADC always on). Shares PB4 with the mode pin, so not both.
const bool UPLOAD_WATCH = true;
static_assert(!(UPLOAD_WATCH && MODE_PIN_ENABLED), "UPLOAD_WATCH uses PB4, disable MODE_PIN_ENABLED");

// Debug mode: boot straight into receive mode (as if PB0 were held) and show
// the raw LDR reading (0..1023) as a bar from the bottom LED up while waiting
// and receiving: 0 LEDs = 0, all 12 = 1023. The reading is LOW when lit, so
// the bar shrinks as light hits the LDR. A failed transfer just restarts the
// wait; the first good image is saved and shown.
//
// After every receive attempt (debug or button):
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
// 12 LEDs charlieplexed on PB0-PB3.
// LED physical order along the sword: 1 2 7 8 11 12 3 4 9 10 5 6
byte ledOrder[12] = {0,1,6,7,10,11,2,3,8,9,4,5};

// LED mappings: {anode_pin, cathode_pin}
static const uint8_t anode[12]   = {0,1,0,2,0,3,1,2,1,3,2,3};
static const uint8_t cathode[12] = {1,0,2,0,3,0,2,1,3,1,3,2};

// ============================ POV bitmaps (in flash) ========================
// Each uint16_t is one column; bit 0 = LED 0 ... bit 11 = LED 11.
// Held in PROGMEM so they live in flash, not the ATtiny85's 512 bytes of SRAM.

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




// "smiley2", 14 columns
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

// scrollImage(userImage, 14, 5);


// scrollImage(userImage, 21, 5);


// scrollImage(userImage, 14, 5);


// scrollImage(userImage, 14, 5);


// scrollImage(userImage, 14, 5);


// scrollImage(userImage, 17, 5);


// scrollImage(userImage, 21, 5);


// scrollImage(userImage, 14, 5);


// scrollImage(userImage, 22, 5);


// scrollImage(userImage, 14, 5);


// scrollImage(userImage, 14, 5);


// scrollImage(userImage, 17, 5);





// 14 columns
const uint16_t smiley2[14] PROGMEM = {
  0x3FC,  // 0b001111111100
  0x606,  // 0b011000000110
  0xC13,  // 0b110000010011
  0x9C9,  // 0b100111001001
  0x809,  // 0b100000001001
  0x809,  // 0b100000001001
  0x9C9,  // 0b100111001001
  0xC13,  // 0b110000010011
  0x606,  // 0b011000000110
  0x3FC,  // 0b001111111100
  0x000,  // 0b000000000000
  0x000,  // 0b000000000000
  0x000,  // 0b000000000000
  0x000   // 0b000000000000
};

// 14 columns
const uint16_t frownie2[14] PROGMEM = {
  0x3FC,  // 0b001111111100
  0x606,  // 0b011000000110
  0xC0B,  // 0b110000001011
  0x9D1,  // 0b100111010001
  0x811,  // 0b100000010001
  0x811,  // 0b100000010001
  0x9D1,  // 0b100111010001
  0xC0B,  // 0b110000001011
  0x606,  // 0b011000000110
  0x3FC,  // 0b001111111100
  0x000,  // 0b000000000000
  0x000,  // 0b000000000000
  0x000,  // 0b000000000000
  0x000   // 0b000000000000
};

// 20 columns
const uint16_t hei[20] PROGMEM = {
  0x000,  // 0b000000000000
  0x7FE,  // 0b011111111110
  0x020,  // 0b000000100000
  0x020,  // 0b000000100000
  0x7FE,  // 0b011111111110
  0x000,  // 0b000000000000
  0x7FE,  // 0b011111111110
  0x422,  // 0b010000100010
  0x422,  // 0b010000100010
  0x402,  // 0b010000000010
  0x000,  // 0b000000000000
  0x402,  // 0b010000000010
  0x7FE,  // 0b011111111110
  0x402,  // 0b010000000010
  0x000,  // 0b000000000000
  0x000,  // 0b000000000000
  0x7F6,  // 0b011111110110
  0x7F6,  // 0b011111110110
  0x000,  // 0b000000000000
  0x000   // 0b000000000000
};

// 15 columns
const uint16_t deichmanLogo[15] PROGMEM = {
  0x000,  // 0b000000000000
  0x7FE,  // 0b011111111110
  0x402,  // 0b010000000010
  0x402,  // 0b010000000010
  0x606,  // 0b011000000110
  0x30C,  // 0b001100001100
  0x198,  // 0b000110011000
  0x060,  // 0b000001100000
  0x000,  // 0b000000000000
  0x168,  // 0b000101101000
  0x36C,  // 0b001101101100
  0x666,  // 0b011001100110
  0x606,  // 0b011000000110
  0x402,  // 0b010000000010
  0x000   // 0b000000000000
};

// 11 columns
const uint16_t paperStringMen[11] PROGMEM = {
  0x063,  // 0b000001100011
  0x063,  // 0b000001100011
  0x0C6,  // 0b000011000110
  0x6CC,  // 0b011011001100
  0xBF8,  // 0b101111111000
  0xFF8,  // 0b111111111000
  0xBF8,  // 0b101111111000
  0x6CC,  // 0b011011001100
  0x0C6,  // 0b000011000110
  0x063,  // 0b000001100011
  0x063   // 0b000001100011
};

// Boot animation: a bar filling up the blade.
const uint16_t start[12] PROGMEM = {
  0x001, 0x003, 0x007, 0x00F, 0x01F, 0x03F,
  0x07F, 0x0FF, 0x1FF, 0x3FF, 0x7FF, 0xFFF
};

// ============================ Image playlist ================================
// One image is chosen per boot, cycling through this list in order.
// Add, remove or reorder entries freely; column counts are computed for you.
struct Image {
  const uint16_t *data;   // points into PROGMEM
  uint8_t cols;
};

#define IMG(a) { a, sizeof(a) / sizeof(a[0]) }
#define COLS(a) (sizeof(a) / sizeof(a[0]))

const Image images[] = {
  IMG(smiley2),
  IMG(frownie2),
  IMG(hei),
  IMG(deichmanLogo),
  IMG(paperStringMen),
};
const uint8_t NUM_IMAGES = COLS(images);

// EEPROM layout
#define EE_IMAGE_ADDR ((uint8_t *)0)    // next image index for mode 4
#define EE_MODE_ADDR  ((uint8_t *)1)    // current mode (1..NUM_MODES)
#define EE_START_ADDR ((uint8_t *)2)    // START_MODE this chip was last flashed with
#define EE_BOOT_ADDR  ((uint16_t *)3)   // boot counter, seeds random() (bytes 3-4)

uint8_t mode;

// What this boot shows (chosen in setup)
const uint16_t *imgData;
uint8_t imgCols;
const char *message;   // non-NULL in mode 2
bool watching = false; // UPLOAD_WATCH is polling the LDR
bool imgStored;        // show the received image, read from EEPROM (imgCols columns)

bool showModeLed = false;   // set once the mode pin has been shorted
bool pinWasDown;
unsigned long pinChangedAt;

// ================================= Font =====================================
// All glyphs are 14 columns wide. Each uint16_t is one column (MSB = top row,
// LSB = bottom row). Index 0 = smiley, 1..26 = 'A'..'Z'.
const uint16_t letters[27][14] PROGMEM = {
  // 0: smiley
  {
    0x3FC, 0x606, 0xC13, 0x9C9, 0x809, 0x809, 0x9C9,
    0xC13, 0x606, 0x3FC, 0x000, 0x000, 0x000, 0x000
  },
  // 1: A
  {
    0x1FF, 0x720, 0xC20, 0x820, 0x820, 0x820, 0xC20,
    0x720, 0x1FF, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 2: B
  {
    0xFFF, 0x881, 0x881, 0x881, 0x881, 0xDC1, 0x763,
    0x03E, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 3: C
  {
    0x3FC, 0x606, 0xC03, 0x801, 0x801, 0x801, 0xC03,
    0x606, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 4: D
  {
    0xFFF, 0x801, 0x801, 0x801, 0x801, 0x801, 0xC03,
    0x606, 0x3FC, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 5: E
  {
    0xFFF, 0x821, 0x821, 0x821, 0x821, 0x821, 0x801,
    0x801, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 6: F
  {
    0xFFF, 0x820, 0x820, 0x820, 0x820, 0x820, 0x800,
    0x800, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 7: G
  {
    0x3FC, 0x606, 0xC03, 0x801, 0x801, 0x821, 0x821,
    0xC23, 0x636, 0x21C, 0x000, 0x000, 0x000, 0x000
  },
  // 8: H
  {
    0xFFF, 0x080, 0x080, 0x080, 0x080, 0x080, 0x080,
    0xFFF, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 9: I
  {
    0x801, 0x801, 0x801, 0xFFF, 0x801, 0x801, 0x801,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 10: J
  {
    0x01E, 0x003, 0x001, 0x001, 0x001, 0x001, 0x803,
    0xFFE, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 11: K
  {
    0xFFF, 0x0C0, 0x0C0, 0x120, 0x210, 0x408, 0x804,
    0x802, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 12: L
  {
    0xFFF, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 13: M
  {
    0xFFF, 0x600, 0x180, 0x060, 0x060, 0x180, 0x600,
    0x800, 0xFFF, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 14: N
  {
    0xFFF, 0x600, 0x300, 0x180, 0x0C0, 0x060, 0x030,
    0xFFF, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 15: O
  {
    0x3FC, 0x606, 0xC03, 0x801, 0x801, 0x801, 0x801,
    0xC03, 0x606, 0x3FC, 0x000, 0x000, 0x000, 0x000
  },
  // 16: P
  {
    0xFFF, 0x900, 0x900, 0x900, 0x900, 0x900, 0x600,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 17: Q
  {
    0x3FC, 0x606, 0xC03, 0x801, 0x801, 0x819, 0x80F,
    0xC06, 0x607, 0x3FD, 0x000, 0x000, 0x000, 0x000
  },
  // 18: R
  {
    0xFFF, 0x900, 0x900, 0x900, 0x900, 0x980, 0x660,
    0x01E, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 19: S
  {
    0x784, 0xCC6, 0x843, 0x841, 0x841, 0x841, 0xC63,
    0x63E, 0x018, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 20: T
  {
    0x800, 0x800, 0x800, 0x800, 0xFFF, 0x800, 0x800,
    0x800, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 21: U
  {
    0xFFE, 0x003, 0x001, 0x001, 0x001, 0x001, 0x003,
    0xFFE, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 22: V
  {
    0xFC0, 0x030, 0x00C, 0x002, 0x002, 0x00C, 0x030,
    0xFC0, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 23: W
  {
    0xFFE, 0x003, 0x006, 0x018, 0x060, 0x060, 0x018,
    0x006, 0x003, 0xFFE, 0x000, 0x000, 0x000, 0x000
  },
  // 24: X
  {
    0xC03, 0x606, 0x30C, 0x198, 0x0F0, 0x198, 0x30C,
    0x606, 0xC03, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 25: Y
  {
    0xC00, 0x600, 0x300, 0x180, 0x0FF, 0x180, 0x300,
    0x600, 0xC00, 0x000, 0x000, 0x000, 0x000, 0x000
  },
  // 26: Z
  {
    0xC03, 0xC07, 0x80F, 0x81B, 0x833, 0x863, 0x8C3,
    0x983, 0xB03, 0x000, 0x000, 0x000, 0x000, 0x000
  }
};

// ================================= Setup ====================================
void setup() {
  // Programming button on PB0: must be read before the matrix is driven.
  bool received = false;
  if (DEBUG) {
    DDRB  &= 0b11110000;
    PORTB &= 0b11110000;
    while (!received) {
      received = receiveImage(showLdrLevel);
      if (!received) showRxFailure();
    }
  } else if (bootButtonPressed()) {
    received = receiveImage();
    if (!received) showRxFailure();
  }
  if (received) showRxSuccess();

  // PB0-PB3 drive the LEDs; start them all as inputs (off).
  DDRB  &= 0b11110000;
  PORTB &= 0b11110000;

  // Mode pin: input, with internal pull-up only if we poll it (otherwise the
  // pull-up just burns current through a lit LDR).
  DDRB  &= ~(1 << MODE_PIN);
  if (MODE_PIN_ENABLED) PORTB |= (1 << MODE_PIN);
  else                  PORTB &= ~(1 << MODE_PIN);

  // Current mode. If START_MODE changed since the last flash (or the EEPROM is
  // blank / out of range), start over from START_MODE.
  mode = eeprom_read_byte(EE_MODE_ADDR);
  bool reflashed = eeprom_read_byte(EE_START_ADDR) != START_MODE;
  if (reflashed || mode < 1 || mode > NUM_MODES) mode = START_MODE;
  if (received) mode = 1;   // show the new image now and on later boots

  // Seed random() from a boot counter, scrambled so consecutive boots don't
  // give correlated picks.
  uint16_t boots = eeprom_read_word(EE_BOOT_ADDR);
  randomSeed(mixSeed(boots + 1UL));

  // Pick this boot's image. A fresh/erased chip reads 0xFF, so wrap to 0.
  uint8_t cycleImage = eeprom_read_byte(EE_IMAGE_ADDR);
  if (cycleImage >= NUM_IMAGES) cycleImage = 0;

  message = NULL;
  imgStored = false;
  switch (mode) {
    case 1:
      if (!useReceivedImage()) { imgData = userImage; imgCols = COLS(userImage); }
      break;
    case 2:
      if (random(2)) { imgData = smiley2;  imgCols = COLS(smiley2); }
      else           { imgData = frownie2; imgCols = COLS(frownie2); }
      break;
    case 3:
      message = random(2) ? "JA" : "NEI";
      break;
    case 4:
      imgData = images[cycleImage].data;
      imgCols = images[cycleImage].cols;
      break;
    case 5:
      imgData = smiley2; imgCols = COLS(smiley2);
      break;
    case 6:
      imgData = paperStringMen; imgCols = COLS(paperStringMen);
      break;
  }

  // The power button bounces, and each bounce can be a full reset.
  // Only commit EEPROM changes once we've stayed powered for 100 ms.
  delay(100);
  eeprom_update_byte(EE_MODE_ADDR, mode);
  eeprom_update_byte(EE_START_ADDR, START_MODE);
  eeprom_update_word(EE_BOOT_ADDR, boots + 1);
  if (mode == 4) eeprom_update_byte(EE_IMAGE_ADDR, (cycleImage + 1) % NUM_IMAGES);

  // Pull-up has settled by now; a pin already held low at boot isn't a press.
  pinWasDown = !(PINB & (1 << MODE_PIN));
  pinChangedAt = millis();

  if (UPLOAD_WATCH) {
    rxWatchBegin();
    watching = true;
  }

  scrollImage(start, COLS(start), 50);
}

// Show the received image from EEPROM, if there is a valid one.
bool useReceivedImage() {
  uint8_t n = imageStored();
  if (!n) return false;
  imgCols = n; imgStored = true;
  message = NULL;
  return true;
}

// The LDR watch saw the sender's handshake mid-display: receive the rest
// with the light bar, then switch to the new image (or carry on as before).
void uploadFromDisplay() {
  watching = false;
  setAllOff();
  if (receiveImage(showLdrLevel, true)) {
    showRxSuccess();
    mode = 1;
    eeprom_update_byte(EE_MODE_ADDR, mode);
    showModeLed = false;
    useReceivedImage();
  } else {
    showRxFailure();
    // A failed wide transfer has already overwritten the stored image.
    if (imgStored && !useReceivedImage()) {
      imgStored = false;
      imgData = userImage; imgCols = COLS(userImage);
    }
  }
  rxWatchBegin();
  watching = true;
}

// ================================== Loop ====================================
void loop() {
  if (showModeLed) {
    displayPattern(1 << (mode - 1), 5);   // bit 0 = bottom LED
  } else if (message) {
    spellOut(message, 5);
  } else if (imgStored) {
    scrollStoredImage(imgCols, 5);
  } else {
    scrollImage(imgData, imgCols, 5);
  }
}

// ============================= LDR debug bar ================================

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

// ================================ Mode pin ==================================

// Poll the mode pin (debounced). Each short to GND advances the mode, saves it,
// and switches the display to the mode's LED until the next power cycle.
void checkModePin() {
  bool down = !(PINB & (1 << MODE_PIN));
  if (down == pinWasDown || millis() - pinChangedAt < 50) return;

  pinWasDown = down;
  pinChangedAt = millis();
  if (!down) return;

  mode = mode % NUM_MODES + 1;
  eeprom_update_byte(EE_MODE_ADDR, mode);
  showModeLed = true;
}

// 32-bit integer hash (murmur3 finalizer). Nonzero in -> nonzero out.
uint32_t mixSeed(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85EBCA6BUL;
  h ^= h >> 13;
  h *= 0xC2B2AE35UL;
  h ^= h >> 16;
  return h;
}

// ================================ Text API ==================================

// Spell out a string, one glyph after another.
// frameDelay = milliseconds each column is held (same units as scrollImage).
void spellOut(const char *text, unsigned long frameDelay) {
  for (const char *p = text; *p; p++) {
    displayGlyph(charToIndex(*p), frameDelay);
  }
  setAllOff();
}

// Map an ASCII character to its glyph index in letters[].
// A-Z (and a-z) -> 1..26; anything else (incl. space) -> -1 (blank cell).
int8_t charToIndex(char c) {
  if (c >= 'a' && c <= 'z') c -= 32;        // fold to uppercase
  if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
  return -1;
}

// Display one 14-column glyph read from PROGMEM.
// index < 0 shows a blank cell (used for spaces / unsupported chars).
void displayGlyph(int8_t index, unsigned long frameDelay) {
  for (uint8_t col = 0; col < 14; col++) {
    uint16_t pattern = (index >= 0) ? pgm_read_word(&letters[index][col]) : 0;
    displayPattern(pattern, frameDelay);
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
    if (MODE_PIN_ENABLED) checkModePin();
    for (uint8_t led = 0; led < 12; led++) {
      setLED(led, pattern & (1 << led));
      if (watching && rxWatchPoll()) uploadFromDisplay();
      delayMicroseconds(50);
    }
  }
}

// Set a single LED on or off.
void setLED(uint8_t led, bool on) {
  uint8_t remappedLED = ledOrder[led];

  // All LEDs off: PB0-3 back to inputs. Preserve PB4-7.
  DDRB  &= 0b11110000;
  PORTB &= 0b11110000;

  if (!on) return;

  uint8_t a = anode[remappedLED];
  uint8_t c = cathode[remappedLED];

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
