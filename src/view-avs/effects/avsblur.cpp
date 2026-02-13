#include "avsblur.h"
#include "avsaudiodata.h"
#include <algorithm>

AvsBlur::AvsBlur(int passes)
    : passes(passes)
    , m_temp(AVS_FB_WIDTH, AVS_FB_HEIGHT)
{
}

void AvsBlur::render(AvsFramebuffer &fb, const AvsAudioData &)
{
    int w = fb.width();
    int h = fb.height();

    for (int p = 0; p < passes; ++p) {
        m_temp.copyFrom(fb);
        const uint32_t *src = m_temp.pixels();
        uint32_t *dst = fb.pixels();

        // Skip edge pixels (border row/col)
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                uint32_t sumR = 0, sumG = 0, sumB = 0;

                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        uint32_t px = src[(y + dy) * w + (x + dx)];
                        sumR += (px >> 16) & 0xFF;
                        sumG += (px >> 8) & 0xFF;
                        sumB += px & 0xFF;
                    }
                }

                uint32_t r = sumR / 9;
                uint32_t g = sumG / 9;
                uint32_t b = sumB / 9;
                dst[y * w + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
    }
}
