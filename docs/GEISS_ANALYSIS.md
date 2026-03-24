# Geiss Visualizer — Technical Analysis

> Deep analysis of the Geiss 4.30 source code (BSD-3, github.com/geissomatik/geiss).
> Written to inform a faithful spiritual recreation within the Linamp project.

---

## 1. Architecture Overview

### The Frame Loop

Each frame executes this sequence:

```
1. RenderFX()           — Draw overlay effects (particles, chasers, grids, etc.) into VS1
2. Process_Map(VS1,VS2) — Apply precomputed warp map: warp VS1 → VS2 (bilinear interpolation)
3. GetWaveData()        — Read audio samples from Winamp or DirectSound capture
4. RenderDots(VS2)      — Render NUCLIDE effect (exploding beat-reactive nodes)
5. RenderWave(VS2)      — Render audio waveform into the framebuffer
6. Swap VS1 ↔ VS2       — Ping-pong: this frame's output becomes next frame's warp input
7. Merge_All_VS_To_Backbuffer() — Copy internal buffer to DirectDraw back surface
8. Page flip            — Display
```

**Key insight**: Steps 1 and 5 are the only places new pixels enter the system. Everything else is the warp eating those pixels, smearing and swirling them across the screen. The dreamlike quality comes from this feedback loop — fresh pixels are drawn, then warped, then the warped result gets more pixels drawn on it, then warped again, endlessly.

### Data Flow

```
Audio (Winamp/DirectSound)
  │
  ├──→ GetWaveData() → g_SoundBuffer[16384] (int16 stereo PCM)
  │                   → g_fSoundBuffer[16384] (float, smoothed, scaled)
  │
  ├──→ RenderDots()  → volume tracking, beat detection, NUCLIDE particles
  │
  └──→ RenderWave()  → waveform drawn into framebuffer
                        (6 drawing modes: horizontal, stereo, vertical, radial, rotated)

Effects (time-driven, some audio-gated)
  │
  └──→ RenderFX()    → ShadeBobs, Chasers, Solid_Line, Solar, Grid, etc.
                        drawn into VS1 before warp

Warp Engine
  │
  ├── DATA_FX[]      — active precomputed warp map (8 bytes/pixel)
  ├── DATA_FX2[]     — next warp map, generating row-by-row in background
  └── Process_Map()  — bilinear interpolation inner loop (x86 ASM / MMX / C fallback)

Display
  │
  └── Merge_All_VS_To_Backbuffer() → DirectDraw page flip
```

### Framebuffer Layout

- **VS1** / **VS2**: Two ping-pong buffers, each `FXW × FXH` pixels
- **8-bit mode**: 1 byte/pixel, palette-mapped via `ape[]`/`ape2[]` lookup tables
- **32-bit mode**: 4 bytes/pixel (RGBX), direct color — R at offset 0, G at +1, B at +2
- **FXW/FXH**: Render resolution (e.g. 320×240, 640×480, up to 1920×1080+)
- **FX_YCUT**: Rows skipped at top/bottom (typically ~90 rows) — reduces warp computation without visible loss because edges are hidden by the display frame

---

## 2. Warp Map System

This is the heart of Geiss. Every frame, the entire framebuffer is remapped through a precomputed warp that tells each destination pixel where to sample from in the source.

### 2.1 DATA_FX Format — 8 Bytes Per Pixel

```
Bytes 0-3:  Four bilinear weights (uint8 each), one per corner of a 2×2 source neighborhood
            w_topLeft, w_topRight, w_bottomLeft, w_bottomRight
            Weights sum to ~253 (weightsum constant), not 256

Bytes 4-7:  Signed 32-bit relative offset to the source pixel
            For 8-bit: offset in bytes (= pixel offset)
            For 32-bit: offset in bytes (= pixel offset × 4)
```

The **relative offset** is the distance from the previous pixel's source address to this pixel's source address. This saves memory bandwidth — instead of storing absolute coordinates, only deltas are needed. The source pointer (EDI in the assembly) is adjusted cumulatively.

