#include "grideffect.h"
#include "../audioanalyzer.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

GridEffect::GridEffect()
    : m_dir((rand() % 2) ? 1 : -1)
{
}

void GridEffect::activate()
{
    m_dir = (rand() % 2) ? 1 : -1;
}

void GridEffect::render(uint32_t* fb, int width, int height,
                        const AudioAnalyzer& audio, float frame)
{
    (void)audio;

    int spacing = height / 5;
    if (spacing < 3)
        return;

    int brightness = std::max(0, (int)(65.0f + 45.0f * sinf(frame * 0.04f)
                                       + 35.0f * cosf(frame * 0.027f)
                                       + 25.0f * cosf(frame * 0.013f)));
    brightness = std::min(255, brightness);

    uint8_t c = (uint8_t)brightness;

    int rawOffset = ((int)frame) % spacing;
    if (rawOffset < 0) rawOffset += spacing;
    int offset = (m_dir < 0) ? -rawOffset : rawOffset;

    // Compute starting positions ensuring we cover the full framebuffer
    int yStart = offset % spacing;
    if (yStart < 0) yStart += spacing;

    int xStart = offset % spacing;
    if (xStart < 0) xStart += spacing;

    // Draw horizontal lines
    for (int y = yStart; y < height; y += spacing) {
        for (int x = 0; x < width; x++) {
            GeissPixel::additiveSafe(fb, x, y, width, height, c, c, c);
        }
    }

    // Draw vertical lines
    for (int x = xStart; x < width; x += spacing) {
        for (int y = 0; y < height; y++) {
            GeissPixel::additiveSafe(fb, x, y, width, height, c, c, c);
        }
    }
}
