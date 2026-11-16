#include <Arduino.h>
#include "rx_hw.h"
#include "rx_light.h"
#include "rx_decoder.h"

#define BUTTON_BIT PB0
#define LDR_BIT    PB4   // ADC2

// ADC clock must be 50-200 kHz: the LDR node is a 20-50k pull-up in series
// with the LDR, far above the ADC's preferred <10k source impedance.
#if   F_CPU >= 12800000UL
#define ADC_PRESCALE ((1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0))  // /128
#elif F_CPU >= 6400000UL
#define ADC_PRESCALE ((1 << ADPS2) | (1 << ADPS1))                 // /64
#elif F_CPU >= 3200000UL
#define ADC_PRESCALE ((1 << ADPS2) | (1 << ADPS0))                 // /32
#elif F_CPU >= 1600000UL
#define ADC_PRESCALE (1 << ADPS2)                                  // /16
#else
#define ADC_PRESCALE ((1 << ADPS1) | (1 << ADPS0))                 // /8
#endif

static RxLight light;
static RxDecoder dec;

bool bootButtonPressed() {
  // PB1-PB3 as inputs without pull-ups, so no LED path loads PB0.
  DDRB  &= 0b11110000;
  PORTB &= 0b11110000;
  PORTB |= (1 << BUTTON_BIT);           // PB0 INPUT_PULLUP

  delayMicroseconds(10);                // let the node charge
  bool a = !(PINB & (1 << BUTTON_BIT));
  delayMicroseconds(500);
  bool b = !(PINB & (1 << BUTTON_BIT));

  PORTB &= ~(1 << BUTTON_BIT);          // drop the pull-up
  return a && b;
}

static uint16_t readAdcOnce() {
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC)) {}
  return ADC;
}

void ldrBegin() {
  DDRB  &= ~(1 << LDR_BIT);
  PORTB |=  (1 << LDR_BIT);             // pull-up = top of the LDR divider
  DIDR0 |=  (1 << ADC2D);               // no digital input buffer on PB4
  ADMUX  = (1 << MUX1);                 // Vcc reference, ADC2 (PB4)
  ADCSRA = (1 << ADEN) | ADC_PRESCALE;
  readAdcOnce();                        // discard first conversion after mux
}

static void ldrEnd() {
  ADCSRA = 0;                           // ADC off: saves the coin cell
  DIDR0 &= ~(1 << ADC2D);               // PB4 readable as digital again
}

// Average of 4 conversions, 0..1023. LOW = lit.
uint16_t readLdr() {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 4; i++) sum += readAdcOnce();
  return sum >> 2;
}

bool isLit() {
  return lightUpdate(light, micros(), readLdr());
}

static void rxStart() {
  ldrBegin();
  uint32_t now = micros();
  lightInit(light, now);
  rxInit(dec, now);
}

bool receiveImage(uint8_t image[POV_IMAGE_BYTES], void (*onSample)(uint16_t raw), bool resume) {
  if (!resume) rxStart();

  RxResult r;
  do {
    uint16_t raw = readLdr();
    bool lit = lightUpdate(light, micros(), raw);
    if (onSample) onSample(raw);
    light.frozen = rxLocked(dec);       // calibration fixed from handshake on
    r = rxFeed(dec, micros(), lit);
  } while (r == RX_BUSY);

  ldrEnd();
  if (r != RX_DONE) return false;
  for (uint8_t i = 0; i < POV_IMAGE_BYTES; i++) image[i] = dec.image[i];
  return true;
}

uint8_t receiveFailCode() {
  if (dec.state == RXS_BITS && dec.bit > POV_N) return 7;   // failed on parity
  return dec.state;                                         // RxState 0..6
}

uint8_t receiveRowsDone() {
  return dec.row;
}

static uint16_t watchSum;
static uint8_t  watchCount;

void rxWatchBegin() {
  rxStart();
  watchSum = 0;
  watchCount = 0;
  ADCSRA |= (1 << ADSC);
}

bool rxWatchPoll() {
  if (ADCSRA & (1 << ADSC)) return false;   // conversion still running
  watchSum += ADC;
  ADCSRA |= (1 << ADSC);                    // start the next one
  if (++watchCount < 4) return false;

  uint16_t raw = watchSum >> 2;
  watchSum = 0;
  watchCount = 0;
  uint32_t now = micros();
  bool lit = lightUpdate(light, now, raw);
  light.frozen = rxLocked(dec);
  if (rxFeed(dec, now, lit) == RX_FAIL) rxInit(dec, now);   // keep watching
  return rxLocked(dec);
}