### 2.2 Bilinear Interpolation

For each destination pixel, four source pixels in a 2×2 grid are sampled and blended:

```
output = (w0 × src[offset] + w1 × src[offset+1] +
          w2 × src[offset+FXW] + w3 × src[offset+FXW+1]) >> 8
```

The right-shift by 8 (division by 256) is implicit — the weights are pre-scaled so their sum ≈ 253, and the high byte of the 16-bit accumulator (DH register) naturally holds the result.

**Why 253, not 256?** This slight deficit means each warp pass slightly dims the image (by ~1.2%). This is the **natural decay** that prevents the feedback loop from saturating to white. Without it, any pixel energy would persist forever. The specific value of 253 was tuned to give Geiss its characteristic slow, dreamy fade.

### 2.3 Error Diffusion (The Grain Trick)

In the MMX 32-bit path, the lowest 8 bits of the weighted sum are **not discarded** — they're carried forward to the next pixel via the MM6 register:

```asm
paddw   mm0, mm6        ; add previous pixel's rounding error
movq    mm6, mm0        ; save full sum for later
psrlw   mm0, 8          ; shift right → final pixel value
psllw   mm6, 8          ; isolate low bits
packuswb mm0, mm0       ; pack to bytes, write to destination
psrlw   mm6, 8          ; prepare carry for next pixel
```

This implements **1D error diffusion dithering**. The effect is subtle but critical: it produces a fine-grained texture in the warped image that prevents banding artifacts and gives Geiss its organic, film-grain quality. Without it, the image would look clean but sterile.

In the scalar 8-bit path, the same effect happens implicitly through the DX register accumulation (DH is the output, DL carries the error).

### 2.4 Dual Warp Map System

Two warp maps exist simultaneously:

| Map | Purpose |
|-----|---------|
| `DATA_FX` | Active — applied to every frame via Process_Map() |
| `DATA_FX2` | Generating — built row-by-row in the background by GenerateChunkOfNewMap() |

**Generation is progressive**: Each frame, GenerateChunkOfNewMap() computes a fraction of the next map proportional to `frames_elapsed / frames_til_auto_switch`. This spreads the CPU cost across the map's entire lifetime.

**Swap trigger**: When DATA_FX2 is complete, Geiss waits for a **beat** (if beat detection is active) then swaps:

```c
// Swap condition:
if (map_complete && (g_rush_map || !bBeatMode || bBigBeat)) {
    temp = DATA_FX;
    DATA_FX = DATA_FX2;      // new map becomes active
    DATA_FX2 = temp;          // old map recycled for next generation
    FX_Random_Palette();      // new palette too
    // begin generating next map...
}
```

The beat-synchronized swap is what gives Geiss its musical structure — the visual character shifts on the downbeat.

### 2.5 Warp Modes (25 Modes)

GenerateChunkOfNewMap() computes warp coordinates per-pixel using a mode-specific transformation. The core formula for most modes:

```c
// 1. Offset from warp center
dx = x - gXC;  dy = y - gYC;

// 2. Mode-specific scale computation (see table below)
scale = f(dx, dy, mode_params);

// 3. Rotation
newx = dx*cos(turn) - dy*sin(turn);
newy = dx*sin(turn) + dy*cos(turn);

// 4. Scale + translate back to center
newx = newx * scale + gXC;
newy = newy * scale + gYC;

// 5. Damping (blend with identity)
newx = x*(1-damping) + newx*damping;
newy = y*(1-damping) + newy*damping;

// 6. Compute bilinear weights from fractional part
fx = newx - floor(newx);  fy = newy - floor(newy);
w0 = (1-fx)*(1-fy)*weightsum;  w1 = fx*(1-fy)*weightsum;
w2 = (1-fx)*fy*weightsum;      w3 = fx*fy*weightsum;
```

**Mode catalog:**

