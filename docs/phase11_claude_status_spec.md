# Clawd Status Mode — FINAL Pixel & Motion Specification

This replaces the render logic in `src/modes/claude_status.cpp`. Two styles (`CLAWD`, `SPARKLE`) × three states (`IDLE`, `WORKING`, `WAITING`). All coordinates are integers, center `(120,120)`, hard safe radius `<= 110`. Frame tick is a fixed **33 ms** cadence (~30 fps); idle uses **66 ms** quantization (every 2nd tick) to save SPI. `fillScreen` runs **only** in `redrawBase()`.

---

## CRITICAL INTEGRATION CONSTRAINT (read first)

The current code reads `bg_color_` from `state::g_state.bg_color_565` — **the user can recolor the background at runtime** (default `0xD880`, but changeable via web). Therefore:

- **No state hardcodes its own background.** The "swap background to amber for WAITING" idea is REJECTED — it breaks user bg control and the existing erase-with-`bg_color_` contract.
- The member `bg_color_` is the single erase color everywhere. In this spec the token `BG` means "the current `bg_color_` value," NOT a literal. All `fillRect(...,BG)` / `fillCircle(...,BG)` erases pass `bg_color_`.
- `redrawBase()` does `fillScreen(bg_color_)`, then draws the static layer.
- `applyState()` already triggers `redrawBase()` on status change OR bg change. **Add: also trigger `redrawBase()` on `cc_style` change.** Treat style change exactly like state change.
- Eyes are square (`fillRect`), matching existing EyesNormal house style. We keep `fillRect` eyes (NOT rounded) for IDLE/WORKING/WAITING to stay consistent with the rest of the firmware; rounding is not worth the extra primitive here.

The "DOZE dims the background" mechanic is also rejected for the same reason (would fight user bg). DOZE is conveyed purely by half-lids + floating `z`.

---

## 1. COLOR PALETTE (RGB565)

| Name | RGB565 | Usage |
|------|--------|-------|
| `kEyeColor` | `0x0000` | eyes, exclamation bar/dot (black, existing default) |
| `kGreen` | `0x07E0` | WORKING accent: CLAWD top sparkle + SPARKLE hero rays |
| `kGreenHi` | `0x9FF3` | WORKING sparkle core highlight / ray-tip glint |
| `kAmber` | `0xFC60` | WAITING accent: exclamation, ring, SPARKLE amber body (warmer/more urgent than old yellow `0xFFE0`) |
| `kAmberHi` | `0xFFE0` | WAITING ring bright crest, SPARKLE amber core highlight |
| `kCream` | `0xFF38` | dozing `z` glyph; SPARKLE idle calm star body |
| `kGlint` | `0xFFFF` | white catch-light square in WAITING eyes (alive/eager) |

`BG` (erase color) = runtime `bg_color_` (default `0xD880`). Not in the table because it is a variable, not a constant.

Only these 7 constants plus `BG` are used. No other colors.

---

## 2. SHARED DRAW HELPERS (signatures + behavior)

All helpers are members of `ClaudeStatus`, pure integer except sparkle trig (float `sinf/cosf`, acceptable — ESP32-S3, ≤16 calls/frame). `auto& d = M5Dial.Display;` inside each.

