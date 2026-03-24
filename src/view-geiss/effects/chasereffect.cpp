#include "chasereffect.h"
#include "../audioanalyzer.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

ChaserEffect::ChaserEffect()
    : m_numChasers(1 + rand() % 2)
{
    m_color.randomize();
}

void ChaserEffect::activate()
{
    m_color.randomize();
}

void ChaserEffect::render(uint32_t* fb, int width, int height,
                           const AudioAnalyzer& audio, float frame)
{
    (void)audio;

    for (int c = 0; c < m_numChasers; c++) {
        float phase = c * 3.14159f;

        for (int trail = 0; trail < 20; trail++) {
            float t = frame - trail * 0.5f;

            float x = width / 2.0f
                     + width * 0.3f * cosf(t * 0.04f + phase)
                     + width * 0.15f * cosf(t * 0.07f + phase * 2.0f);
            float y = height / 2.0f
                     + height * 0.3f * sinf(t * 0.05f + phase)
                     + height * 0.15f * sinf(t * 0.03f + phase * 3.0f);

            float brightness = 200.0f * (float)(20 - trail) / 20.0f;
            uint8_t r, g, b;
            m_color.getColor(frame, brightness, r, g, b);

            GeissPixel::additiveSafe(fb, (int)x, (int)y, width, height, r, g, b);
        }
    }
}
