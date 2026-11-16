# Generates web/how-it-works.html (static page, diagrams as inline SVG).
#   python3 tools/gen_how_it_works.py web/how-it-works.html
# Edit the text and diagrams here, not in the generated HTML. Numbers come from
# cycle_pov_sword/pov_protocol.h and the editor's buildSchedule(): keep in sync.
import math, sys

OUT = sys.argv[1]
W = 680  # diagram viewBox width

def svg(h, body, label):
    return (f'<svg class="dia" viewBox="0 0 {W} {h}" role="img" aria-label="{label}">'
            f'{body}</svg>')

def t(x, y, s, cls="lbl", anchor="start"):
    return f'<text x="{x}" y="{y}" class="{cls}" text-anchor="{anchor}">{s}</text>'

def arrow(x1, x2, y, label, cls="dim"):
    # double-headed dimension arrow with label above
    return (f'<line x1="{x1+3}" y1="{y}" x2="{x2-3}" y2="{y}" class="{cls}" marker-start="url(#ah)" marker-end="url(#ah)"/>'
            + t((x1 + x2) / 2, y - 5, label, "small", "middle"))

DEFS = ('<defs><marker id="ah" viewBox="0 0 10 10" refX="5" refY="5" markerWidth="5" markerHeight="5" orient="auto-start-reverse">'
        '<path d="M0,0 L10,5 L0,10 z" class="ahead"/></marker></defs>')

def strip(runs, x0, y, h, scale):
    """Screen strip: runs of (lit, frames). Returns svg and onset x list."""
    out, x, onsets, prev = [], x0, [], 0
    out.append(f'<rect x="{x0}" y="{y}" width="{sum(n for _, n in runs) * scale}" height="{h}" class="dark"/>')
    for v, n in runs:
        w = n * scale
        if v:
            out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" class="lit"/>')
            if not prev: onsets.append(x)
        prev = v
        x += w
    return "".join(out), onsets, x

# ---------------------------------------------------------------- A: circuit
def circuit():
    b = DEFS
    cx = 120
    b += t(cx, 22, "Vcc (battery +)", "lbl", "middle")
    b += f'<line x1="{cx}" y1="30" x2="{cx}" y2="50" class="wire"/>'
    # pull-up resistor (zigzag)
    zz = " ".join(f"{cx + (8 if i % 2 else -8)},{50 + i * 6}" for i in range(1, 9))
    b += f'<polyline points="{cx},50 {zz} {cx},104" class="wire"/>'
    b += t(cx + 18, 80, "pull-up inside the ATtiny (~30 kΩ)", "small")
    b += f'<line x1="{cx}" y1="104" x2="{cx}" y2="140" class="wire"/>'
    b += f'<circle cx="{cx}" cy="122" r="4" class="node"/>'
    b += f'<line x1="{cx}" y1="122" x2="{cx + 150}" y2="122" class="wire"/>'
    b += f'<rect x="{cx + 150}" y="108" width="110" height="28" rx="5" class="chip"/>'
    b += t(cx + 205, 127, "PB4 → ADC", "lbl", "middle")
    # LDR: resistor box in a circle with light arrows
    b += f'<circle cx="{cx}" cy="172" r="24" class="ldr"/>'
    b += f'<rect x="{cx - 7}" y="152" width="14" height="40" class="wirebox"/>'
    for dx in (-52, -40):
        b += f'<line x1="{cx + dx}" y1="138" x2="{cx + dx + 22}" y2="158" class="lightray" marker-end="url(#ah)"/>'
    b += t(cx - 58, 132, "light", "small accent", "middle")
    b += t(cx + 34, 176, "LDR (light sensor)", "small")
    b += f'<line x1="{cx}" y1="196" x2="{cx}" y2="222" class="wire"/>'
    b += f'<line x1="{cx - 16}" y1="222" x2="{cx + 16}" y2="222" class="wire"/>'
    b += f'<line x1="{cx - 10}" y1="228" x2="{cx + 10}" y2="228" class="wire"/>'
    b += f'<line x1="{cx - 4}" y1="234" x2="{cx + 4}" y2="234" class="wire"/>'
    b += t(cx, 252, "GND", "small", "middle")
    # explanation column
    x = 420
    rows = [("dark screen", "LDR ≈ 1 MΩ", "reading ≈ 950", ""),
            ("white screen", "LDR ≈ 1 kΩ", "reading ≈ 150", "accent")]
    y = 70
    b += t(x, 48, "What the ADC reads (0–1023)", "lbl")
    for a, r, v, c in rows:
        b += f'<rect x="{x}" y="{y}" width="34" height="34" rx="4" class="{"lit" if c else "dark"} edge"/>'
        b += t(x + 46, y + 14, a, "lbl")
        b += t(x + 46, y + 30, f"{r} → {v}", f"small {c}")
        y += 52
    b += t(x, y + 18, "More light = lower reading.", "small")
    return svg(262, b, "Circuit: the light sensor and a pull-up resistor form a voltage divider read by the ATtiny's ADC")

