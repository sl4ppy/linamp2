#include "radialwaveeffect.h"
#include "../audioanalyzer.h"
#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

RadialWaveEffect::RadialWaveEffect()
{
    m_color.randomize();
    std::memset(m_prevRad, 0, sizeof(m_prevRad));
}

void RadialWaveEffect::activate()
{
    m_color.randomize();
}

void RadialWaveEffect::render(uint32_t* fb, int width, int height,
                              const AudioAnalyzer& audio, float frame)
{
    float base = std::min(255.0f, audio.currentVol() * 200.0f + audio.avgVol() * 50.0f);
    uint8_t r, g, b;
    m_color.getColor(frame, base, r, g, b);

    float baseRad = std::min(width, height) * 0.25f;
    int cx = width / 2;
    int cy = height / 2;

    int waveSize = audio.waveformSize();
    const float* wL = audio.smoothWaveL();

    for (int i = 0; i < 314; i++) {
        float rad = baseRad + wL[i % waveSize] * height * 0.3f;
        rad = m_prevRad[i] * 0.5f + rad * 0.5f;
        m_prevRad[i] = rad;

        float angle = i * (2.0f * (float)M_PI / 314.0f);
        int x = cx + (int)(rad * cosf(angle));
        int y = cy + (int)(rad * sinf(angle));

        GeissPixel::additiveSafe(fb, x, y, width, height, r, g, b);
    }
}
