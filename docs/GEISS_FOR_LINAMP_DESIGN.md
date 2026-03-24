# Geiss-Style Visualization for Linamp — Implementation Design

> Complete implementation plan for bringing Geiss-style audio-reactive visuals
> into Linamp's Qt-based architecture on Raspberry Pi.

---

## 1. Architecture

### 1.1 Pipeline Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│ AUDIO THREAD (existing)                                                 │
│                                                                         │
│  AudioSource::dataEmitted(QByteArray pcm, QAudioFormat fmt)             │
│       │                                                                 │
│       ▼                                                                 │
│  AudioSourceCoordinator ──signal──→ GeissWidget::feedAudio()            │
└─────────────────────────────────────────────────────────────────────────┘
                                           │
                                           ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ UI THREAD — GeissWidget (QWidget subclass, ~30 FPS paint timer)         │
│                                                                         │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐                │
│  │ AudioAnalyzer │   │ EffectEngine │   │  WarpEngine  │                │
│  │              │   │              │   │              │                │
│  │ • waveform   │   │ • effect[]   │   │ • DATA_FX    │                │
│  │ • FFT bands  │──▶│ • render()   │──▶│ • warp()     │                │
│  │ • beat detect│   │   into FB_A  │   │   FB_A→FB_B  │                │
│  │ • vol track  │   │              │   │              │                │
│  └──────────────┘   └──────────────┘   └──────────────┘                │
│                                              │                          │
│                                              ▼                          │
│  ┌──────────────────────────────────────────────────┐                   │
│  │ paintEvent(): QPainter::drawImage(FB_B, scaled)  │                   │
│  └──────────────────────────────────────────────────┘                   │
│       │                                                                 │
│  swap FB_A ↔ FB_B (ping-pong)                                          │
└─────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────┐
│ WORKER THREAD — WarpMapGenerator (QThread)                              │
│                                                                         │
│  GenerateChunkOfNewMap() — builds DATA_FX2 row-by-row                   │
│  Signals UI thread when complete                                        │
│  UI thread swaps DATA_FX ↔ DATA_FX2 on next beat                       │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Component Responsibilities

| Component | Thread | Role |
|-----------|--------|------|
| `GeissWidget` | UI | QWidget hosting the visualizer. Owns the framebuffers, runs the frame timer, paints to screen. |
| `AudioAnalyzer` | UI | Processes raw PCM into waveform buffer, FFT bands, volume metrics, beat detection. Cheap at 512 samples. |
| `EffectEngine` | UI | Manages active effects, calls each effect's render function to seed pixels into the framebuffer. |
| `WarpEngine` | UI | Applies the active warp map (bilinear interpolation loop) — the core inner loop. |
| `WarpMapGenerator` | Worker | Generates next warp map row-by-row in background. Emits `mapReady()` when done. |

### 1.3 Integration with Linamp

The visualizer plugs into the existing architecture at two points:

**Audio input**: Connect to `AudioSourceCoordinator`'s routing, same as `SpectrumWidget`:
```cpp
// In MainWindow or coordinator setup:
connect(coordinator, &AudioSourceCoordinator::dataEmitted,
        geissWidget, &GeissWidget::feedAudio);
```

**View integration**: Add as a new view in the `QStackedLayout`, or replace the screensaver:
```cpp
// Option A: New view index
viewStack->addWidget(geissWidget);  // index 4

// Option B: Replace screensaver (index 3)
viewStack->removeWidget(screenSaverView);
viewStack->insertWidget(3, geissWidget);
```

The existing screensaver idle-timeout logic in MainWindow can trigger switching to the Geiss view when music plays and the user is idle.

---

## 2. Warp System Design

### 2.1 Framebuffer Setup

Two `QImage` objects in `Format_RGB32` (32-bit, 0xAARRGGBB):

```cpp
static constexpr int FB_W = 320;  // Match Linamp base resolution
static constexpr int FB_H = 100;  // Match Linamp base resolution

QImage m_fb[2];  // Ping-pong framebuffers
int m_activeFB = 0;  // Index of the "current" framebuffer (warp source)

// Initialize:
m_fb[0] = QImage(FB_W, FB_H, QImage::Format_RGB32);
m_fb[1] = QImage(FB_W, FB_H, QImage::Format_RGB32);
m_fb[0].fill(Qt::black);
m_fb[1].fill(Qt::black);
```

Direct pixel access: `uint32_t* pixels = reinterpret_cast<uint32_t*>(m_fb[i].bits());`

### 2.2 Warp Map Structure

Faithful to Geiss, but adapted for 32-bit RGBA pixels:

