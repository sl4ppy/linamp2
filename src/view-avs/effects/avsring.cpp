#include "avsring.h"
#include "avsframebuffer.h"
#include "avsaudiodata.h"
#include <algorithm>
#include <cmath>

AvsRing::AvsRing(uint32_t color, int ringCount, float scaleY)
    : m_color(color), m_ringCount(ringCount), m_scaleY(scaleY)
{
}

void AvsRing::drawCircle(AvsFramebuffer &fb, int cx, int cy, int radius, uint32_t col)
{
    if (radius <= 0)
        return;

    uint32_t *px = fb.pixels();
    int w = fb.width();
    int h = fb.height();

    // Midpoint circle algorithm
    int x = radius;
    int y = 0;
    int err = 1 - radius;

    auto plot = [&](int px_x, int px_y) {
        if (px_x >= 0 && px_x < w && px_y >= 0 && px_y < h)
            px[px_y * w + px_x] = col;
    };

    while (x >= y) {
        plot(cx + x, cy + y);
        plot(cx - x, cy + y);
        plot(cx + x, cy - y);
        plot(cx - x, cy - y);
        plot(cx + y, cy + x);
        plot(cx - y, cy + x);
        plot(cx + y, cy - x);
        plot(cx - y, cy - x);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void AvsRing::render(AvsFramebuffer &fb, const AvsAudioData &audio)
{
    int w = fb.width();
    int h = fb.height();
    int cx = w / 2;
    int cy = h / 2;
    float maxRadius = std::min(cx, cy) * 0.9f;

    for (int i = 0; i < m_ringCount; i++) {
        // Map ring to spectrum bin
        int bin = i * AVS_SPECTRUM_SIZE / m_ringCount;
        float mag = audio.spectrumMono[std::min(bin, AVS_SPECTRUM_SIZE - 1)];

        float baseRadius = (i + 1) * maxRadius / m_ringCount;
        float modRadius = baseRadius + mag * m_scaleY * maxRadius * 0.3f;
        int radius = static_cast<int>(modRadius);

        // Scale brightness by magnitude
        float brightness = 0.3f + mag * 0.7f;
        int cr = static_cast<int>(((m_color >> 16) & 0xFF) * brightness);
        int cg = static_cast<int>(((m_color >> 8) & 0xFF) * brightness);
        int cb = static_cast<int>((m_color & 0xFF) * brightness);
        cr = std::min(cr, 255);
        cg = std::min(cg, 255);
        cb = std::min(cb, 255);
        uint32_t col = 0xFF000000 | (cr << 16) | (cg << 8) | cb;

        drawCircle(fb, cx, cy, radius, col);
    }
}
