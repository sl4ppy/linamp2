#include "solarparticles.h"
#include "../audioanalyzer.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

SolarParticles::SolarParticles()
{
    m_color.randomize();
}

void SolarParticles::activate()
{
    m_color.randomize();
}

void SolarParticles::render(uint32_t* fb, int width, int height,
                            const AudioAnalyzer& audio, float frame)
{
    int n = 15 + (int)(audio.currentVol() * 30.0f);
    int cx = width / 2;
    int cy = height / 2;
    float maxRad = std::min(width, height) * 0.25f;

    for (int k = 0; k < n; k++) {
        int px, py;
        do {
            px = rand() % 64 - 32;
            py = rand() % 24 - 12;
        } while (sqrtf((float)(px * px + py * py)) > maxRad);

        float dist = sqrtf((float)(px * px + py * py));
        float brightness = (maxRad - dist) / maxRad * 100.0f;

        uint8_t r, g, b;
        m_color.getColor(frame, brightness, r, g, b);

        GeissPixel::accumulateSafe(fb, cx + px, cy + py, width, height, r, g, b);

        // Dimmer neighbors
        uint8_t rd, gd, bd;
        m_color.getColor(frame, brightness * 0.4f, rd, gd, bd);
        GeissPixel::accumulateSafe(fb, cx + px + 1, cy + py, width, height, rd, gd, bd);
        GeissPixel::accumulateSafe(fb, cx + px - 1, cy + py, width, height, rd, gd, bd);
        GeissPixel::accumulateSafe(fb, cx + px, cy + py + 1, width, height, rd, gd, bd);
        GeissPixel::accumulateSafe(fb, cx + px, cy + py - 1, width, height, rd, gd, bd);
    }
}
