// Optical image link: every constant shared with the sender lives here.
// The sender (pov-sword-sender.html) is the canonical spec; keep this in sync
// with its WIRE FORMAT comment and buildSchedule(). See README.md.
#pragma once
#include <stdint.h>

// ================================== Clock ===================================
// 8 MHz internal RC (lfuse 0xE2). F_CPU normally comes from the build
// (ATTinyCore menu / platformio.ini); it MUST match the fuses, because
// millis()/micros()/delayMicroseconds() are all derived from it. Nothing in
// the decoder assumes a clock or frame rate: the handshake period and the
// unit u are measured per transfer, so a few % of RC-oscillator error is fine.
#ifndef F_CPU
#define F_CPU 8000000UL
#endif
#if !defined(POV_HOST_TEST) && F_CPU != 8000000UL
#warning "F_CPU is not 8 MHz - make sure it matches the fuses"
#endif

// ================================ Image =====================================
// 12x12 bitmap, row-major, top->bottom, left->right, filled pixel = 1.
// Packed MSB-first: pixel k = row*12 + col lives in byte k/8, bit 7 - k%8.
#define POV_N           12
#define POV_IMAGE_BYTES ((POV_N * POV_N + 7) / 8)   // 18

// ============================== Wire format =================================
// Symbol = MARK (1u white) + GAP (N u black). Measured onset-to-onset:
//   bit 0 = 2u, bit 1 = 3u, ROW = 4u, LEADER/END = 6u.
// Decision thresholds, in half-units (so integer math stays exact):
#define SYM_MIN_HALF_U     3   // < 1.5u           -> glitch / desync
#define SYM_BIT1_HALF_U    5   // >= 2.5u          -> bit 1 (else bit 0)
#define SYM_ROW_HALF_U     7   // >= 3.5u          -> ROW marker
#define SYM_LEADER_HALF_U 10   // >= 5u            -> LEADER / END
#define SYM_MAX_HALF_U    16   // >= 8u            -> desync / lost signal

// Training burst sent before the leader: 0 1 0 1 0 1 0 1.
#define TRAIN_BITS    8
#define TRAIN_PATTERN 0x55     // 0b01010101, MSB = first training bit
// Sum of the training intervals in units: 4 x 2u + 4 x 3u.
#define TRAIN_UNITS   20

// Row parity over the 13 symbols (12 data + parity). The sender's comment says
// "even parity", but its code, pushBit(ones & 1 ? 0 : 1), makes the total
// number of ones ODD. The receiver follows what is actually transmitted.
#define ROW_PARITY 1           // 1 = odd (matches sender code), 0 = even

// Plausible unit range. Sender: 2..6 frames per unit at ~60 fps = 33..100 ms.
#define UNIT_MIN_US    8000UL
#define UNIT_MAX_US  250000UL

// ================================ Handshake =================================
// Sender: 6 (original page) or 10 (web app) cycles of 15 frames white / 15
// frames black (~500 ms onset-to-onset), then 30 frames of extra dark, so the
// boundary interval is ~2x the period. Any count from 4 to ~16 works.
#define HS_PERIOD_MIN_US   300000UL  // accepted handshake onset spacing
#define HS_PERIOD_MAX_US  1200000UL  // (sender dropping to 30 fps = 1 s)
#define HS_LOCK_COUNT      3         // consecutive matching intervals to lock
#define HS_MAX_INTERVALS  16         // a 2 Hz blink longer than this isn't a sender
// Boundary = onset interval between 1.5 and 3 handshake periods (nominal 2).

// =============================== Receiver ===================================
#define RX_LOCK_TIMEOUT_US 15000000UL  // no handshake lock -> give up (15 s)

// LDR calibration: sliding min/max over LIGHT_BLOCKS blocks of LIGHT_BLOCK_US.
#define LIGHT_BLOCK_US   250000UL
#define LIGHT_BLOCKS     4
#define LIGHT_MIN_SWING  48     // ADC counts (of 1023) needed to call it a signal
#define LIGHT_HYST_SHIFT 3      // hysteresis = +/- swing/8 around the midpoint

// ================================ EEPROM ====================================
// [base+0] magic, [base+1..18] packed image, [base+19] CRC-8 of the image.
// Bytes 0..4 hold the sketch's mode/boot counters, so the image starts at 8.
#define EE_RX_BASE   8
#define EE_RX_MAGIC  0xA5
