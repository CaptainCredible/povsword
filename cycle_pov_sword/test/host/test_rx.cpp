// Host test for the optical decoder. Synthesises the sender's light schedule
// (a port of buildSchedule() in pov-sword-sender.html), turns it into a
// sampled (timestamp, lit) stream and checks it decodes back to the image.
//
//   c++ -std=c++11 -DPOV_HOST_TEST -I. test/host/test_rx.cpp rx_decoder.cpp rx_light.cpp -o test_rx && ./test_rx
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include "rx_decoder.h"
#include "rx_light.h"

typedef uint8_t Grid[POV_N][POV_N];

static int hsCycles = 6;     // handshake cycles the sender sends

// ---------------------------------------------------------------- sender ----
struct Run { int v, n; };

// Mirrors buildSchedule(). flipBit >= 0 corrupts that payload bit (no parity
// fix-up); stopAfterRow >= 0 truncates the frame.
static std::vector<Run> buildSchedule(const Grid g, int U, int flipBit = -1, int stopAfterRow = -1) {
  const int HS_HALF = 15, HS_CYCLES = hsCycles, BOUNDARY = 30, LEAD_HOLD = 30;
  std::vector<Run> s;
  auto run = [&](int v, int n) { if (n > 0) s.push_back({v, n}); };
  auto sym = [&](int gap) { run(1, U); run(0, gap * U); };
  auto bit = [&](int b) { sym(b ? 2 : 1); };

  run(0, LEAD_HOLD);
  for (int c = 0; c < HS_CYCLES; c++) { run(1, HS_HALF); run(0, HS_HALF); }
  run(0, BOUNDARY);
  for (int b : {0, 1, 0, 1, 0, 1, 0, 1}) bit(b);
  sym(5);
  int k = 0;
  for (int r = 0; r < POV_N; r++) {
    sym(3);
    int ones = 0;
    for (int c = 0; c < POV_N; c++, k++) {
      int b = g[r][c] ? 1 : 0;
      ones += b;
      bit(k == flipBit ? !b : b);
    }
    bit(ones & 1 ? 0 : 1);
    if (r == stopAfterRow) { run(0, LEAD_HOLD); return s; }
  }
  sym(5);
  run(0, LEAD_HOLD);
  return s;
}

// Wide frame (any number of columns): LEADER, LEADER, count, then per column
// ROW + 12 bits (top pixel first) + parity. Mirrors buildSchedule() in the
// web editor (web/index.html). flipBit corrupts that payload bit (header bits
// count too); stopAfterCol truncates; badCount sends this count instead.
static std::vector<Run> buildWide(const std::vector<uint16_t> &cols, int U, int flipBit = -1,
                                  int stopAfterCol = -1, int badCount = -1) {
  const int HS_HALF = 15, HS_CYCLES = hsCycles, BOUNDARY = 30, LEAD_HOLD = 30;
  std::vector<Run> s;
  auto run = [&](int v, int n) { if (n > 0) s.push_back({v, n}); };
  auto sym = [&](int gap) { run(1, U); run(0, gap * U); };
  auto bit = [&](int b) { sym(b ? 2 : 1); };
  int k = 0;
  auto word = [&](int w) {          // ROW + 12 bits + odd parity
    sym(3);
    int ones = 0;
    for (int i = POV_N - 1; i >= 0; i--, k++) {
      int b = (w >> i) & 1;
      ones += b;
      bit(k == flipBit ? !b : b);
    }
    bit(ones & 1 ? 0 : 1);
  };

  run(0, LEAD_HOLD);
  for (int c = 0; c < HS_CYCLES; c++) { run(1, HS_HALF); run(0, HS_HALF); }
  run(0, BOUNDARY);
  for (int b : {0, 1, 0, 1, 0, 1, 0, 1}) bit(b);
  sym(5);
  sym(5);
  word(badCount >= 0 ? badCount : (int)cols.size());
  for (size_t c = 0; c < cols.size(); c++) {
    word(cols[c]);
    if ((int)c == stopAfterCol) { run(0, LEAD_HOLD); return s; }
  }
  sym(5);
  run(0, LEAD_HOLD);
  return s;
}