| # | Name | Scale Function | Visual Character |
|---|------|---------------|-----------------|
| 1 | Inward zoom | 0.85–0.985 uniform | Classic zoom-in with rotation |
| 2 | Outward zoom | 1.00–1.02 uniform | Expanding, dissipating |
| 3 | Terra | Y-dependent: 0.95 − y×0.0005 | Ground/horizon perspective |
| 4 | Sphere | r-dependent: 0.9 + r×0.00035 | Fisheye/spherical distortion |
| 5 | Perspective | r-dependent: f2 − f1×√√r | Deep tunnel/perspective |
| 6 | Vortex | N/A — custom vector field | 10 vortex centers with radial/tangential forces |
| 7 | Fuzzy | r-dependent + noise | Blurry, organic distortion |
| 8 | Ripples | sin(√r × f1) | Concentric wave rings |
| 9 | Flower | r-dependent with feedback | Petal-like spreading |
| 10 | Y-perspective | y-dependent: 1.03 + 0.03×(y/H) | Vertical vanishing point |
| 11 | Spin/Blur | 1.008–1.016, fast rotation | Rapid spinning expansion |
| 12 | H-split | √\|x\| transform | Mirror-split along vertical axis |
| 13 | Black hole | 1.04 − r×√r×0.000035 | Gravitational lensing effect |
| 14 | Y-horizon | cos(y×12) | Sinusoidal horizon bands |
| 15 | Petal rings | sin(atan2×petals) | Radial symmetry (2–6 fold) |
| 16 | Crystal ball | r²-dependent | Quadratic lens distortion |
| 17 | H-tunnel | −y²×0.40 | Horizontal tunnel |
| 18 | V-tunnel | −x²×0.40 | Vertical tunnel |
| 19 | Sucking vortex | 1.04 − 0.25×√(x²+y²) | Inward pull |
| 20 | Terra 2 | 1.15 − 0.20×√(y+1.4) | Y-based terrain variant |
| 21 | Diced cube | Quantized axes | Blocky, pixelated warping |
| 22 | Phonic rings | Quantized radius | Concentric ring banding |
| 23 | Phonic fast | (int(r×20)%4)×0.12 | Modulo ring pattern |
| 24 | Fast swirl | 0.96, turn=0.05 | Smooth fast rotation |
| 25 | Inverse zoom | 3/(3+√(x²+y²)) | 1/r fisheye |

**Randomized parameters** per mode:
- **turn1/turn2**: Rotation angle per frame (radians, typically 0.007–0.18)
- **scale1/scale2**: Zoom factor (< 1 = inward, > 1 = outward)
- **damping**: 0.5–1.0, blends warp with identity (lower = subtler warp)
- **gXC/gYC**: Warp center, randomized ±30/15 px from screen center
- **f1–f4**: Mode-specific shape parameters

**Rotation dithering**: Some modes alternate between turn1/scale1 (even pixels) and turn2/scale2 (odd pixels). This creates a subtle per-pixel variation that acts as spatial anti-aliasing for the warp.

---

## 3. Effect Catalog

Effects are overlay animations drawn into the framebuffer before warping. They provide the "seed" pixels that the warp then smears. Each effect is independently toggled with probabilistic selection per warp map generation.

### 3.1 Waveform Effects (Audio-Reactive)

These are the primary audio visualizations, drawn by RenderWave() into the warped buffer after Process_Map().

| Wave # | Mode | Description |
|--------|------|-------------|
| 1 | Horizontal oscilloscope | Single waveform line, L/R alternating samples, heavy smoothing (90% hysteresis) |
| 2 | Stereo horizontal | Two waveform lines — L at 40% height, R at 60% height |
| 3 | Vertical oscilloscope | Waveform amplitude maps to X position, samples progress vertically |
| 4 | Stereo vertical | Two vertical waveforms, L and R separated horizontally |
| 5 | Radial/circular | 314 samples mapped in polar coordinates around center (radius = base + amplitude) |
| 6 | Rotated radial | Like #5, but L/R channels treated as 2D vectors with animated rotation matrix |