```cpp
// ---- eyes (square, existing house style) ----
// Draw one square eye at (x,y,w,h). lidTop = px of background lid from the TOP
// (0 = fully open, h = fully closed). lidBot = px of background lid from the BOTTOM.
// glint = draw a 4x4 kGlint square near upper area when eye is open enough.
void drawEye(int16_t x, int16_t y, int16_t w, int16_t h,
             int16_t lidTop, int16_t lidBot, bool glint);
// Body = fillRect(x,y,w,h,kEyeColor); then fillRect top lidTop in BG; bottom lidBot in BG;
// if (glint && lidTop < h/3) fillRect(x+w-9, y+lidTop+5, 4,4, kGlint).

// Erase one eye's MAX bounding box to BG (constant per state, covers all motion).
void eraseEyeBox(int16_t clrX, int16_t clrY, int16_t clrW, int16_t clrH);
// = d.fillRect(clrX, clrY, clrW, clrH, bg_color_);

// ---- exclamation (shared CLAWD-waiting + SPARKLE-waiting) ----
// Vertical "!" centered on cx, bar top at topY. Bar 10x34 + gap + dot 10x10.
void drawBang(int16_t cx, int16_t topY, uint16_t col);
// bar = fillRect(cx-5, topY, 10, 34, col); dot = fillRect(cx-5, topY+40, 10, 10, col).
// Erase (caller): d.fillRect(cx-7, prevTopY-2, 14, 56, bg_color_) before redraw.

// ---- dozing z (3 strokes, chunky, NOT a font glyph) ----
void drawZ(int16_t x, int16_t y, int16_t s, uint16_t col);
// top  : fillRect(x, y, s, 2, col)
// diag : drawWideLine(x+s, y, x, y+s, 2, col)
// base : fillRect(x, y+s-2, s, 2, col)
// Erase (caller): d.fillRect(prevX-1, prevY-1, prevS+3, prevS+3, bg_color_).

// ---- sparkle (shared hero/accent/star) — N tapered rays + 2-tone core ----
// 8 rays. baseAngleDeg[i] = 45*i. Each ray a fillTriangle from a base near the
// core out to a tip at radius L[i]. coreR = inner disc radius. hi = highlight color.
// Returns the max ray length drawn this call (store as eraseR for next frame).
int16_t drawSparkle(int16_t cx, int16_t cy, const int16_t L[8],
                    int16_t halfW, float rotDeg,
                    int16_t coreR, uint16_t body, uint16_t hi);
// For each i: a = (45*i + rotDeg) * DEG2RAD;
//   tip  = (cx + L[i]*cos a, cy + L[i]*sin a)
//   perpendicular p = a + 90deg; base points at radius coreR +/- halfW along p:
//   b1 = (cx + coreR*cos a + halfW*cos p, cy + coreR*sin a + halfW*sin p)
//   b2 = (cx + coreR*cos a - halfW*cos p, cy + coreR*sin a - halfW*sin p)
//   fillTriangle(b1, b2, tip, body)
// core : fillCircle(cx,cy,coreR,body); fillCircle(cx,cy,coreR-3,hi)  [coreR>=4]
// returns max(L[i]).

// Erase prior sparkle in ONE primitive (bounding disc). Call BEFORE drawSparkle.
void eraseSparkle(int16_t cx, int16_t cy, int16_t eraseR);
// = d.fillCircle(cx, cy, eraseR, bg_color_);   eraseR = lastMaxL + halfW + 2
```

> Sparkle erase is a single `fillCircle` of the previous footprint. This is the canonical no-flicker pattern for a rotating vector shape: one disc wipe + redraw, every changed pixel written once. It is NOT `fillScreen` — it is a localized disc (max r ≈ 99) whose footprint never reaches the rim and (in SPARKLE-IDLE) never reaches the eyes below it (proven in §5.3).

---

## 3. STATE MACHINE & redrawBase CONTRACT

- `redrawBase()` fires when **state changes OR style changes** (extend existing `applyState` to compare `cc_style`). It is the ONLY place `fillScreen` is allowed.
  - `d.fillScreen(bg_color_)`, reset all phase/prev trackers (`frame_idx_=0xFF`, `prevTopY_=-1`, `eraseR_=0`, `dozePhase_=AWAKE`, blink/glance timers = now), then draw the static layer for the new (style,state).
- `tick(now)` animates only the moving element(s) with the per-element erase named below. **`tick` never calls `fillScreen`.**
- IDLE dwell timer: `elapsed = now - enter_time_`. CLAWD-IDLE enters DOZE at `elapsed >= kDozeAfterMs`. Any `applyState` (hook ping / state change) resets `enter_time_` and re-runs `redrawBase` → wakes.
- Randomized intervals (blink gaps, glance, brow dip) use `esp_random()`; store `nextEventMs_`.
- Existing `speed_` scaling: keep applying to IDLE blink/glance cadence only (as today). WORKING/WAITING/SPARKLE timings are fixed (state semantics, not user "speed").

---

## 4. STYLE = CLAWD (emotive eyes + signature overlay)

Eye geometry. Overlays live in the **top band (`y < 60`)** or **rim**, disjoint from eye boxes (`y >= 62`), so overlays and eyes never share an erase box.

| State | W | H | gap | leftX | rightX | topY | eyeCenterY |
|-------|---|---|-----|-------|--------|------|-----------|
| IDLE | 30 | 60 | 96 | 42 | 168 | 70 | 100 |
| WORKING | 30 | 60 | 96 | 42 | 168 | 70 | 100 (gaze faked by lids, see below) |
| WAITING | 34 | 64 | 92 | 38 | 168 | 64 | 96 |

**Rim safety of eyes (worst corner):**
- IDLE/WORKING right eye corner `(168+30, 70+60) = (198,130)`: dx=78, dy=10 → r=√(6084+100)=**78.6** < 110. ✓ Top corner `(198,70)`: dx=78,dy=−50 → r=√(6084+2500)=**92.7** < 110. ✓
- WAITING right eye corner `(168+34, 64+64) = (202,128)`: dx=82,dy=8 → r=√(6724+64)=**82.4** < 110. ✓ Top `(202,64)`: dx=82,dy=−56 → r=√(6724+3136)=**99.3** < 110. ✓ Left top `(38,64)`: dx=−82,dy=−56 → r=**99.3** < 110. ✓