# ---------------------------------------------------------- B: calibration
def calibration():
    b = DEFS
    x0, x1, top, bot = 60, 650, 30, 170
    # simulate: handshake-like light, LDR fast to lit, slow to dark (exaggerated)
    pts, lit_state, level = [], [], 950.0
    n = 590
    period = 150
    for i in range(n):
        on = (i % period) < period / 2
        target = 150 if on else 950
        tau = 5 if on else 22
        level += (target - level) * (1 - math.exp(-1 / tau))
        pts.append(level)
    lo, hi = 150, 950
    mid = (lo + hi) / 2
    hy = (hi - lo) / 8
    tl, th = mid - hy, mid + hy
    Y = lambda v: top + (v - 100) / (1000 - 100) * (bot - top)
    path = "M" + " L".join(f"{x0 + i},{Y(v):.1f}" for i, v in enumerate(pts))
    by0, by1 = sorted((Y(tl), Y(th)))   # low readings are drawn at the top
    b += f'<rect x="{x0}" y="{by0:.1f}" width="{x1 - x0}" height="{by1 - by0:.1f}" class="band"/>'
    for v, lab in ((hi, "highest seen (dark)"), (lo, "lowest seen (lit)")):
        b += f'<line x1="{x0}" y1="{Y(v):.1f}" x2="{x1}" y2="{Y(v):.1f}" class="guide"/>'
        b += t(x0 - 6, Y(v) + 4, "950" if v == hi else "150", "small", "end")
    b += f'<line x1="{x0}" y1="{Y(mid):.1f}" x2="{x1}" y2="{Y(mid):.1f}" class="midline"/>'
    b += t(x0 - 6, Y(mid) + 4, "mid", "small accent", "end")
    b += f'<path d="{path}" class="curve"/>'
    # lit/dark decision strip
    state, sy = False, 196
    xs, cur = [], None
    for i, v in enumerate(pts):
        if state and v > th: state = False
        elif not state and v < tl: state = True
        xs.append(state)
    run_start = 0
    b += f'<rect x="{x0}" y="{sy}" width="{x1 - x0}" height="16" class="dark"/>'
    for i in range(1, n + 1):
        if i == n or xs[i] != xs[run_start]:
            if xs[run_start]:
                b += f'<rect x="{x0 + run_start}" y="{sy}" width="{i - run_start}" height="16" class="lit"/>'
            run_start = i
    b += t(x0 - 6, sy + 12, "lit?", "small", "end")
    b += t(x0, 18, "ADC reading", "small")
    b += t(x0, 236, "Red band: hysteresis, ± 1/8 of the swing around the midpoint.", "small")
    b += t(x0, 254, "Lit edges are sharp, dark edges are slow: the sword only times the sharp ones.", "small")
    return svg(264, b, "Calibration: the reading swings between dark and lit; a threshold halfway with a hysteresis band turns it into lit or dark")