```cpp
struct WarpEntry {
    uint8_t w[4];    // Bilinear weights: TL, TR, BL, BR (sum ≈ 253)
    int32_t offset;  // Relative pixel offset from previous entry's source
};
// 8 bytes per pixel, same as original Geiss

std::vector<WarpEntry> m_warpMap;     // Active map (FB_W × FB_H entries)
std::vector<WarpEntry> m_warpMapNext; // Generating in background
```

### 2.3 Warp Inner Loop (CPU, C++)

The core rendering operation. For each destination pixel, sample 4 source pixels with bilinear weights:

```cpp
void WarpEngine::warp(const uint32_t* src, uint32_t* dst,
                      const WarpEntry* map, int numPixels, int stride)
{
    const uint8_t* srcBytes = reinterpret_cast<const uint8_t*>(src);
    uint8_t* dstBytes = reinterpret_cast<uint8_t*>(dst);
    int srcOffset = 0;  // Cumulative source byte offset
    int dstIdx = 0;

    // Error diffusion accumulators (one per channel)
    uint16_t errR = 0, errG = 0, errB = 0;

    for (int i = 0; i < numPixels; i++) {
        srcOffset += map[i].offset;
        const uint8_t* p = srcBytes + srcOffset;
        int stride4 = stride * 4;  // Byte stride for next row

        uint8_t w0 = map[i].w[0];  // Top-left weight
        uint8_t w1 = map[i].w[1];  // Top-right weight
        uint8_t w2 = map[i].w[2];  // Bottom-left weight
        uint8_t w3 = map[i].w[3];  // Bottom-right weight

        // Red channel + error diffusion
        uint16_t r = p[0]*w0 + p[4]*w1 + p[stride4]*w2 + p[stride4+4]*w3 + errR;
        errR = r & 0xFF;
        dstBytes[dstIdx + 0] = r >> 8;

        // Green channel
        uint16_t g = p[1]*w0 + p[5]*w1 + p[stride4+1]*w2 + p[stride4+5]*w3 + errG;
        errG = g & 0xFF;
        dstBytes[dstIdx + 1] = g >> 8;

        // Blue channel
        uint16_t b = p[2]*w0 + p[6]*w1 + p[stride4+2]*w2 + p[stride4+6]*w3 + errB;
        errB = b & 0xFF;
        dstBytes[dstIdx + 2] = b >> 8;

        dstIdx += 4;  // Next pixel (skip alpha byte)
    }
}
```

**Performance estimate**: 320×100 = 32,000 pixels. Each pixel: ~15 multiplies + adds + 3 memory reads. At 1.5 GHz ARM with NEON, this is well under 1ms per frame. The entire warp fits comfortably in L1 cache.

### 2.4 Warp Map Generation

On a background QThread, `WarpMapGenerator` builds the next map:

```cpp
class WarpMapGenerator : public QThread {
    Q_OBJECT
public:
    void startNewMap(const WarpParams& params);
signals:
    void mapReady(std::vector<WarpEntry> newMap);
protected:
    void run() override;
};
```

The generation math follows Geiss's approach (see GEISS_ANALYSIS.md §2.5):

```cpp
void WarpMapGenerator::run() {
    std::vector<WarpEntry> map(FB_W * FB_H);
    int prevOffset = 0;

    for (int y = 0; y < FB_H; y++) {
        for (int x = 0; x < FB_W; x++) {
            // 1. Offset from warp center
            float dx = x - m_params.centerX;
            float dy = y - m_params.centerY;

            // 2. Mode-specific scale
            float scale = computeScale(dx, dy, m_params);

            // 3. Rotation
            float nx = dx * m_params.cosT - dy * m_params.sinT;
            float ny = dx * m_params.sinT + dy * m_params.cosT;

            // 4. Scale + translate
            float srcX = nx * scale + m_params.centerX;
            float srcY = ny * scale + m_params.centerY;

            // 5. Damping (blend toward identity)
            srcX = x * (1.0f - m_params.damping) + srcX * m_params.damping;
            srcY = y * (1.0f - m_params.damping) + srcY * m_params.damping;

            // 6. Clamp to valid range
            srcX = std::clamp(srcX, 1.0f, (float)(FB_W - 2));
            srcY = std::clamp(srcY, 1.0f, (float)(FB_H - 2));

            // 7. Bilinear weights
            int ix = (int)srcX;
            int iy = (int)srcY;
            float fx = srcX - ix;
            float fy = srcY - iy;

            int idx = y * FB_W + x;
            map[idx].w[0] = (uint8_t)((1-fx)*(1-fy) * WEIGHT_SUM);
            map[idx].w[1] = (uint8_t)(   fx *(1-fy) * WEIGHT_SUM);
            map[idx].w[2] = (uint8_t)((1-fx)*   fy  * WEIGHT_SUM);
            map[idx].w[3] = (uint8_t)(   fx *   fy  * WEIGHT_SUM);

            // 8. Relative offset (in bytes, for 32-bit pixels)
            int absOffset = (iy * FB_W + ix) * 4;
            map[idx].offset = absOffset - prevOffset;
            prevOffset = absOffset;
        }
    }

    emit mapReady(std::move(map));
}
```

