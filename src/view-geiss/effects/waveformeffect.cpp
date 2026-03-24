#include "waveformeffect.h"
#include "../audioanalyzer.h"
#include <cmath>
#include <cstring>
#include <algorithm>

WaveformEffect::WaveformEffect(int mode)
    : m_mode(mode)
{
    m_color.randomize();
    std::memset(m_prevZ, 0, sizeof(m_prevZ));
}

void WaveformEffect::activate()
{
    m_color.randomize();
}

void WaveformEffect::render(uint32_t* fb, int width, int height,
                            const AudioAnalyzer& audio, float frame)
{
    float base = std::min(255.0f, audio.currentVol() * 200.0f + audio.avgVol() * 50.0f);
    uint8_t r, g, b;
    m_color.getColor(frame, base, r, g, b);

    int n = audio.waveformSize();
    const float* wL = audio.smoothWaveL();
    const float* wR = audio.smoothWaveR();

    if (m_mode == 0) {
        // Horizontal oscilloscope
        int count = std::min(n, width - 2);
        for (int i = 1; i < count; i++) {
            float z = wL[i] * height * 0.4f + height / 2.0f;
            z = m_prevZ[i] * 0.9f + z * 0.1f;
            m_prevZ[i] = z;
            int y = std::clamp((int)z, 1, height - 2);
            GeissPixel::additiveSafe(fb, i, y, width, height, r, g, b);
        }
    } else if (m_mode == 1) {
        // Stereo mode
        int count = std::min(n, width - 2);
        int yOffL = (int)(height * 0.35f);
        int yOffR = (int)(height * 0.65f);
        for (int i = 1; i < count; i++) {
            float zL = wL[i] * height * 0.2f + yOffL;
            zL = m_prevZ[i] * 0.9f + zL * 0.1f;
            m_prevZ[i] = zL;
            int yL = std::clamp((int)zL, 1, height - 2);
            GeissPixel::additiveSafe(fb, i, yL, width, height, r, g, b);

            float zR = wR[i] * height * 0.2f + yOffR;
            int yR = std::clamp((int)zR, 1, height - 2);
            GeissPixel::additiveSafe(fb, i, yR, width, height, r, g, b);
        }
    } else if (m_mode == 2) {
        // Vertical mode
        int count = std::min(n, height - 2);
        for (int i = 1; i < count; i++) {
            float xf = wL[i] * width * 0.3f + width / 2.0f;
            int x = std::clamp((int)xf, 1, width - 2);
            GeissPixel::additiveSafe(fb, x, i, width, height, r, g, b);
        }
    }
}
