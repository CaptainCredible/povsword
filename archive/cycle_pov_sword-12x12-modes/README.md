# cycle_pov_sword

ATtiny85 POV sword (12 charlieplexed LEDs on PB0–PB3) that can download a
12×12 image over an optical link from `pov-sword-sender.html`.

## Files

| File | What |
|------|------|
| `cycle_pov_sword.ino` | Sketch: modes, bitmaps, charlieplex display. Mode 1 shows the received image. |
| `pov_protocol.h` | **All** wire-format / timing / EEPROM constants. Keep in sync with the sender. |
| `rx_decoder.*` | Frame decoder state machine. Pure logic, fed `(micros, lit)` samples. |
| `rx_light.*` | LDR level → lit/dark: sliding min/max calibration + hysteresis. Pure logic. |
| `rx_hw.*` | ATtiny side: PB0 boot button, ADC on PB4, receive loop, `isLit()`. |
| `image_store.*` | EEPROM save/load (magic written last, CRC-8) and image → display columns. |
| `test/host/test_rx.cpp` | Host test: replays the sender's schedule through the decoder. |

## Building

- **Arduino IDE**: open the folder; board *ATTinyCore → ATtiny25/45/85 (No bootloader)*,
  chip ATtiny85, clock *8 MHz (internal)*, *EEPROM retained*. Burn bootloader once to set fuses.
- **PlatformIO**: `pio run`, `pio run -t fuses`, `pio run -t upload` (see `platformio.ini`).
- **Host test**, from this folder:

  ```sh
  c++ -std=c++11 -DPOV_HOST_TEST -I. test/host/test_rx.cpp rx_decoder.cpp rx_light.cpp -o test_rx && ./test_rx
  ```

## Receiving an image

1. Switch the sword on. While it displays, it watches the LDR for the sender's handshake
   (`UPLOAD_WATCH`). Alternatively hold the button (PB0 → GND) while switching on: that
   waits for a transfer straight away, for 15 s.
2. Press **Transmit** on the sender page and hold the LDR (PB4) flat against the square within
   ~3 s. When the sword locks on, it switches to the light bar for the rest of the transfer.
3. On success all LEDs blink 3 times, the image is saved and the sword starts showing it
   (mode 1, also on later boots). On any error or timeout the stored image is left untouched
   and the sword boots normally. A failure first shows an error code on one LED, then a bar
   with the number of rows that arrived intact (codes listed at `DEBUG` in the sketch).

If it fails at the default Unit (3 frames), try a larger Unit: a slow LDR doesn't recover between
short pulses. In simulation, an LDR with ~30 ms dark recovery works at every Unit, ~50 ms needs Unit ≥ 4.

## Wire format

Source of truth: the `WIRE FORMAT` comment and `buildSchedule()` in `pov-sword-sender.html`.

Channel: WHITE = light pulse (**MARK**), BLACK = gap. A symbol = MARK (1 unit white) + GAP
(N units black). The receiver times **onset-to-onset** (dark→lit, the LDR's fast edge).

| GAP | onset interval | meaning |
|-----|---------------|---------|
| 1u  | 2u | bit 0 |
| 2u  | 3u | bit 1 |
| 3u  | 4u | ROW marker |
| 5u  | 6u | LEADER / END marker |

Decision thresholds 2.5u / 3.5u / 5u; < 1.5u or ≥ 8u is a desync. `u` is measured per transfer.

Sequence (frames at ~60 fps):

1. Dark hold, 30 frames.
2. Handshake: 6 × (15 white + 15 black), so onsets ~500 ms apart. The receiver locks after 3
   consecutive matching intervals (300–1200 ms, within ±25 % of each other) and freezes the
   LDR threshold (midpoint of the min/max seen, ± swing/8 hysteresis).
3. Boundary: 30 more dark frames. The onset interval is 1.5–3 handshake periods (nominally 2).
4. Training `0 1 0 1 0 1 0 1`: the 8 intervals are clustered short/long, must match the pattern,
   and `u = sum / 20`.
5. LEADER (6u).
6. 12 rows: ROW marker (4u), 12 data bits left→right, 1 parity bit.
   **Parity is odd** over the 13 bits. The sender comment says "even parity", but its code
   (`ones & 1 ? 0 : 1`) makes the total odd; the receiver matches the code (`ROW_PARITY`).
7. END marker (6u). The sender only sends dark after it, so its interval never closes:
   the receiver takes ≥ 5u of silence after row 12's parity bit as END.
8. Dark hold.

Image is row-major, top→bottom, left→right; filled pixel = 1. There is no frame checksum on the
wire; integrity comes from per-row parity plus strict symbol counting.

## EEPROM

| Address | Use |
|---------|-----|
| 0 | next image index (mode 4) |
| 1 | current mode |
| 2 | START_MODE last flashed |
| 3–4 | boot counter |
| 8 | magic `0xA5` (written last) |
| 9–26 | packed image, 18 bytes, MSB-first, pixel k = row·12 + col |
| 27 | CRC-8 (poly 0x07) of the image |

Save order: clear magic → image → CRC → read back and verify → magic. The display only
uses the image if both the magic and the CRC match.