### 4.1 CLAWD · IDLE — "resting companion, then dozing"

**This is the fix for "idle == plain swaying eyes."** The plain mode swings horizontally in 5 discrete steps. This mode does NOT swing horizontally. Identity = three layers:

**Static (redrawBase):** both open eyes at IDLE geometry, `drawEye(42,70,30,60,0,0,false)` and `drawEye(168,70,30,60,0,0,false)`. `dozePhase_ = AWAKE`.

**Animated per frame (66 ms quantization):**

(a) **Vertical breathing** (replaces swing). Both eyes drift on a slow sine: `dy = round(3 * sinf(now * 0.0011f))` → ±3 px, period ≈ 5.7 s. Applied as a Y offset to both eyes (same dy). Redraw only when integer `dy` changes (~every ~300 ms).

(b) **Varied randomized blink** (replaces metronomic timeline). Blink = `lidTop` ramps `0 → 60 → 0`. Inter-blink gap = uniform random `[2800, 5200] ms` (key difference from old fixed timeline). Two blink shapes, picked 80/20:
- `single`: lidTop 0→30→60→30→0, one 33 ms frame each (~165 ms).
- `slow` (sleepy): lidTop 0→60 over 160 ms, hold 60 for 120 ms, 60→0 over 160 ms.
Blink overrides breathing dy for its duration (eyes hold position while blinking).