// ------------------------------------------------------------- channel ----
struct Channel {
  double fps = 60;          // sender's real frame rate
  double jitterMs = 0;      // +/- random shift of each light change
  double skew = 1.0;        // receiver clock / real time (RC oscillator error)
  bool analog = false;      // simulate the LDR + ADC instead of ideal lit
  double tauLitMs = 5;      // LDR response going lit (fast)
  double tauDarkMs = 15;    // LDR response going dark (slow)
  double prefixShadows = 0; // random light blips before the frame (count)
};

struct Edge { double t; int v; };

static std::vector<Edge> toEdges(const std::vector<Run> &s, const Channel &ch, double t0) {
  std::vector<Edge> e;
  double frame = 1000.0 / ch.fps;
  long f = 0;
  for (auto &r : s) {
    double j = ch.jitterMs * (2.0 * rand() / RAND_MAX - 1.0);
    e.push_back({t0 + f * frame + j, r.v});
    f += r.n;
  }
  e.push_back({t0 + f * frame, 0});
  return e;
}

static const char *stateName(uint8_t s) {
  static const char *n[] = {"HUNT", "LOCKED", "TRAIN", "LEADER", "ROWMARK", "BITS", "END"};
  return n[s];
}

// Columns delivered by RX_COL during the last receive().
static std::vector<uint16_t> gotCols;

// Feed the edge list to the receiver. Returns the decoder result.
static RxResult receive(const std::vector<Edge> &e, const Channel &ch, RxDecoder &d,
                        double endMs) {
  gotCols.clear();
  const double stepMs = 0.5;          // ~one averaged ADC read per loop pass
  RxLight light;
  rxInit(d, 0);
  lightInit(light, 0);
  double level = 950;                 // ADC reading: ~950 dark, ~150 lit
  size_t i = 0;
  int v = 0;
  for (double t = 0; t < endMs; t += stepMs) {
    while (i < e.size() && e[i].t <= t) v = e[i++].v;
    uint32_t now = (uint32_t)(t * 1000.0 * ch.skew);
    bool lit;
    if (ch.analog) {
      double target = v ? 150 : 950, tau = v ? ch.tauLitMs : ch.tauDarkMs;
      level += (target - level) * (1 - exp(-stepMs / tau));
      double noisy = level + (rand() % 9) - 4;
      lit = lightUpdate(light, now, (uint16_t)noisy);
      light.frozen = rxLocked(d);
    } else {
      lit = v;
    }
    RxResult r = rxFeed(d, now, lit);
    if (r == RX_COL) {
      if ((int)gotCols.size() != d.row - 1) return RX_FAIL;   // out of order
      gotCols.push_back(d.colBits);
      t += 7;   // the sword stops sampling while it writes the column to EEPROM
      continue;
    }
    if (r != RX_BUSY) return r;
  }
  return RX_BUSY;
}

static bool sameImage(const RxDecoder &d, const Grid g) {
  for (int r = 0; r < POV_N; r++)
    for (int c = 0; c < POV_N; c++) {
      int k = r * POV_N + c;
      if (!!(d.image[k >> 3] & (0x80 >> (k & 7))) != !!g[r][c]) return false;
    }
  return true;
}

// -------------------------------------------------------------- tests ----
static int failures = 0;

static void check(const char *name, bool ok) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) failures++;
}

static RxResult runRuns(const std::vector<Run> &s, const Channel &ch, RxDecoder &d);

static RxResult runFrame(const Grid g, int U, const Channel &ch, RxDecoder &d,
                         int flipBit = -1, int stopAfterRow = -1) {
  return runRuns(buildSchedule(g, U, flipBit, stopAfterRow), ch, d);
}

static RxResult runRuns(const std::vector<Run> &s, const Channel &ch, RxDecoder &d) {
  double t0 = 300;
  std::vector<Edge> e;
  // Optional random "shadows" (short light blips) before the transmission.
  for (int i = 0; i < ch.prefixShadows; i++) {
    double t = 50 + rand() % 200 + i * 250;
    e.push_back({t, 1});
    e.push_back({t + 40 + rand() % 120, 0});
    t0 = t + 400;
  }
  std::vector<Edge> f = toEdges(s, ch, t0);
  e.insert(e.end(), f.begin(), f.end());
  RxResult r = receive(e, ch, d, e.back().t + 20000);
  if (r != RX_DONE)
    printf("      -> result %d in state %s (row %d bit %d, u=%u us)\n",
           r, stateName(d.state), d.row, d.bit, (unsigned)d.unit);
  return r;
}