# ------------------------------------------------------------ C: handshake
def handshake():
    b = DEFS
    sc = 1.6
    runs = [(0, 30)] + [(1, 15), (0, 15)] * 5 + [(0, 30)] + [(1, 6), (0, 6), (1, 6), (0, 12), (1, 6), (0, 6)]
    s, onsets, xend = strip(runs, 20, 70, 34, sc)
    b += s
    labels = ["1", "2", "3 ✓ locked", "4", "5"]
    for i in range(min(5, len(onsets) - 1)):
        a, c = onsets[i], onsets[i + 1]
        lab = "500 ms" if i < 4 else "1000 ms"
        b += arrow(a, c, 56, lab, "dim accent" if i == 2 else "dim")
    for i, lab in enumerate(labels[:3]):
        b += t((onsets[i] + onsets[i + 1]) / 2, 124, lab, "small accent" if "locked" in lab else "small", "middle")
    b += t((onsets[4] + onsets[5]) / 2, 124, "2× as long: go!", "small accent", "middle")
    b += t(20, 22, "Handshake: 10 slow flashes, 15 frames white + 15 black (500 ms apart)", "lbl")
    b += t(onsets[5] + 4, 142, "training →", "small")
    b += t(20, 160, "Three matching gaps in a row (300–1200 ms, within ±25 %) lock the sword on.", "small")
    b += t(20, 178, "The calibration freezes there, then a double-length dark gap marks the start.", "small")
    return svg(190, b, "Handshake: slow flashes 500 ms apart; after three matching intervals the sword locks, a double-length gap starts the data")

# -------------------------------------------------------------- D: symbols
def symbols():
    b = DEFS
    u = 22
    rows = [("bit 0", 1, "2 u"), ("bit 1", 2, "3 u"), ("ROW marker", 3, "4 u"), ("LEADER / END", 5, "6 u")]
    y = 46
    x0 = 150
    b += t(20, 22, "Every symbol = 1 unit of white, then a dark gap. The sword times one white start to the next.", "lbl")
    for name, gap, lab in rows:
        b += t(x0 - 12, y + 18, name, "lbl", "end")
        runs = [(1, 1), (0, gap), (1, 1)]
        s, on, xe = strip(runs, x0, y, 26, u)
        b += s
        b += f'<rect x="{xe}" y="{y}" width="{u}" height="26" class="fadeout"/>'
        b += arrow(on[0], on[1], y - 4, lab, "dim accent")
        y += 56
    # threshold ruler
    ry = y + 14
    b += t(20, ry - 2, "Decision", "lbl")
    ticks = [(1.5, "1.5"), (2.5, "2.5"), (3.5, "3.5"), (5, "5"), (8, "8 u")]
    zones = [(0, 1.5, "glitch"), (1.5, 2.5, "0"), (2.5, 3.5, "1"), (3.5, 5, "ROW"), (5, 8, "LEADER/END"), (8, 9.5, "lost")]
    for a, c, lab in zones:
        cls = "zone bad" if lab in ("glitch", "lost") else "zone"
        b += f'<rect x="{x0 + a * u * 2}" y="{ry + 10}" width="{(c - a) * u * 2}" height="22" class="{cls}"/>'
        b += t(x0 + (a + c) * u, ry + 25, lab, "small", "middle")
    for v, lab in ticks:
        x = x0 + v * u * 2
        b += f'<line x1="{x}" y1="{ry + 6}" x2="{x}" y2="{ry + 36}" class="tick"/>'
        b += t(x, ry + 50, lab, "small", "middle")
    b += t(x0 - 12, ry + 25, "interval", "small", "end")
    return svg(ry + 62, b, "Symbols: bit 0 is 2 units from flash to flash, bit 1 is 3, a row marker 4, leader or end 6; halfway points decide")