### 2.5 Dual Map Swap

```cpp
// In GeissWidget, connected to WarpMapGenerator::mapReady:
void GeissWidget::onMapReady(std::vector<WarpEntry> newMap) {
    m_nextMapReady = true;
    m_warpMapNext = std::move(newMap);
}

// In the frame loop, after beat detection:
if (m_nextMapReady && (m_beatDetected || !m_beatMode || m_forceSwap)) {
    std::swap(m_warpMap, m_warpMapNext);
    m_nextMapReady = false;
    m_forceSwap = false;
    selectNewEffects();
    startGeneratingNextMap();  // Begin background generation
}
```

---

## 3. Effect System Design

### 3.1 Effect Interface

```cpp
class GeissEffect {
public:
    virtual ~GeissEffect() = default;

    // Render this effect into the framebuffer.
    // Called once per frame, before the warp pass.
    virtual void render(uint32_t* fb, int width, int height,
                       const AudioAnalyzer& audio, float frame) = 0;

    // Optional: called when effect is activated/deactivated
    virtual void activate() {}
    virtual void deactivate() {}
};
```

All effects receive:
- Direct pointer to the framebuffer pixels (can read and write)
- Framebuffer dimensions
- Audio analysis data (waveform, FFT, volume, beat state)
- Frame counter (for time-based animation)

### 3.2 Pixel Write Helpers

```cpp
namespace GeissPixel {
    // Additive write: max(existing, value) — never darkens
    inline void additive(uint32_t* fb, int offset, uint8_t r, uint8_t g, uint8_t b) {
        uint8_t* p = reinterpret_cast<uint8_t*>(&fb[offset]);
        if (r > p[0]) p[0] = r;
        if (g > p[1]) p[1] = g;
        if (b > p[2]) p[2] = b;
    }

    // Accumulative write: saturating add
    inline void accumulate(uint32_t* fb, int offset, uint8_t r, uint8_t g, uint8_t b) {
        uint8_t* p = reinterpret_cast<uint8_t*>(&fb[offset]);
        p[0] = std::min(255, (int)p[0] + r);
        p[1] = std::min(255, (int)p[1] + g);
        p[2] = std::min(255, (int)p[2] + b);
    }
}
```

### 3.3 Effect Priority List

Prioritized by visual impact and implementation complexity:

| Priority | Effect | Geiss Equivalent | Description | Complexity |
|----------|--------|-------------------|-------------|------------|
| **1** | WaveformEffect | RenderWave mode 1 | Horizontal oscilloscope line | Low |
| **2** | RadialWaveEffect | RenderWave mode 5 | Circular waveform around center | Medium |
| **3** | SolarParticles | Drop_Solar_Particles | Random particle scatter near center | Low |
| **4** | StereoWaveEffect | RenderWave mode 2 | Dual L/R waveform lines | Low |
| **5** | NuclideEffect | RenderDots/Nuclide | Beat-reactive glowing node bursts | Medium |
| **6** | ShadeBobsEffect | ShadeBobs | Orbiting color blobs on Lissajous paths | Medium |
| **7** | SolidLineEffect | Solid_Line + chromatic dispersion | Parametric curve with RGB separation | Medium |
| **8** | ChaserEffect | Two_Chasers | Moving point chasers with trails | Low |
| **9** | GridEffect | Grid | Scrolling grid with breathing brightness | Low |
| **10** | VerticalWaveEffect | RenderWave mode 3 | Waveform mapped to X-axis, scrolling vertically | Low |

### 3.4 Effect Selection

Each warp mode defines per-effect probabilities. On warp map swap:

```cpp
void GeissWidget::selectNewEffects() {
    const ModeInfo& mode = m_modes[m_currentMode];
    for (int i = 0; i < NUM_EFFECTS; i++) {
        int threshold = mode.effectFreq[i];
        if (m_audio.hasSoundData()) threshold = threshold * 7 / 10; // 30% reduction with sound
        m_effectActive[i] = (rand() % 1000) < threshold;
    }
}
```

---

## 4. Audio Analysis Design

### 4.1 AudioAnalyzer Class