**Color in 32-bit mode**: Each channel (R, G, B) gets independent sinusoidal modulation:
```c
r = base * 1.07 * (1 + 0.3*sin(gF[0]*frame + φ₁)) * (1 + 0.20*cos(gF[3]*frame + φ₂))
```
where `gF[0..5]` are slow-varying frequency multipliers and `base` is audio-volume-proportional brightness.

**Drawing technique**: Pixels are written as `max(existing, color)` — additive-only blending that never darkens. This lets waveforms glow on top of existing warped imagery.

### 3.2 NUCLIDE (Beat-Reactive Particle Bursts)

Drawn by RenderDots() after waveforms. On volume spikes (`vol > avg_vol × 1.25`):
- Spawns 3–7 glowing nodes arranged in a polygon around the center
- Each node is a soft circle (radius 2–10 px) with `(r − distance) × 25` brightness falloff
- Radius scales with beat intensity: `r = 3 + 32 × (vol/avg_vol − 1.25)`
- In 32-bit mode, nodes have animated color (sinusoidal R/G/B independent of audio)

When sound is absent, Nuclide spawns probabilistic filler bursts (1/12 chance per frame).

### 3.3 Overlay Effects (Time-Driven)

These are drawn by RenderFX() before Process_Map().

| Effect | Enum | Description | Audio-Reactive? |
|--------|------|-------------|-----------------|
| **ShadeBobs** | SHADE | Orbiting color blobs on Lissajous paths around center. Each bob writes R/G/B selectively based on sinusoidal color oscillation. Soft glow (center + 4 neighbors). | No — pure animation |
| **Two Chasers** | CHASERS | 1–2 point chasers on parametric circular paths. 20-point trails. FPS-compensated: `time_scale = 30/fps`. Brightens via `255−(255−pixel)×0.6`. | No |
| **Solid Line** | BAR | Animated parametric line (two endpoints on smooth curves). In 32-bit mode, drawn **3 times** at channel-offset positions for **chromatic dispersion** (see §6.2). | No |
| **One Dotty Chaser** | DOTS | Single moving point with 20-element color trail. Per-point animated RGB via sinusoids. | No |
| **Solar Particles** | SOLAR | Random particles scattered in circular region around center. Brightness falls off with distance. Count oscillates with sinusoidal animation. | No |
| **Grid** | GRID | Scrolling grid pattern (spacing FXW/30). Brightness oscillates with 3-frequency sinusoidal breathing. Direction toggles. | No |
| **Nuclide (silent)** | NUCLIDE | See §3.2 — the silent-mode filler bursts. | Inverted |
| **Mode6 Edges** | — | Animated color bars at screen top/bottom edges. Per-pixel noise + color oscillation. | No |

**Effect selection**: Each warp mode defines per-effect probability (out of 1000). On map generation, each effect is independently enabled/disabled by random roll. Sound presence reduces probabilities by 30% (effects compete less with waveforms). Maximum simultaneous effects is capped per mode.

---

## 4. Audio Processing Pipeline

### 4.1 Data Acquisition

**Plugin mode (Winamp)**: Reads from `winampVisModule.waveformData[2][576]` (8-bit unsigned, 2 channels × 576 samples). Also available: `spectrumData[2][576]` for FFT data.

**Screensaver mode**: Direct capture via DirectSound (`pDSCB->Lock/Unlock`) at 44100 Hz, 16-bit stereo PCM into `g_SoundBuffer[16384]`.

**Level triggering** (plugin waveform mode): Before copying samples, scans for a zero-crossing aligned with the previous frame's phase. This synchronizes the waveform display to prevent horizontal jitter — a crucial visual quality feature.

### 4.2 Processing