(c) **Occasional glance** (the only horizontal motion, and it's a one-shot, NOT continuous sway): every random `[4000, 9000] ms`, shift both eyes together by +2 px OR −2 px (random sign), hold `700 ms`, then return. Two-step ease. This reads as "looked at something," not "swaying forever."

**DOZE (after `kDozeAfterMs = 12000` ms continuous idle):** `dozePhase_ = DOZE`.
- Eyes pinned half-lidded: `lidTop = 33` (55% of 60), eyeCenterY sinks +2 (draw at `y = 72`). Breathing reduced to ±2 px, period ~7 s.
- **Floating z** (signature, top band): one `z` alive at a time. Spawns every `kZSpawnMs = 2600` ms at `(138, 56)` size 8, travels up-right to `(150, 32)` over `kZLifeMs = 2000` ms, growing 8→12. Fade is faked by 3 color steps over life thirds: `kCream` → `0xFEF0` → (last third) drawn in BG = vanish. Linear interpolation of x,y,s by `t = lifeElapsed / kZLifeMs`.
- DOZE exits on any `applyState` (→ `redrawBase`).

**z rim check (worst extreme):** end point `(150, 32)`, plus glyph extent +12: bottom-right `(162, 44)`: dx=42, dy=−76 → r=√(1764+5776)=√7540=**86.8** < 110. ✓

**Erase summary (IDLE):**

| Element | Prev tracked | Erase op |
|---------|--------------|----------|
| Left eye | dy, lidTop, glanceX | `fillRect(40, 66, 34, 68, BG)` then redraw (covers ±2 glance, ±3 breath, +2 doze sink) |
| Right eye | dy, lidTop, glanceX | `fillRect(166, 66, 34, 68, BG)` then redraw |
| Doze z | prevX, prevY, prevS | `fillRect(prevX-1, prevY-1, prevS+3, prevS+3, BG)` |

Clear band `40..74` and `166..200`, `y∈[66,134]` is below all z positions (`y<=44`) → disjoint. ✓

### 4.2 CLAWD · WORKING — "concentrating, reading code" + pulsing green Claude sparkle

**Static (redrawBase):** both eyes drawn **downcast** via a bottom-biased aperture: `drawEye(42,70,30,60, lidTop=8, lidBot=22, false)` and same at x=168. Visible aperture sits low → reads as looking down at code. Eyes are otherwise STILL (focus = stillness; only sparkle animates). Draw the static green sparkle's first frame too.

**Animated per frame (33 ms):**

(a) **Top green Claude sparkle, pulsing** (replaces the generic orbit dot — on-brand). Center `(120, 44)`. 8 rays, `halfW = 3`, `coreR = 6`, body `kGreen`, hi `kGreenHi`. All rays equal length, synchronized pulse (calm, not frantic): `L = 19 + round(3 * sinf(now * 0.00449f))` → range **16..22**, period ≈ 1.4 s. Apply to all 8 `L[i]`. Slow rotation `rotDeg += 1.2` per frame (~36°/s, calm turn — NOT the 1.08 s spinner).
- Erase each frame: `eraseSparkle(120, 44, eraseR_)` where `eraseR_ = lastMaxL + halfW + 2 = 22 + 3 + 2 = 27`. Then `drawSparkle(...)`, store new `eraseR_`.

(b) **Rare focus blink:** a single `single`-shape blink every `[5000, 8000] ms` (deep focus = rare blink). Renders by re-drawing both eyes with lidTop ramping over the downcast baseline (lidTop 8→60→8). Erase via eye boxes (below). Glance/breathing are OFF in WORKING.

**Sparkle bbox & overlap check:** disc center `(120,44)`, max r `eraseR_=27`. Bottom of disc `y = 44+27 = 71`. Eye top `y = 70`. **1 px graze.** Resolution: the eyes are STILL between rare blinks (drawn at redrawBase + only on blink frames), and the sparkle disc bottom (y=71) only touches the eyes' topmost row. To make it airtight, cap working sparkle `L_max = 20` (not 22): `eraseR_ = 20+3+2 = 25`, disc bottom `y = 44+25 = 69 < 70`. **Zero overlap.** → **Use `L ∈ [16,20]`, pulse `L = 18 + round(2*sinf(now*0.00449f))`.**
- Rim check: disc top `y = 44−25 = 19 > 10`. ✓ Disc never near rim.

**Erase summary (WORKING):**

| Element | Erase op |
|---------|----------|
| Sparkle | `eraseSparkle(120,44,25)` each frame + redraw |
| Eyes (rare blink only) | `fillRect(40,68,34,64,BG)` & `fillRect(166,68,34,64,BG)` + redraw on blink-frame change |

### 4.3 CLAWD · WAITING — "wide alert eyes looking up + bouncing exclamation + sweeping ring"

**Static (redrawBase):** wide eyes biased UP (looking at viewer): `drawEye(38,64,34,64, lidTop=0, lidBot=14, glint=true)` and same at x=168 (glint = 4×4 `kGlint`). Draw base dim ring `fillArc(120,120,103,107,0,360,kAmber)` once. Draw initial bang.

**Animated per frame (33 ms):**

(a) **Bouncing exclamation** in the empty center column (`cx=120`, x∈[113,127] — never shares x with eyes at x∈[38,72] and [168,202], so its y-range may freely overlap eye y-range). `drawBang(120, topY, kAmber)`, `topY = 30 + round(8 * fabsf(sinf(now * 0.00599f)))` → range **30..38**, bounce period ≈ 1.05 s (lively |sin| bounce-and-settle).
- Erase: `fillRect(113, prevTopY-2, 14, 56, BG)` then redraw when `topY` changes.
- Rim check (worst): dot bottom at `topY=38` → `(120, 38+50)=(120,88)`: dx=0,dy=−32 → r=**32** < 110. ✓ Top at `topY=30` → `(120,30)`: r=**90** < 110. ✓

(b) **Sweeping bright ring crest** (replaces the on/off blink — no strobe). Base ring stays `kAmber`. A 40°-wide bright `kAmberHi` arc rotates clockwise `+12°` per frame every `kCrestStepMs = 60` ms (~2 s/lap): draw `fillArc(120,120,103,107, sweep, sweep+40, kAmberHi)`. Erase = repaint the trailing 12° back to base: `fillArc(120,120,103,107, prevSweep, prevSweep+12, kAmber)`. Two `fillArc`/step. Reads as a "loading halo demanding attention," continuous, no flicker.
- Rim check: ring outer r=107 < 110. ✓

(c) **Attention pop (optional, subtle):** every `kPopMs = 1500` ms both eyes grow +2 px taller (lidBot 14→12, top y 64→62) for 120 ms then back. Erase via eye boxes. (Include only if cheap; can be dropped — bang + ring carry the energy.)

**Erase summary (WAITING):**

| Element | Erase op |
|---------|----------|
| Bang | `fillRect(113, prevTopY-2, 14, 56, BG)` + redraw on change |
| Ring crest | repaint trailing 12° `kAmber` + draw new 40° `kAmberHi` |
| Eyes (pop) | `fillRect(36,61,38,72,BG)` & `fillRect(166,61,38,72,BG)` + redraw on pop change |

Bang erase x∈[113,127] vs eyes x∈[36,74]/[166,204] → disjoint. ✓ Ring annulus r∈[103,107] vs all eye/bang pixels (max r 99.3 / 90) → disjoint. ✓

---

## 5. STYLE = SPARKLE (Claude sparkle hero at center)

8-ray sparkle at `(120,120)` is the hero. Eyes appear ONLY in IDLE (small, below). Sparkle erase = single bounding disc per §2.

### 5.1 SPARKLE · WORKING — "large slowly-rotating sparkle, rays pulsing"

**Static (redrawBase):** draw first frame of hero sparkle. No eyes.

**Animated per frame (33 ms):** hero sparkle, center `(120,120)`, `coreR = 14`, `halfW = 7`, body `kGreen`, hi `kGreenHi`.
- **Rotation:** `rotDeg += 0.8` per frame (~24°/s, full turn ≈ 15 s — slow, majestic).
- **Per-ray length pulse with stagger** (rays shimmer): for ray i, `L[i] = round(Lbase * (0.92f + 0.08f * sinf(now*0.004f + i*0.785f)))`, where `Lbase = 84 + round(8 * sinf(now*0.00393f))` → `Lbase ∈ [76, 92]`, overall pulse period ≈ 1.6 s; per-ray ×[0.92,1.0]. Max `L[i] = 92`.
- Erase: `eraseSparkle(120,120, eraseR_)`, `eraseR_ = 92 + 7 + 2 = 101`. Then redraw, store actual `maxL + 9` returned.

**Rim check (worst case):** longest ray tip at radius `L=92` → r=**92** < 110. ✓ The triangle base corners sit at radius `≈ coreR=14` offset by `halfW=7` perpendicular → base corner r ≈ √(14²+7²)=15.7, well inside. The widest part of a ray is at the base (near center); the line from base to tip never exceeds tip radius 92. Erase disc r=101 < 110. ✓ **(Lbase capped at 92 precisely so 92+7+2=101 ≤ 110.)**

### 5.2 SPARKLE · WAITING — "freeze, turn amber, grow for a beat, exclamation"

**Static (redrawBase):** snap `rotDeg = 0` (symmetric asterisk — base angles 0,45,…,315; **no ray points straight up at 0°** since the nearest up-rays are at 45°/315° i.e. up-left/up-right). Draw sparkle frozen, body `kAmber`, hi `kAmberHi`, `coreR = 16`, `halfW = 8`, all rays equal length.

**Animated per frame (33 ms):**

(a) **Grow-beat heartbeat:** every `kBeatMs = 1400` ms, all-ray length L ramps `64 → 78 → 64`: 64→78 over 120 ms (`easeOutQuad`), 78→64 over 200 ms (`easeInOutSine`); between beats HOLD at 64 (frozen, tense). `easeOutQuad(t)=1−(1−t)²`, `easeInOutSine(t)=0.5−0.5cos(πt)`.
- Erase during a beat only: `eraseSparkle(120,120, eraseR_)`, `eraseR_ = 78 + 8 + 2 = 88`. Between beats sparkle is static → no draw, no flicker.

(b) **Bouncing exclamation** above the sparkle. The up-most ray reach at peak L=78: rays at 45°/315° (up-left/up-right), `y_top = 120 − round(78 * sin(45°)) = 120 − 55 = 65`. So the sparkle's topmost pixel is y≈65 at the beat peak. The bang sits in `cx=120` column above that. `drawBang(120, topY, kAmberHi)`, `topY = 14 + round(4 * fabsf(sinf(now*0.006f)))` → range **14..18**. Bang dot bottom at `topY=18` → `18+50 = 68`. Up-rays at x = 120 ± 78·cos(45°)=120±55 → x∈{65,175} at their tips; bang x∈[113,127]. At the bang's lowest pixel (y=68) the sparkle rays at x=120 column reach only to y≈65 region near x=65/175, NOT at x=120 (no ray at x=120 above center). So bang column x∈[113,127] is clear of ray tips (nearest up-rays are at x≈65 and x≈175). Effective vertical clearance is ample. **No overlap.**
  - Erase: `fillRect(113, prevTopY-2, 14, 56, BG)`.
  - Rim check: bang top `(120,14)`: dx=0,dy=−106 → r=**106** < 110. ✓ (topY min 14 is the binding rim constraint — do not lower topY below 13.) Dot bottom `(120,68)`: r=**52**. ✓

**Erase summary (WAITING):**

| Element | Erase op |
|---------|----------|
| Sparkle | beat frames: `eraseSparkle(120,120,88)` + redraw; static between beats |
| Bang | `fillRect(113, prevTopY-2, 14, 56, BG)` + redraw on change |

### 5.3 SPARKLE · IDLE — "shrink to calm star + slow-blink eyes return"

**Static (redrawBase):** small calm star at `(120,78)` and two small open eyes below. Star: 8 rays, `coreR = 5`, `halfW = 2`, `L = 13` all rays (or tiny breath 12..15), body `kCream`, hi `kAmberHi`, `rotDeg = 0` (or ultra-slow +0.2/frame). Eyes: W=30, H=60, leftX=42, rightX=168, **topY=104** (eyeCenterY=134), `drawEye(...,0,0,false)`.

**Animated per frame (66 ms idle quantization):**

(a) **Star twinkle (optional):** `L = 13 + round(2 * sinf(now*0.00224f))` → 11..15, period ≈ 2.8 s; optional ultra-slow rotation +0.2/frame.
- Erase: `eraseSparkle(120,78, eraseR_)`, `eraseR_ = 15 + 2 + 2 = 19`. Star disc: top `y=78−19=59`, **bottom `y=78+19=97`**. Eye top `y=104`. Disc bottom (97) < eye top (104) → **7 px clearance, disjoint.** ✓ If you prefer zero per-frame cost, draw the star once in redrawBase and skip twinkle (fully valid).

(b) **Slow-blink eyes:** single blink every `[3500, 6000] ms`, lidTop 0→60 over 120 ms / hold 80 ms / 60→0 over 120 ms. Calm, sleepy. No breathing, no swing, no glance (serenity — the star carries personality). Erase via eye boxes only on blink frames.

**Rim checks:**
- Star tip top `(120, 78−15) = (120,63)`: dx=0,dy=−57 → r=**57** < 110. ✓
- Eye worst corner: right eye `(168+30, 104+60) = (198,164)`: dx=78,dy=44 → r=√(6084+1936)=√8020=**89.6** < 110. ✓ Left bottom `(42,164)`: dx=−78,dy=44 → r=**89.6**. ✓

**Erase summary (SPARKLE IDLE):**

| Element | Erase op |
|---------|----------|
| Star | `eraseSparkle(120,78,19)` + redraw (or static, no per-frame) |
| Eyes | `fillRect(40,102,34,64,BG)` & `fillRect(166,102,34,64,BG)` + redraw on blink change |

---

## 6. WORST-CASE RIM SUMMARY (every animated extreme, must be ≤ 110)

| Style·State | Extreme element | Worst point | r | OK |
|---|---|---|---|---|
| CLAWD·IDLE | eye corner | (198,130) | 78.6 | ✓ |
| CLAWD·IDLE | doze z far corner | (162,44) | 86.8 | ✓ |
| CLAWD·WORK | sparkle disc top | (120,19) | 101 (from center, dy=76) → r=76 | ✓ |
| CLAWD·WORK | eye top corner | (198,70) | 92.7 | ✓ |
| CLAWD·WAIT | eye top corner | (202,64) | 99.3 | ✓ |
| CLAWD·WAIT | ring outer | r=107 | 107 | ✓ |
| CLAWD·WAIT | bang dot bottom | (120,88) | 32 | ✓ |
| SPARKLE·WORK | hero ray tip | r=92 | 92 | ✓ |
| SPARKLE·WORK | erase disc | r=101 | 101 | ✓ |
| SPARKLE·WAIT | hero ray tip (peak) | r=78 | 78 | ✓ |
| SPARKLE·WAIT | bang top | (120,14) | 106 | ✓ (binding) |
| SPARKLE·IDLE | eye corner | (198,164) | 89.6 | ✓ |
| SPARKLE·IDLE | star erase disc | r=19 @ (120,78) bottom (120,97) | 23 | ✓ |

The two binding constraints: **SPARKLE·WAIT bang top (r=106, keep topY ≥ 14)** and **SPARKLE·WORK erase disc (r=101, keep Lbase_max ≤ 92)**. Everything else has ≥ 10 px margin.

---

## 7. CONSTANTS (constexpr-ready)

```cpp
// ---- palette (RGB565) ----
constexpr uint16_t kEyeColor = 0x0000;
constexpr uint16_t kGreen    = 0x07E0;
constexpr uint16_t kGreenHi  = 0x9FF3;
constexpr uint16_t kAmber    = 0xFC60;
constexpr uint16_t kAmberHi  = 0xFFE0;
constexpr uint16_t kCream    = 0xFF38;
constexpr uint16_t kGlint    = 0xFFFF;
// BG is runtime bg_color_ (default 0xD880) — NOT a constant.

// ---- geometry ----
constexpr int16_t kCx = 120, kCy = 120, kSafeR = 110;
constexpr float   kDeg2Rad = 0.01745329f;

// ---- CLAWD eyes ----
constexpr int16_t kEyeW=30, kEyeH=60, kEyeGap=96;
constexpr int16_t kLeftX=42, kRightX=168, kEyeYT=70, kEyeCY=100;
constexpr int16_t kWideW=34, kWideH=64, kWideLeftX=38, kWideRightX=168, kWideYT=64;

// idle eye clear boxes (cover breath/glance/doze)
constexpr int16_t kIdleClrLX=40, kIdleClrRX=166, kIdleClrY=66, kIdleClrW=34, kIdleClrH=68;
// waiting eye clear boxes (cover pop)
constexpr int16_t kWaitClrLX=36, kWaitClrRX=166, kWaitClrY=61, kWaitClrW=38, kWaitClrH=72;

// ---- CLAWD timings (ms) ----
constexpr uint32_t kIdleTickMs    = 66;     // idle quantization
constexpr uint32_t kFrameMs       = 33;     // working/waiting cadence
constexpr uint32_t kBlinkGapMinMs = 2800, kBlinkGapMaxMs = 5200;
constexpr uint32_t kGlanceGapMinMs= 4000, kGlanceGapMaxMs= 9000;
constexpr uint32_t kGlanceHoldMs  = 700;
constexpr int16_t  kGlanceDx      = 2;
constexpr int16_t  kBreathAmp     = 3;      // px
constexpr float    kBreathRate    = 0.0011f;// rad/ms (period ~5.7s)
constexpr uint32_t kDozeAfterMs   = 12000;
constexpr int16_t  kDozeLidTop    = 33;     // 55% of 60
constexpr int16_t  kDozeSink      = 2;
constexpr uint32_t kZSpawnMs      = 2600, kZLifeMs = 2000;
constexpr int16_t  kZStartX=138, kZStartY=56, kZEndX=150, kZEndY=32, kZSizeMin=8, kZSizeMax=12;
constexpr uint32_t kFocusBlinkMinMs=5000, kFocusBlinkMaxMs=8000;
constexpr int16_t  kWorkLidTop=8, kWorkLidBot=22;   // downcast aperture
constexpr uint32_t kPopMs=1500;             // waiting attention pop

// ---- CLAWD working top sparkle ----
constexpr int16_t  kWSpkCx=120, kWSpkCy=44, kWSpkCore=6, kWSpkHalfW=3;
constexpr int16_t  kWSpkLmin=16, kWSpkLmax=20;   // pulse range
constexpr float    kWSpkPulseRate=0.00449f;      // rad/ms (~1.4s)
constexpr float    kWSpkRotStep=1.2f;            // deg/frame
constexpr int16_t  kWSpkEraseR=25;               // 20+3+2

// ---- CLAWD waiting bang + ring ----
constexpr int16_t  kBangBarW=10, kBangBarH=34, kBangDotH=10, kBangGap=6; // dot at topY+40
constexpr int16_t  kWaitBangBaseY=30, kWaitBangAmp=8;  // topY 30..38
constexpr float    kWaitBangRate=0.00599f;             // ~1.05s
constexpr int16_t  kRingR0=103, kRingR1=107;
constexpr int16_t  kCrestArc=40, kCrestAdv=12;         // deg
constexpr uint32_t kCrestStepMs=60;

// ---- SPARKLE hero (working) ----
constexpr int16_t  kHeroCore=14, kHeroHalfW=7;
constexpr int16_t  kHeroLmin=76, kHeroLmax=92;
constexpr float    kHeroPulseRate=0.00393f;   // base pulse ~1.6s
constexpr float    kHeroShimmerRate=0.004f;   // per-ray shimmer
constexpr float    kHeroRotStep=0.8f;         // deg/frame
constexpr int16_t  kHeroEraseR=101;           // 92+7+2

// ---- SPARKLE waiting ----
constexpr int16_t  kWaitSpkCore=16, kWaitSpkHalfW=8;
constexpr int16_t  kBeatLmin=64, kBeatLmax=78;
constexpr uint32_t kBeatMs=1400, kBeatUpMs=120, kBeatDownMs=200;
constexpr int16_t  kWaitSpkEraseR=88;         // 78+8+2
constexpr int16_t  kSWaitBangBaseY=14, kSWaitBangAmp=4;  // topY 14..18 (rim-bound!)
constexpr float    kSWaitBangRate=0.006f;

// ---- SPARKLE idle ----
constexpr int16_t  kStarCx=120, kStarCy=78, kStarCore=5, kStarHalfW=2;
constexpr int16_t  kStarLmin=11, kStarLmax=15;
constexpr float    kStarRate=0.00224f;        // ~2.8s
constexpr int16_t  kStarEraseR=19;            // 15+2+2
constexpr int16_t  kSIdleEyeYT=104;           // eyes below star
constexpr uint32_t kSIdleBlinkMinMs=3500, kSIdleBlinkMaxMs=6000;

// ---- sparkle ray base angles ----
// baseAngleDeg[i] = 45 * i, i in 0..7  (compute inline, no array needed)
constexpr int8_t   kSpkRays=8;
```

---

## 8. WHY THIS FIXES THE VERDICT

| Criticism | Fix in this spec |
|---|---|
| Idle == plain swaying eyes | Removed horizontal swing. Idle now = vertical breathing (±3px sine) + randomized varied blinks + occasional one-shot glance + **DOZE** (half-lids + floating cream `z`) after 12 s. Unique silhouette, clearly "a resting/sleeping companion." |
| Expressions lack character | WORKING = downcast narrowed eyes + on-brand pulsing green Claude sparkle (replaces generic spinner). WAITING = wide up-looking eyes with white glint + bouncing amber `!` + sweeping bright ring crest (no strobe). |
| Generic green spinner | Replaced with the pulsing/rotating Claude sparkle in both styles. |
| SPARKLE underspecified | Full rotation+per-ray shimmer (working), freeze→amber→grow-beat→`!` (waiting), shrink-to-calm-star + returning slow-blink eyes (idle). One-disc erase per frame. |
| Flicker | `fillScreen` only in `redrawBase` (state OR style OR bg change). Every animated element has an explicit erase-then-draw with proven-disjoint boxes. |

All animation extremes proven ≤ r=110. Accents exact RGB565. Helpers = 5 functions + a switch on style and state — no over-abstraction.

---

## 9. 落地风险清单（synth workflow 的 12 条 risks，实现/review 时逐条核对）

1. Background is user-mutable (state.bg_color_565). The 'glance lens' proposal's background-swap for WAITING is unsafe and was rejected; all erases must pass the runtime bg_color_ member, not a literal. Implementer must NOT hardcode 0xD880 in any erase.

2. redrawBase currently triggers only on status or bg change (applyState in claude_status.cpp lines 154-164). It MUST be extended to also compare cc_style and redraw on style change, or a CLAWD<->SPARKLE switch will render garbage over the old static layer. The spec assumes this edit.

3. SPARKLE-WAIT bang top at topY=14 sits at r=106 — only 4px of rim margin. If the bang bar width or anti-aliasing of drawWideLine/font pushes pixels outward, it can clip the rim. Keep topY>=14 and verify on hardware; the bang is fillRect (no AA) so this should hold, but it is the tightest constraint in the whole spec.

4. CLAWD-WORK top sparkle disc (center 120,44 r=25) bottoms at y=69 vs eye top y=70 — only 1px gap. Depends on L_max capped at 20 (not 22). If anyone bumps the working sparkle size, the disc will erase the top row of the eyes each frame and the eyes will flicker. This coupling must be commented in code.

5. fillArc winding/degree direction in LovyanGFX: the ring crest sweep and erase assume 0deg=+X, clockwise increasing, matching the existing tickWaiting fillArc usage. The trailing-12deg repaint must use the SAME angle convention and overlap the previous crest edge exactly, or a thin stale arc pixel band will persist. Test the wrap at 360->0.

6. Per-ray sparkle erase via single bounding fillCircle assumes the sparkle center never moves within a state (it doesn't here). The disc radius must track the ACTUAL max L drawn last frame (eraseR_ stored from drawSparkle return), not the nominal max, so a mid-ramp grow-beat frame still erases fully. Using a fixed constant eraseR is fine ONLY if it is the absolute max (the constants given are absolute maxes, so a fixed value is safe and simpler).

7. drawWideLine is used only for the doze z diagonal; confirm it exists in the M5GFX/LovyanGFX version pulled by the M5Dial lib (the task lists it as available, but verify the exact signature drawWideLine(x0,y0,x1,y1,width,color)).

8. Frame cadence: tick() is called from the main loop every M5Dial.update() iteration, not on a fixed 33ms timer. The spec's per-frame deg/ms rates assume ~30fps. If the loop runs faster (e.g. 60fps when little else is happening) rotation/pulse will run ~2x fast. Implementer should gate animation steps on elapsed-time accumulators (now - lastStep_ >= kFrameMs), not assume one tick == one 33ms frame.

9. esp_random() for blink/glance gaps is fine but must be reseeded per-event (store nextEventMs_ = now + random_in_range); do not call random every tick or the gap will never resolve.

10. The CLAWD-WAIT 'attention pop' (4.3c) is marked optional and slightly raises eyes to topY=62; its clear box kWaitClrY=61 already covers it. If implemented, ensure the pop redraw of the wide eyes does not race with the bang erase (different x columns, so safe, but both run same frame).

11. Speed scaling (speedScaleNum) currently only sensibly applies to IDLE; the spec says keep it on idle blink/glance only. Applying it to WORKING/WAITING/SPARKLE timings would distort state semantics — implementer should NOT thread speed_ into those.

12. Color kGreenHi=0x9FF3 and kCream=0xFF38 are derived values; verify they read as intended (light green / warm cream) on the actual GC9A01 panel, which can render greens/whites differently than a desktop preview. Cheap to tweak, but confirm on device.

---

> 来源：本规格由 2026-05-31 会话 `dbea38f2` 的设计合成 workflow `w8dlxei1y`（4 agent + judge，210k tokens）产出。该会话在 spec 返回后撞 API 524 + /compact 卡死，spec 未被消费。本文件为恢复留档。