// Decode a schedule written by the web editor's own buildSchedule() (see the
// README). File: first line = expected columns (hex, space separated), then
// one "v n" run per line. Checks the editor and this decoder agree.
static int replayFile(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) { perror(path); return 2; }
  std::vector<uint16_t> want;
  char line[8192];
  if (!fgets(line, sizeof line, f)) return 2;
  for (char *p = strtok(line, " \n"); p; p = strtok(0, " \n")) want.push_back((uint16_t)strtol(p, 0, 16));
  std::vector<Run> s;
  int v, n;
  while (fscanf(f, "%d %d", &v, &n) == 2) s.push_back({v, n});
  fclose(f);

  RxDecoder d;
  Channel ch;
  ch.analog = true; ch.jitterMs = 2;
  RxResult r = runRuns(s, ch, d);
  std::vector<uint16_t> got = gotCols;
  if (r == RX_DONE && !d.wide)   // square frame: rows -> columns, like squareColumn()
    for (int c = 0; c < POV_N; c++) {
      uint16_t w = 0;
      for (int row = 0; row < POV_N; row++) {
        int k = row * POV_N + c;
        if (d.image[k >> 3] & (0x80 >> (k & 7))) w |= 1 << (POV_N - 1 - row);
      }
      got.push_back(w);
    }
  bool ok = r == RX_DONE && got == want;
  printf("%s  editor schedule %s (%s, %d columns)\n", ok ? "PASS" : "FAIL", path,
         d.wide ? "wide" : "square", (int)got.size());
  return ok ? 0 : 1;
}