```cpp
class AudioAnalyzer {
public:
    // Called from feedAudio() with raw PCM
    void process(const QByteArray& pcm, const QAudioFormat& format);

    // Waveform data (used by waveform effects)
    const float* waveformL() const;     // Left channel, normalized [-1, 1]
    const float* waveformR() const;     // Right channel
    int waveformSize() const;           // Number of samples (≤ 512)

    // Frequency data (used by beat detection, some effects)
    const float* spectrum() const;      // 256 magnitude bins from FFT
    float bandEnergy(Band band) const;  // BASS, MID, TREBLE energy

    // Volume tracking
    float currentVol() const;
    float avgVol() const;           // Medium-term (IIR, ~10 frames)
    float avgVolWide() const;       // Long-term baseline (~25 frames)

    // Beat detection
    bool isBeatMode() const;        // Music has rhythmic structure
    bool isBigBeat() const;         // Current frame is a beat peak
};
```

### 4.2 Audio Data Processing

```cpp
void AudioAnalyzer::process(const QByteArray& pcm, const QAudioFormat& format) {
    // 1. Convert int16 stereo to float L/R buffers
    const int16_t* samples = reinterpret_cast<const int16_t*>(pcm.constData());
    int numFrames = std::min(pcm.size() / 4, WAVEFORM_SIZE);

    for (int i = 0; i < numFrames; i++) {
        m_waveL[i] = samples[i * 2]     / 32768.0f;
        m_waveR[i] = samples[i * 2 + 1] / 32768.0f;
    }

    // 2. Smooth waveform (Geiss-style: 0.8 current + 0.2 next)
    for (int i = 0; i < numFrames - 1; i++) {
        m_smoothWaveL[i] = 0.8f * m_waveL[i] + 0.2f * m_waveL[i + 1];
        m_smoothWaveR[i] = 0.8f * m_waveR[i] + 0.2f * m_waveR[i + 1];
    }

    // 3. FFT for frequency analysis (reuse existing fft.cpp)
    float mono[DFT_SIZE];
    for (int i = 0; i < DFT_SIZE && i < numFrames; i++)
        mono[i] = (m_waveL[i] + m_waveR[i]) * 0.5f;
    calc_freq(mono, m_spectrum);

    // 4. Band energy extraction
    m_bassEnergy = 0; m_midEnergy = 0; m_trebleEnergy = 0;
    for (int i = 1; i < 10; i++)   m_bassEnergy   += m_spectrum[i];
    for (int i = 10; i < 80; i++)  m_midEnergy    += m_spectrum[i];
    for (int i = 80; i < 256; i++) m_trebleEnergy += m_spectrum[i];

    // 5. Volume tracking (IIR filters, matching Geiss)
    float vol = 0;
    for (int i = 0; i < numFrames; i++)
        vol = std::max(vol, std::abs(m_waveL[i]) + std::abs(m_waveR[i]));
    m_currentVol = vol;
    m_avgVol = m_avgVol * 0.85f + vol * 0.15f;
    m_avgVolWide = m_avgVolWide * 0.96f + vol * 0.04f;

    // 6. Beat detection (see §5)
    detectBeat();
}
```

### 4.3 Reuse of Existing FFT

The existing `fft.cpp` (512-point Cooley-Tukey with Hamming window) is directly reusable. No new FFT library needed.

---

## 5. Beat Detection Design

Energy-based, following Geiss's approach:

```cpp
void AudioAnalyzer::detectBeat() {
    // Store volume history
    m_volHistory[m_volHistoryPos] = m_currentVol;
    m_volHistoryPos = (m_volHistoryPos + 1) % VOL_HISTORY_SIZE;  // 120 entries

    // Compute average and variability
    float sum = 0, varSum = 0;
    for (int i = 0; i < VOL_HISTORY_SIZE; i++) sum += m_volHistory[i];
    float avg = sum / VOL_HISTORY_SIZE;

    // Beat strength = sum of volume changes exceeding threshold
    float beatStrength = 0;
    for (int i = 1; i < VOL_HISTORY_SIZE; i++) {
        float delta = std::abs(m_volHistory[i] - m_volHistory[(i-1)]);
        beatStrength += std::max(0.0f, delta - avg * 0.15f);
    }
    if (avg > 0.01f) beatStrength = beatStrength / avg * 10.0f;
    else beatStrength = 0;

    // Hysteresis for beat mode classification
    if (beatStrength > 109) m_beatMode = true;
    if (beatStrength < 71)  m_beatMode = false;

    // Detect individual beat peaks
    float recentMax = 0;
    for (int i = 0; i < 40; i++) {
        int idx = (m_volHistoryPos - 1 - i + VOL_HISTORY_SIZE) % VOL_HISTORY_SIZE;
        recentMax = std::max(recentMax, m_volHistory[idx]);
    }

    float narrowAvg = m_avgVol * 0.3f + m_currentVol * 0.7f;
    m_bigBeat = narrowAvg > recentMax * m_beatThreshold;

    // Adaptive threshold
    if (!m_bigBeat && m_beatMode)
        m_beatThreshold -= 0.002f;  // Slowly lower if no beats detected
    else
        m_beatThreshold = 1.10f;    // Reset on beat
    m_beatThreshold = std::max(m_beatThreshold, 1.02f);
}
```