# ------------------------------------------------------------- E: training
def training():
    b = DEFS
    u = 13
    bits = [0, 1, 0, 1, 0, 1, 0, 1]
    runs = []
    for bt in bits:
        runs += [(1, 1), (0, 2 if bt else 1)]
    runs += [(1, 1)]
    s, on, xe = strip(runs, 60, 54, 30, u)
    b += s
    for i, bt in enumerate(bits):
        b += arrow(on[i], on[i + 1], 46, f"{bt}", "dim accent" if bt else "dim")
    b += t(20, 22, "Training: 0 1 0 1 0 1 0 1 tells the sword how long one unit is", "lbl")
    b += t(60, 112, "4 short (2 u) + 4 long (3 u) = 20 units in total, so  u = total time ÷ 20.", "small")
    b += t(60, 130, "Short and long must alternate exactly, or the transfer stops (error 2).", "small")
    b += t(60, 148, "So any screen frame rate works, and the sword's clock doesn't have to be exact.", "small")
    return svg(158, b, "Training: eight alternating short and long intervals add up to 20 units; dividing gives the unit length")

# ---------------------------------------------------------------- F: frame
def frame():
    b = DEFS
    def bar(y, parts, title):
        out = t(20, y - 8, title, "lbl")
        x = 20
        for lab, w, cls in parts:
            out += f'<rect x="{x}" y="{y}" width="{w}" height="34" rx="3" class="blk {cls}"/>'
            out += t(x + w / 2, y + 21, lab, "small", "middle")
            x += w + 3
        return out
    head = [("dark", 34, "d"), ("handshake ×10", 104, "h"), ("gap", 34, "d"), ("training", 70, "t"), ("LEADER", 60, "l")]
    word = lambda lab: [("ROW", 40, "r"), (lab, 96, "b"), ("P", 22, "p")]
    b += bar(36, head + word("row 1: 12 bits") [:3] + [("… ×12", 40, "x"), ("END", 44, "l")],
             "12 columns or fewer: a 12×12 image, row by row (works on every sword)")
    b += bar(112, head + [("LEADER", 60, "l")] + word("count") + [("col 1…", 50, "x"), ("END", 44, "l")],
             "Wider: a second LEADER, then the number of columns, then one word per column")
    b += t(20, 172, "Each word: a ROW marker, 12 bits (one row, or one column top to bottom), and a parity bit P.", "small")
    b += t(20, 190, "P makes the number of 1s in the 13 bits odd. A single misread bit breaks that, so the transfer stops", "small")
    b += t(20, 206, "instead of saving a wrong image. After END the screen stays dark: 5 units of silence mean “done”.", "small")
    return svg(216, b, "Frame layout: dark, handshake, gap, training, leader, then rows or a column count and columns, then end")

# ------------------------------------------------------------- G: success
def leds():
    b = DEFS
    def sword(x, y, lit, label, sub):
        out = f'<rect x="{x}" y="{y}" width="26" height="150" rx="6" class="blade"/>'
        for i in range(12):
            on = i in lit
            out += f'<circle cx="{x + 13}" cy="{y + 140 - i * 12}" r="4.2" class="{"ledon" if on else "ledoff"}"/>'
        out += t(x + 13, y + 172, label, "small", "middle")
        out += t(x + 13, y + 188, sub, "small dim2", "middle")
        return out
    b += sword(40, 20, set(range(12)), "received", "all blink 3×")
    b += sword(150, 20, {4}, "failed", "1.5 s: how far")
    b += sword(260, 20, set(range(5)), "then", "1.5 s: rows done")
    x = 360
    codes = ["1  handshake ok, start gap wrong", "2  training unreadable", "3  leader missing",
             "4  row marker missing", "5  unreadable bit", "6  extra pulses after the last row",
             "7  parity error (a bit was misread)"]
    b += t(x, 28, "Failure: one LED shows how far it got", "lbl")
    for i, c in enumerate(codes):
        b += t(x, 52 + i * 19, f"LED {c}", "small")
    b += t(x, 196, "(nothing at all = it never saw the handshake)", "small dim2")
    return svg(214, b, "Result on the sword: all LEDs blink three times on success; on failure one LED shows the error code, then a bar shows progress")