int main(int argc, char **argv) {
  if (argc > 1) {
    int bad = 0;
    for (int i = 1; i < argc; i++) bad |= replayFile(argv[i]);
    return bad;
  }
  srand(1);
  Grid imgs[4];
  memset(imgs, 0, sizeof imgs);
  for (int r = 0; r < POV_N; r++)
    for (int c = 0; c < POV_N; c++) {
      imgs[0][r][c] = (r * 7 + c * 3 + r * c) % 5 < 2;                       // pattern
      imgs[1][r][c] = 0;                                                     // blank
      imgs[2][r][c] = 1;                                                     // full
      imgs[3][r][c] = r == 0 || c == 0 || r == POV_N - 1 || c == POV_N - 1;  // border
    }

  RxDecoder d;
  char name[128];

  // Ideal channel, every unit size the sender's slider offers, every image.
  for (int U = 2; U <= 6; U++)
    for (int i = 0; i < 4; i++) {
      Channel ch;
      bool ok = runFrame(imgs[i], U, ch, d) == RX_DONE && sameImage(d, imgs[i]);
      snprintf(name, sizeof name, "ideal        U=%d image %d", U, i);
      check(name, ok);
    }

  // Frame rate / RC clock error / display jitter.
  struct { double fps, skew, jit; } cases[] = {
    {60, 1.10, 3}, {60, 0.90, 3}, {50, 1.0, 4}, {75, 1.05, 2}, {30, 1.0, 5},
  };
  for (auto &c : cases)
    for (int U = 2; U <= 6; U++) {
      Channel ch;
      ch.fps = c.fps; ch.skew = c.skew; ch.jitterMs = c.jit;
      bool ok = runFrame(imgs[0], U, ch, d) == RX_DONE && sameImage(d, imgs[0]);
      snprintf(name, sizeof name, "fps=%2.0f skew=%.2f jitter=%.0fms U=%d", c.fps, c.skew, c.jit, U);
      check(name, ok);
    }

  // Full pipeline through the simulated LDR + calibration + hysteresis.
  for (int U = 2; U <= 6; U++) {
    Channel ch;
    ch.analog = true; ch.jitterMs = 2;
    bool ok = runFrame(imgs[0], U, ch, d) == RX_DONE && sameImage(d, imgs[0]);
    snprintf(name, sizeof name, "analog LDR (tau 5/15 ms) U=%d", U);
    check(name, ok);
  }

  // Shadows / blips before the handshake must not lock, and must not stop
  // the real handshake from locking afterwards.
  {
    Channel ch;
    ch.prefixShadows = 6;
    bool ok = runFrame(imgs[3], 3, ch, d) == RX_DONE && sameImage(d, imgs[3]);
    check("shadows before handshake", ok);
  }

  // The web sender's longer handshake (10 cycles), and a sword placed on the
  // screen late, so only the last 4 handshake flashes are seen.
  {
    Channel ch;
    hsCycles = 10;
    check("10-cycle handshake", runFrame(imgs[0], 6, ch, d) == RX_DONE && sameImage(d, imgs[0]));
    hsCycles = 4;
    check("only last 4 handshake flashes seen", runFrame(imgs[0], 6, ch, d) == RX_DONE && sameImage(d, imgs[0]));
    // A steady 2 Hz blink (e.g. swinging past a lamp) must not hold it locked.
    hsCycles = 40;
    check("endless 2 Hz blink -> FAIL", runFrame(imgs[0], 6, ch, d) == RX_FAIL);
    hsCycles = 6;
  }

  // Failures must be reported, never a wrong image.
  {
    Channel ch;
    check("flipped data bit -> parity FAIL", runFrame(imgs[0], 3, ch, d, 50) == RX_FAIL);
    check("truncated after row 6 -> FAIL", runFrame(imgs[0], 3, ch, d, -1, 5) == RX_FAIL);

    std::vector<Edge> none = {{0, 0}};
    check("no signal -> lock timeout FAIL", receive(none, ch, d, 20000) == RX_FAIL);
  }

  // ---- Wide images ----
  std::vector<uint16_t> wides[3];
  for (int n : {13, 40, POV_MAX_COLS}) {
    std::vector<uint16_t> w;
    for (int c = 0; c < n; c++) w.push_back((uint16_t)((c * 2654435761u >> 7) & 0xFFF));
    wides[n == 13 ? 0 : n == 40 ? 1 : 2] = w;
  }
  auto wideOk = [&](RxResult r, const std::vector<uint16_t> &w) {
    return r == RX_DONE && d.wide && d.cols == w.size() && gotCols == w;
  };
  for (int U = 2; U <= 6; U++)
    for (int i = 0; i < 2; i++) {
      Channel ch;
      snprintf(name, sizeof name, "wide %3d cols ideal       U=%d", (int)wides[i].size(), U);
      check(name, wideOk(runRuns(buildWide(wides[i], U), ch, d), wides[i]));
    }
  {
    Channel ch;
    check("wide 250 cols (max) ideal U=3", wideOk(runRuns(buildWide(wides[2], 3), ch, d), wides[2]));
    ch.analog = true; ch.jitterMs = 2;
    check("wide 250 cols analog LDR  U=3", wideOk(runRuns(buildWide(wides[2], 3), ch, d), wides[2]));
  }
  for (auto &c : cases)
    for (int U : {2, 4}) {
      Channel ch;
      ch.fps = c.fps; ch.skew = c.skew; ch.jitterMs = c.jit;
      snprintf(name, sizeof name, "wide 40 fps=%2.0f skew=%.2f jitter=%.0fms U=%d", c.fps, c.skew, c.jit, U);
      check(name, wideOk(runRuns(buildWide(wides[1], U), ch, d), wides[1]));
    }
  {
    Channel ch;
    check("wide: flipped column bit -> FAIL", runRuns(buildWide(wides[1], 3, 12 + 5 * 12 + 3), ch, d) == RX_FAIL);
    check("wide: flipped count bit -> FAIL", runRuns(buildWide(wides[1], 3, 4), ch, d) == RX_FAIL);
    check("wide: truncated after col 20 -> FAIL", runRuns(buildWide(wides[1], 3, -1, 19), ch, d) == RX_FAIL);
    check("wide: count 0 -> FAIL", runRuns(buildWide(wides[1], 3, -1, -1, 0), ch, d) == RX_FAIL);
    check("wide: count 251 -> FAIL", runRuns(buildWide(wides[1], 3, -1, -1, POV_MAX_COLS + 1), ch, d) == RX_FAIL);
    check("wide: count says 39, 40 sent -> FAIL", runRuns(buildWide(wides[1], 3, -1, -1, 39), ch, d) == RX_FAIL);
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