---

## 6. Color System Design

### 6.1 Direct Color (32-bit)

Unlike Geiss's original 8-bit palette mode, we operate exclusively in 32-bit RGBA. This simplifies the color system — no palette management needed.

**Waveform and effect colors** use Geiss's sinusoidal modulation scheme:

```cpp
struct ColorState {
    float gF[6];  // Slow-varying frequency multipliers, randomized per map

    void randomize() {
        for (int i = 0; i < 6; i++)
            gF[i] = 0.003f + (rand() % 1000) * 0.00001f;
    }

    void getColor(float frame, float base, uint8_t& r, uint8_t& g, uint8_t& b) const {
        float f = 7*sinf(frame*0.007f+29) + 5*cosf(frame*0.0057f+27);
        float cr = 0.58f + 0.21f*sinf(frame*gF[0]+20-f) + 0.21f*cosf(frame*gF[3]+17+f);
        float cg = 0.58f + 0.21f*sinf(frame*gF[1]+42+f) + 0.21f*cosf(frame*gF[4]+26-f);
        float cb = 0.58f + 0.21f*sinf(frame*gF[2]+57-f) + 0.21f*cosf(frame*gF[5]+35+f);
        r = std::min(255, (int)(base * cr));
        g = std::min(255, (int)(base * cg));
        b = std::min(255, (int)(base * cb));
    }
};
```

### 6.2 Chromatic Dispersion

For the Solid Line (BAR) effect, render the line three times, each time writing only one color channel, with temporally-offset animation:

```cpp
void SolidLineEffect::render(uint32_t* fb, int w, int h,
                             const AudioAnalyzer& audio, float frame) {
    float dispersion = 4.0f;
    float speed = 0.6f;

    renderLine(fb, w, h, frame * speed, Channel::RED);
    renderLine(fb, w, h, frame * speed
        + 3.5f * dispersion * (sinf(frame*0.03f+1) + cosf(frame*0.04f+3)),
        Channel::GREEN);
    renderLine(fb, w, h, frame * speed
        - 3.5f * dispersion * (cosf(frame*0.05f+2) + sinf(frame*0.06f+4)),
        Channel::BLUE);
}
```

This creates prismatic rainbow separation — one of Geiss's most visually distinctive features.

---

## 7. Error Diffusion / Grain

### 7.1 Approach: Direct Implementation (CPU)

Since we're using CPU software rendering, we can implement Geiss's error diffusion exactly as the original — carry forward the low 8 bits of the weighted sum to the next pixel.

This is already shown in the warp inner loop (§2.3). The `errR`, `errG`, `errB` accumulators carry the fractional rounding error from pixel to pixel, creating the organic grain texture.

### 7.2 Tuning

The grain is most visible at low brightness. To control its intensity:

```cpp
// Optional: attenuate error carry (0.0 = no grain, 1.0 = full Geiss grain)
static constexpr float GRAIN_AMOUNT = 1.0f;

uint16_t r = /* weighted sum */ + (uint16_t)(errR * GRAIN_AMOUNT);
errR = r & 0xFF;
```

### 7.3 Additional Noise Option

If the error diffusion alone isn't enough grain (possible at very low resolutions), a simple noise overlay can supplement:

```cpp
// Per-pixel noise, applied after warp (optional, Phase D)
uint8_t noise = (rand() & 0x03);  // 0–3 noise
p[0] = std::min(255, (int)p[0] + noise);
```

---

## 8. Performance Budget

### 8.1 Target

| Parameter | Target |
|-----------|--------|
| Framerate | 30 FPS (33ms frame budget) |
| Render resolution | 320 × 100 (32,000 pixels) |
| Display resolution | 1280 × 400 (scaled via QPainter) |
| CPU budget per frame | < 20ms total |

### 8.2 Per-Frame Cost Breakdown (Estimated)