CSS = r'''
@font-face { font-family: "Jost"; font-style: normal; font-weight: 400 700; font-display: swap; src: url(fonts/jost.woff2) format("woff2"); }
:root {
  color-scheme: dark;
  --bg: #0b0707; --panel: #160d0d; --text: #f5e6e6; --muted: #a07f7f; --line: #3a1a1a;
  --accent: #e0242c; --board: #050303;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 17px/1.55 Futura, "Futura PT", "Jost", system-ui, sans-serif; }
main { max-width: 760px; margin: 0 auto; padding: 28px 16px 64px; }
h1 { font-size: 2.2rem; line-height: 1.1; margin: 6px 0 6px; }
h2 { font-size: 1.25rem; margin: 40px 0 8px; }
p { margin: 8px 0; }
a { color: var(--accent); }
.lead { color: var(--muted); font-size: 1.05rem; }
.muted { color: var(--muted); }
figure { margin: 14px 0 6px; background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 12px; }
.dia { display: block; width: 100%; height: auto; }
.dia text { font-family: inherit; fill: var(--text); }
.dia .lbl { font-size: 14px; }
.dia .small { font-size: 12px; fill: var(--muted); }
.dia .accent { fill: var(--accent); }
.dia .dim2 { fill: #6e5555; }
.dia .dark { fill: #000; stroke: var(--line); }
.dia .lit { fill: #fff; }
.dia .edge { stroke: var(--line); }
.dia .fadeout { fill: #000; opacity: 0; }
.dia .dim { stroke: var(--muted); stroke-width: 1.2; }
.dia .dim.accent { stroke: var(--accent); }
.dia .ahead { fill: context-stroke; }
.dia .wire { stroke: var(--text); stroke-width: 2; fill: none; }
.dia .wirebox { stroke: var(--text); stroke-width: 2; fill: none; }
.dia .node { fill: var(--text); }
.dia .ldr { stroke: var(--text); stroke-width: 2; fill: none; }
.dia .lightray { stroke: var(--accent); stroke-width: 2; }
.dia .chip { fill: #211212; stroke: var(--accent); }
.dia .band { fill: var(--accent); opacity: 0.32; }
.dia .guide { stroke: var(--line); stroke-dasharray: 4 4; }
.dia .midline { stroke: var(--accent); stroke-dasharray: 6 4; }
.dia .curve { fill: none; stroke: #fff; stroke-width: 2; }
.dia .zone { fill: #211212; stroke: var(--line); }
.dia .zone.bad { fill: #120a0a; }
.dia .tick { stroke: var(--muted); }
.dia .blk { stroke: var(--line); }
.dia .blk.d { fill: #000; } .dia .blk.h { fill: #3a1a1a; } .dia .blk.t { fill: #2e1919; }
.dia .blk.l { fill: #5a1616; } .dia .blk.r { fill: #4a1414; } .dia .blk.b { fill: #211212; }
.dia .blk.p { fill: #7a1c1c; } .dia .blk.x { fill: transparent; stroke-dasharray: 4 3; }
.dia .blade { fill: #160d0d; stroke: var(--line); }
.dia .ledon { fill: #ff2b2b; }
.dia .ledoff { fill: #261616; }
table { border-collapse: collapse; margin: 10px 0; font-size: 0.95rem; }
td, th { border-bottom: 1px solid var(--line); padding: 6px 12px 6px 0; text-align: left; vertical-align: top; }
th { color: var(--muted); font-weight: 500; }
code { font-size: 0.88em; color: #f0dede; }
.back { font-size: 0.95rem; }
'''