```c
// 1. Smooth: emphasizes bass, reduces treble jitter
g_fSoundBuffer[i] = 0.8 * g_SoundBuffer[i] + 0.2 * g_SoundBuffer[i+2];

// 2. Scale by volume control and screen-size factor
billy = volscale * (1.0 / (64.0 * (640.0/FXW)));
g_fSoundBuffer[i] *= billy;

// 3. DC offset removal: center waveform at zero
centroid = average(g_fSoundBuffer[channel]);
g_fSoundBuffer[channel] -= centroid;
```

### 4.3 Volume Tracking

```c
current_vol = (high - low) / 256.0;          // Frame's amplitude range
avg_vol_narrow = avg_vol * 0.3 + vol * 0.7;  // Fast-response (~3 frames)
avg_vol        = avg_vol * 0.85 + vol * 0.15; // Medium response (~10 frames)
avg_vol_wide   = avg_vol_wide * 0.96 + vol * 0.04;  // Slow baseline (~25 frames)
```

### 4.4 Beat Detection

Energy-based detection using a 120-sample volume history ring buffer:

```c
// 1. Compute "beat strength" from volume variability
beat_strength = Σ max(0, |vol[i] - vol[i-1]| - avg_vol*0.15) / avg_vol * 10;

// 2. Hysteresis to classify rhythmic vs ambient
if (beat_strength > 109) bBeatMode = true;
if (beat_strength < 71)  bBeatMode = false;

// 3. Detect individual beats as short-term peaks
max_recent = max(past_vol[0..39]);
if (avg_vol_narrow > max_recent * fBigBeatThreshold)
    bBigBeat = true;  // Beat peak!

// 4. Adaptive threshold: slowly lowers if no beats detected
fBigBeatThreshold -= 0.2 / frames_til_auto_switch;
```

**Beat brightness modulation**: When beat mode is active, waveform brightness is scaled by `brite_scale = (current_vol - mean) / (std_dev * 0.5)`, clamped to [0, 1]. This makes waveforms pulse with the rhythm.

---

## 5. Color and Rendering System

### 5.1 8-Bit Palette Mode

The framebuffer stores palette indices (0–255). A 256-entry palette maps indices to RGB colors.

**Palette generation** (`FX_Random_Palette`):

- **Monotone palettes** (1/6 chance): Three transfer curves (one per RGB channel) chosen from:
  - `a²/64` (quadratic — dark-to-bright)
  - `a×2` (linear)
  - `√a × 22.6` (square root — bright-heavy)

  The three curves are shuffled across R, G, B channels (4 permutations).

- **Curve-based palettes** (5/6 chance): Each RGB channel gets an independent curve:

  | Curve # | Formula | Character |
  |---------|---------|-----------|
  | 1 | `√x × 22.6` | Bright, saturated |
  | 2 | `x × 2` | Linear |
  | 3 | `x² / 64` | Dark, slow ramp |
  | 4 | `255 × sin(x/256 × π/2)` | Sinusoidal, soft |
  | 5 | `x × 3.5` | Bright, clipped early |
  | 6 | `1.5^(x/20) − 1` | Exponential, very dark |
  | 7 | `x × 1.5 + 32 + 32 × sin(x × 0.3)` | Undulating, warm |

  Random curve assignment per channel, with a rule that no more than 1 channel may use curve 6 (prevents overly dark palettes).

- **Coarse banding** (random chance): A mid-range of palette indices (7–12 to 17–22) is doubled in brightness, creating a visible contour band in the warped image.

- **Gamma**: Adjustable factor `1 + gamma × 0.01` multiplies all palette values. Increased by 0.3 when sound is absent (brightens silent visuals).

**Palette transition**: Over 18 frames, linearly interpolates between old and new palette:
```c
apetemp[n] = ape_old[n] * (frames_left/18) + ape_new[n] * (1 - frames_left/18);
```

### 5.2 32-Bit Direct Color

No palette — colors are written directly. Waveform and effect colors are computed per-frame using sinusoidal animation:

```c
// Chaser color channels (gF[] are slow-varying frequency offsets)
float f = 7*sin(frame*0.007+29) + 5*cos(frame*0.0057+27);
cr = 0.58 + 0.21*sin(frame*gF[0]+20-f) + 0.21*cos(frame*gF[3]+17+f);
cg = 0.58 + 0.21*sin(frame*gF[1]+42+f) + 0.21*cos(frame*gF[4]+26-f);
cb = 0.58 + 0.21*sin(frame*gF[2]+57-f) + 0.21*cos(frame*gF[5]+35+f);
```

The `gF[]` frequencies are themselves randomized, creating constantly-shifting color relationships.

### 5.3 Gamma Correction in Display Merge

The `Merge_All_VS_To_Backbuffer()` function (video.h) supports an optional gamma-correction step using MMX saturating addition:

```asm
PADDUSB mm0, mm0    ; saturated double = approximate gamma 2.0
```

This brightens mid-tones while clamping highlights, giving a more vivid, contrasty look.

---

## 6. Visual "Secrets" — What Makes Geiss Look Like Geiss

### 6.1 The Decay Loop

The most important single fact: **weightsum = 253, not 256**. Each warp pass multiplies every pixel's brightness by 253/256 ≈ 0.988. Over 60 frames (1 second at 60fps), a pixel decays to 0.988⁶⁰ ≈ 0.48 of its original brightness. This creates:

- Natural trails behind moving waveforms
- Smoke-like dissipation of particles
- The characteristic "glow then fade" of each audio pulse
- Self-regulation that prevents the image from blowing out

### 6.2 Chromatic Dispersion

The Solid_Line (BAR) effect, in 32-bit mode, is drawn **three times** at different time offsets, each targeting a different color channel:

```c
Solid_Line(t,                                               VS1);       // Red
Solid_Line(t + 3.5*dispersion*sin(t*0.03+1)+cos(t*0.04+3), &VS1[1]);  // Green
Solid_Line(t - 3.5*dispersion*cos(t*0.05+2)+sin(t*0.06+4), &VS1[2]);  // Blue
```

The green and blue versions are displaced by sinusoidally-varying amounts (chromatic_dispersion = 4.0). This creates a prismatic rainbow separation that drifts over time — like light refracting through moving glass.

### 6.3 Error Diffusion Grain

Described in §2.3. The carried-forward rounding error creates a persistent, low-level noise texture that:
- Breaks up banding in smooth gradients
- Gives the image an organic, almost film-like grain
- Is most visible in dark areas where few bits of precision remain
- Naturally vanishes in bright areas (where the error is proportionally tiny)

### 6.4 The Feedback Aesthetic

The entire visual is a **feedback loop**: fresh pixels (waveforms, effects) are drawn into a buffer that already contains the warped remnants of all previous frames. This means:

- Every waveform oscillation leaves a smeared trail
- Particles spiral outward (or inward) along the warp field
- Complex interference patterns emerge from simple inputs
- The visual "remembers" its history, creating depth and richness impossible with single-frame rendering

### 6.5 Beat-Synchronized Structure

Warp map swaps are synchronized to detected beats, not to timers. This means:
- Visual character changes feel musically motivated
- During rhythmic sections, the visualizer "breathes" with the music
- During ambient passages, it lingers on a single warp, creating a meditative quality
- The adaptive threshold ensures it eventually transitions even without beats

### 6.6 Warp Center Drift

The warp center (gXC, gYC) is randomized per map, not fixed at screen center. This prevents the visual from becoming a static bullseye — the center of rotation/zoom shifts subtly between maps, keeping the geometry dynamic.

### 6.7 Natural Damping

The `damping` parameter (0.5–1.0) blends the warp with identity (no-op). At damping=0.5, each pixel only moves halfway to its warp destination. This softens extreme modes and creates a dreamy, underwater quality. The damping is also FPS-compensated: `damping *= 30/fps`.

---

## 7. Performance Architecture

### 7.1 Dynamic Code Generation

The warp inner loop is constructed at runtime by stitching together labeled assembly fragments:

1. Each fragment function ends with a `push 0xDEADBEEF` sentinel
2. `ADDBLOCK()` copies bytes until the sentinel into an executable buffer
3. `FixJumpTarget()` patches placeholder `jnz 0` instructions with real loop addresses
4. `ReplaceDWORD()` patches in runtime-known constants (FXW, FXW+1, etc.)
5. Buffer is `VirtualAlloc`'d with `PAGE_EXECUTE_READWRITE` and called directly

This eliminates branch misprediction overhead from conditional code paths and allows the screen dimensions to be embedded as immediate constants.

### 7.2 Three Rendering Paths

| Path | Color Depth | Technique | Dithering |
|------|------------|-----------|-----------|
| Scalar 8-bit | 8 bpp | Dynamic x86 ASM, 2 pixels/iter | Implicit (DX accumulator) |
| Scalar 32-bit | 32 bpp | Dynamic x86 ASM, 3 channels × 4 corners | Implicit |
| MMX 32-bit | 32 bpp | Inline MMX, packed 16-bit multiply | Explicit (MM6 carry) |

### 7.3 Memory Prefetch

Every 256 pixels (or 64 pixels in 8-bit mode), the inner loop bulk-loads upcoming DATA_FX entries into CPU cache:

```asm
mov  ebx, 8*256
prefetch_loop:
    mov  eax, [ebx+esi]
    mov  eax, [ebx+esi+32]
    sub  ebx, 64
    jnz  prefetch_loop
```

Ryan Geiss notes this yielded a **30–40% speed gain** by hiding memory latency.

### 7.4 Background Map Generation

Warp map generation is amortized: `frames_til_auto_switch` frames are budgeted, and each frame generates `1/total_frames` of the next map's rows. With `g_rush_map`, the entire map is computed in one frame (for user-triggered changes).

---

## 8. Key Constants Reference

| Constant | Value | Purpose |
|----------|-------|---------|
| `weightsum` | 253 | Bilinear weight sum (controls natural decay) |
| `NUM_MODES` | 25 | Warp transformation modes |
| `NUM_WAVES` | 5 (+1) | Waveform drawing modes |
| `NUM_EFFECTS` | 8 | Overlay effect slots |
| `PAST_VOL_N` | 30 (×4 for beat history) | Volume history for beat detection |
| `FX_YCUT` | ~90 | Rows skipped at top/bottom of warp computation |
| `iBlendsLeftInPal` | 18 | Frames for palette transition |
| `BUFSIZE` | max(FXW×2, 648+) | Sound buffer size in samples |

---

## 9. Source File Map

| File | Size | Contents |
|------|------|----------|
| `main.cpp` | 353 KB | Main loop, GenerateChunkOfNewMap (25 modes), beat detection, mode/effect/palette orchestration, keyboard handling |
| `proc_map.cpp` | 41 KB | Process_Map assembly (8-bit scalar, 32-bit scalar, 32-bit MMX), dynamic code generation |
| `Effects.h` | 35 KB | ShadeBobs, Solid_Line, Two_Chasers, Dotty_Chaser, Solar_Particles, Grid, Mode6Edges, preset save/load |
| `SOUND.CPP` | 24 KB | GetWaveData, RenderDots (NUCLIDE), RenderWave (6 waveform modes), volume tracking |
| `video.h` | 51 KB | Merge_All_VS_To_Backbuffer (8/16/32-bit display), FX_Random_Palette, CrankPal, PutPalette, gamma |
| `SOUND.H` | 3 KB | Audio globals: g_SoundBuffer, volume averages, beat state |
| `Proc_map.h` | 3 KB | Warp globals: DATA_FX, FXW/FXH, FX_YCUT |
| `DEFINES.H` | 2 KB | Constants: weightsum, NUM_EFFECTS, NUM_MODES, NUM_WAVES |
| `VIS.H` | 4 KB | Winamp plugin interface (waveformData/spectrumData struct) |