| Operation | Estimated Cost | Notes |
|-----------|---------------|-------|
| Audio analysis + beat detection | < 0.5ms | 512 samples, FFT, IIR filters |
| Effect rendering | < 1ms | Particle draws, line draws, simple math |
| Warp inner loop (32K pixels) | < 3ms | 4 multiplies + 3 channel sums per pixel |
| QImage → QPainter scaling | < 2ms | Qt's bilinear scaling, 320×100 → 1280×400 |
| **Total** | **< 7ms** | **Well within 33ms budget** |

### 8.3 Warp Map Generation (Background Thread)

| Operation | Estimated Cost | Notes |
|-----------|---------------|-------|
| Full map generation | ~5–15ms | 32K pixels × trig per pixel |
| Amortized per frame | < 0.5ms | Spread over ~30 frames |

### 8.4 Resolution Scaling Fallback

If performance is tight on older Pi models:

```cpp
static constexpr int FB_W = 160;  // Half resolution
static constexpr int FB_H = 50;   // 8,000 pixels — trivially fast
```

QPainter scales identically; the visual quality drops slightly but remains authentic (original Geiss at 320×240 looked great, 160×50 would still produce recognizable visuals).

### 8.5 Aspect Ratio Consideration

The 320:100 (3.2:1) aspect ratio is very wide. For warp modes that assume ~4:3, the warp center should be offset and the Y-axis coordinates should be scaled:

```cpp
// In warp map generation, compensate for aspect ratio:
float aspectCompensation = (float)FB_W / FB_H / (4.0f / 3.0f);  // ≈ 2.4
float dy_adjusted = dy * aspectCompensation;  // Stretch Y to match visual expectation
```

Without this, radial modes would appear as horizontal streaks. With it, they'll form proper circles/spirals.

---

## 9. Implementation Phases

### Phase A: Minimal Viable Visualizer

**Goal**: A working widget that shows *something* Geiss-like.

**Deliverables**:
1. `GeissWidget` (QWidget subclass) with two QImage framebuffers
2. Basic warp engine with a single hardcoded warp map (inward zoom + slight rotation)
3. One waveform effect (horizontal oscilloscope, mode 1)
4. `AudioAnalyzer` receiving PCM data and providing waveform + volume
5. Integration into Linamp's view stack
6. 30 FPS timer driving the frame loop
7. Error diffusion in the warp inner loop

**Acceptance**: Running Linamp shows a swirling, warping oscilloscope line that reacts to music.

### Phase B: Multiple Effects and Warp Modes

**Goal**: Visual variety — it should look different every few seconds.

**Deliverables**:
1. `WarpMapGenerator` (QThread) with randomized parameters
2. At least 5 warp modes (inward zoom, outward zoom, sphere, ripples, vortex)
3. Effect engine with 4+ effects (waveform, radial wave, solar particles, nuclide)
4. Effect selection/cycling per warp map generation
5. Warp map swap (no beat sync yet — swap on timer or generation complete)
6. Color animation (sinusoidal RGB modulation)

**Acceptance**: The visualizer cycles through recognizably different visual styles, with multiple waveform shapes and overlay effects.

### Phase C: Beat Detection and Auto-Transitions

**Goal**: Musically synchronized behavior — it should feel like it's listening.

**Deliverables**:
1. Full beat detection (volume history, beat mode classification, big beat detection)
2. Beat-synchronized warp map swaps
3. Dual warp map system (one active, one generating in background)
4. Beat-reactive brightness modulation on waveforms
5. Nuclide effect responding to beat peaks (burst nodes on hits)
6. Adaptive beat threshold

**Acceptance**: Warp transitions happen on musical beats. The visualizer pulses with the rhythm.

### Phase D: Polish

**Goal**: Capture the full Geiss aesthetic.

**Deliverables**:
1. Chromatic dispersion on Solid Line effect
2. ShadeBobs effect (Lissajous orbiting blobs)
3. Grid effect
4. Chaser effects (dotty chaser, two chasers)
5. Center diminishing (prevent center accumulation artifacts)
6. Remaining warp modes (perspective, terra, tunnel, etc.)
7. All 6 waveform modes
8. User-facing controls (if desired): waveform cycle, mode lock, volume sensitivity
9. Noise/grain tuning
10. Screensaver integration: auto-activate on idle during playback

**Acceptance**: Indistinguishable in *feel* from the original Geiss. Smooth, dreamlike, musical.

---

## 10. File and Module Structure

Proposed layout following Linamp's existing conventions:

```
src/
├── view-geiss/                         # New directory for the visualizer
│   ├── geisswidget.h                   # GeissWidget (QWidget, main visualizer)
│   ├── geisswidget.cpp
│   ├── audioanalyzer.h                 # AudioAnalyzer (PCM → waveform/FFT/beat)
│   ├── audioanalyzer.cpp
│   ├── warpengine.h                    # WarpEngine (bilinear warp inner loop)
│   ├── warpengine.cpp
│   ├── warpmapgenerator.h              # WarpMapGenerator (QThread, background map gen)
│   ├── warpmapgenerator.cpp
│   ├── warpparams.h                    # WarpParams struct, WarpMode enum
│   ├── effectengine.h                  # EffectEngine (manages active effects)
│   ├── effectengine.cpp
│   ├── geisseffect.h                   # GeissEffect base class
│   ├── effects/                        # Individual effect implementations
│   │   ├── waveformeffect.h/cpp        # Horizontal/stereo/vertical oscilloscope
│   │   ├── radialwaveeffect.h/cpp      # Circular/rotated waveform
│   │   ├── solarparticles.h/cpp        # Scattered particle effect
│   │   ├── nuclideeffect.h/cpp         # Beat-reactive node bursts
│   │   ├── shadebobseffect.h/cpp       # Orbiting color blobs
│   │   ├── solidlineeffect.h/cpp       # Parametric line + chromatic dispersion
│   │   ├── chasereffect.h/cpp          # Point chasers with trails
│   │   └── grideffect.h/cpp            # Scrolling grid
│   └── colorstate.h                    # ColorState (sinusoidal color animation)
├── shared/
│   ├── fft.h/cpp                       # (existing) — reused by AudioAnalyzer
│   └── scale.h                         # (existing) — reused for UI_SCALE
```

### CMakeLists.txt additions:

```cmake
# Add new source files to qt_add_executable:
qt_add_executable(player WIN32 MACOSX_BUNDLE
    # ... existing sources ...
    src/view-geiss/geisswidget.cpp
    src/view-geiss/audioanalyzer.cpp
    src/view-geiss/warpengine.cpp
    src/view-geiss/warpmapgenerator.cpp
    src/view-geiss/effectengine.cpp
    src/view-geiss/effects/waveformeffect.cpp
    src/view-geiss/effects/radialwaveeffect.cpp
    src/view-geiss/effects/solarparticles.cpp
    src/view-geiss/effects/nuclideeffect.cpp
    src/view-geiss/effects/shadebobseffect.cpp
    src/view-geiss/effects/solidlineeffect.cpp
    src/view-geiss/effects/chasereffect.cpp
    src/view-geiss/effects/grideffect.cpp
)

# No new dependencies needed — pure Qt Widgets + existing libs
```

### Class Hierarchy

```
QWidget
└── GeissWidget
    ├── owns: QImage m_fb[2]               (framebuffers)
    ├── owns: AudioAnalyzer                 (audio processing)
    ├── owns: WarpEngine                    (warp inner loop)
    ├── owns: EffectEngine                  (effect management)
    │         └── owns: GeissEffect*[]      (active effects)
    ├── owns: WarpMapGenerator*             (QThread, background)
    ├── owns: ColorState                    (color animation)
    ├── owns: std::vector<WarpEntry>        (active warp map)
    └── owns: QTimer                        (30 FPS frame timer)

QThread
└── WarpMapGenerator
    └── generates: std::vector<WarpEntry>   (next warp map)

GeissEffect (abstract base)
├── WaveformEffect
├── RadialWaveEffect
├── SolarParticles
├── NuclideEffect
├── ShadeBobsEffect
├── SolidLineEffect
├── ChaserEffect
└── GridEffect
```

---

## Appendix: Aspect Ratio Adaptation Notes

Linamp's 320×100 display (3.2:1) is dramatically different from Geiss's original 320×240 (4:3) or 640×480 targets. Effects that need adaptation:

| Effect/Mode | Issue | Mitigation |
|-------------|-------|------------|
| Radial waveform (mode 5/6) | Elliptical at 3.2:1 | Scale Y coordinates by aspect ratio in polar conversion |
| Solar particles | Circular scatter would be clipped top/bottom | Reduce vertical scatter radius |
| Nuclide nodes | Polygon would extend past top/bottom | Scale polygon radius to min(FB_W, FB_H)/2 |
| Warp modes (sphere, vortex, etc.) | Y-axis would dominate | Use `dy * aspectCompensation` in warp math |
| ShadeBobs orbits | Lissajous paths might exit bounds | Scale orbit radii to fit (FB_W×0.4, FB_H×0.4) |
| Grid | Spacing would differ | Use square spacing based on FB_H, let width extend |
| Warp center | Off-center vertically feels more wrong at this ratio | Constrain gYC to ±(FB_H×0.1) from center |

The wide aspect ratio may actually benefit some modes (tunnels, horizons, terra) that were designed for width.

---

## Appendix B: Implementation Status