html = f'''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>How sending works · Lasersword editor</title>
<style>{CSS}</style>
</head>
<body>
<main>
  <p class="back"><a href="index.html">← Back to the editor</a></p>
  <h1>How does sending to the sword work?</h1>
  <p class="lead">No cable, no Bluetooth: the editor blinks a little square, and the sword reads the
    blinks with a light sensor. Here's what happens, step by step.</p>

  <h2>1. The sword's eye: a light sensor</h2>
  <p>The sword has an <b>LDR</b> (light-dependent resistor) on pin PB4 of its ATtiny85 chip. Together with
    a resistor inside the chip it forms a voltage divider. The chip's <b>ADC</b> measures the voltage
    thousands of times a second as a number from 0 to 1023. More light means a lower number.</p>
  <figure>{circuit()}</figure>

  <h2>2. Light or dark? Self-calibrating</h2>
  <p>Every screen, room and sensor is different, so there's no fixed threshold. The sword keeps track of
    the lowest and highest readings of the last second, and puts the threshold halfway between them.
    Around it is a <b>hysteresis band</b>: to count as “lit” the reading has to drop below the band, and to
    count as “dark” again it has to rise above it. Small flickers inside the band change nothing.</p>
  <figure>{calibration()}</figure>
  <p>Notice the curve: an LDR reacts to light within a few milliseconds, but takes much longer to recover in
    the dark. That's why the sword only times the moments the screen <i>turns white</i>, never when it turns black.</p>

  <h2>3. The handshake: “a sender is here”</h2>
  <p>While the sword shows its image, it keeps an eye on the sensor. Swinging past lamps makes random flashes,
    so it waits for something very regular: <b>10 slow flashes, half a second apart</b>.</p>
  <figure>{handshake()}</figure>
  <p>Once three intervals in a row match, the sword stops displaying, shows a light bar, and freezes its
    calibration. A steady half-second blink that goes on for too long isn't a sender either, so after 16 it gives up.</p>

  <h2>4. Training: how long is one unit?</h2>
  <p>The data is sent in <b>units</b> of a few screen frames each: the <i>Transfer speed</i> slider picks
    2 to 6 frames per unit (33 to 100 ms at 60 frames a second). The sword doesn't know which, so the editor
    first sends a fixed pattern to measure it.</p>
  <figure>{training()}</figure>

  <h2>5. Symbols: it's all about timing</h2>
  <p>Every symbol is one unit of white followed by a gap. Only the length from one white flash to the next
    matters, and each length means something different. The sword decides with halfway points, so timing can
    be off by almost half a unit and still read correctly.</p>
  <figure>{symbols()}</figure>

  <h2>6. A whole transfer</h2>
  <figure>{frame()}</figure>
  <table>
    <tr><th>Transfer speed</th><th>One unit</th><th>12 × 12 image</th><th>12 × 60 image</th></tr>
    <tr><td>fastest (2 frames)</td><td>33 ms</td><td>about 22 s</td><td>about 1.4 min</td></tr>
    <tr><td>slowest (6 frames)</td><td>100 ms</td><td>about 52 s</td><td>about 4 min</td></tr>
  </table>
  <p class="muted">Faster works if the sensor and screen are quick enough; slower gives a slow sensor time to
    recover between flashes. That's the whole trade-off behind the slider.</p>

  <h2>7. Saving, and how the sword tells you</h2>
  <p>The image goes into the chip's <b>EEPROM</b>, memory that survives switching off. It's saved with a
    checksum, read back and checked, and only then marked valid by writing one last byte. A cut-off or
    garbled image is never shown. Long images are written column by column during the transfer: between two
    columns there's always enough time for the ~7 ms it takes to write one.</p>
  <figure>{leds()}</figure>
  <p class="muted">If it keeps failing: turn the screen brightness up, hold the sensor flat on the square,
    keep the tab in front, and move <i>Transfer speed</i> towards slower.</p>

  <p class="back" style="margin-top:40px"><a href="index.html">← Back to the editor</a></p>
</main>
</body>
</html>
'''
open(OUT, "w").write(html)
print("wrote", OUT, len(html), "bytes")