All four phases (A through D) have been implemented. This section documents the actual implementation, deviations from the plan, and known issues.

### Implementation Summary

| Component | File(s) | Lines | Status |
|-----------|---------|-------|--------|
| GeissWidget | `geisswidget.h/cpp` | 269 | Complete — ping-pong framebuffers, 30fps timer, warp map swap on beat, QPainter display |
| AudioAnalyzer | `audioanalyzer.h/cpp` | 310 | Complete — PCM decode, smoothing, FFT, band energy, volume IIR, beat detection |
| WarpEngine | `warpengine.h/cpp` | 96 | Complete — bilinear warp with per-channel error diffusion, center diminish |
| WarpMapGenerator | `warpmapgenerator.h/cpp` | 150 | Complete — QThread, 15 warp modes, aspect compensation |
| EffectEngine | `effectengine.h/cpp` | 120 | Complete — probabilistic selection, overlay/waveform/post-warp rendering |
| WaveformEffect | `effects/waveformeffect.h/cpp` | 87 | Complete — horizontal, stereo, vertical modes with hysteresis |
| RadialWaveEffect | `effects/radialwaveeffect.h/cpp` | 68 | Complete — 314-point circular waveform |
| SolarParticles | `effects/solarparticles.h/cpp` | 68 | Complete — volume-reactive scatter with glow |
| NuclideEffect | `effects/nuclideeffect.h/cpp` | 85 | Complete — beat-reactive glowing node bursts |
| ShadeBobsEffect | `effects/shadebobseffect.h/cpp` | 94 | Complete — Lissajous orbiting blobs, aspect-aware |
| SolidLineEffect | `effects/solidlineeffect.h/cpp` | 85 | Complete — parametric line with 3-channel chromatic dispersion |
| ChaserEffect | `effects/chasereffect.h/cpp` | 64 | Complete — 1-2 chasers with 20-point fading trails |
| GridEffect | `effects/grideffect.h/cpp` | 76 | Complete — scrolling grid with 3-frequency breathing brightness |
| **Total** | **29 files** | **1,809** | |

### Integration Points (Modified Existing Files)

| File | Change |
|------|--------|
| `CMakeLists.txt` | Added `view-geiss` include dirs, all 29 new source files |
| `mainwindow.h` | Added `GeissWidget*` member, `geissActive` flag |
| `mainwindow.cpp` | GeissWidget at view stack index 4, audio connected from all 4 sources, idle+playing→Geiss / idle+stopped→screensaver |

### Deviations from Original Design

1. **Waveform rendering path**: The design proposed waveform effects as part of EffectEngine's overlay pipeline. Implementation separates them: `renderOverlays()` runs before warp, `renderWaveform()` runs after warp (matching original Geiss's post-warp waveform rendering), and `renderPostWarp()` handles beat-reactive nuclide effects.

2. **NuclideEffect dual rendering**: NuclideEffect is instantiated both in the overlay system (for silent-mode filler) and as a dedicated post-warp renderer (for beat-reactive bursts). The effect itself gates on `isBigBeat()` or volume threshold, so it self-regulates.

3. **Pixel byte order**: `QImage::Format_RGB32` on little-endian stores bytes as B,G,R,A (not R,G,B,A). All pixel helpers use channel offset constants (`CH_R=2`, `CH_G=1`, `CH_B=0`) to write to the correct bytes.

4. **Qt metatype registration**: `std::vector<WarpEntry>` is passed across thread boundaries via signal/slot, requiring `Q_DECLARE_METATYPE` and `qRegisterMetaType` calls.

### Known Issues and Limitations

**Not yet tested at runtime:**
- No compilation has been performed (project targets Linux/Qt6, development on Windows)
- Warp mode math for all 15 modes has been code-reviewed but not visually validated
- Effect brightness and radius parameters may need tuning on the actual 1280×400 display
- Performance on Raspberry Pi (ARM) has not been measured

**Known technical concerns:**
- `rand()` is called from the WarpMapGenerator worker thread (Fuzzy mode noise). `rand()` is not thread-safe on all platforms. Should migrate to a thread-local random engine.
- The frame timer runs continuously even when the GeissWidget is not visible (hidden in the view stack). Should stop the timer when not the active view and restart on show.
- The initial warp map is generated synchronously in the constructor, duplicating the logic in WarpMapGenerator. Could be unified.

**Not implemented from the original plan:**
- User-facing controls (waveform cycle, mode lock, volume sensitivity) — Phase D item 8
- Waveform modes 4-6 from original Geiss (stereo vertical, rotated radial) — Phase D item 7 (partially: 4 of 6 modes implemented)
- Noise/grain tuning controls — Phase D item 9
