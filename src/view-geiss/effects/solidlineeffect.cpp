#include "solidlineeffect.h"
#include "../audioanalyzer.h"
#include <cmath>
#include <algorithm>

void SolidLineEffect::render(uint32_t* fb, int width, int height,
                              const AudioAnalyzer& audio, float frame)
{
    (void)audio;

    float dispersion = 4.0f;
    float speed = 0.6f;
    float t = frame * speed;

    renderLine(fb, width, height, t, RED);
    renderLine(fb, width, height,
               t + 3.5f * dispersion * (sinf(frame * 0.03f + 1.0f) + cosf(frame * 0.04f + 3.0f)),
               GREEN);
    renderLine(fb, width, height,
               t - 3.5f * dispersion * (cosf(frame * 0.05f + 2.0f) + sinf(frame * 0.06f + 4.0f)),
               BLUE);
}

void SolidLineEffect::renderLine(uint32_t* fb, int w, int h, float t, Channel channel)
{
    float s = w / 640.0f;

    float x1 = w / 2.0f + s * 64.0f * cosf(t * 0.0613f + 33.0f)
                         + s * 55.0f * cosf(t * 0.0708f + 15.0f);
    float y1 = h / 2.0f + s * 40.0f * sinf(t * 0.0522f + 17.0f)
                         + s * 30.0f * sinf(t * 0.0614f + 9.0f);
    float x2 = w / 2.0f + s * 64.0f * cosf(t * 0.0417f + 55.0f)
                         + s * 55.0f * cosf(t * 0.0319f + 7.0f);
    float y2 = h / 2.0f + s * 40.0f * sinf(t * 0.0728f + 33.0f)
                         + s * 30.0f * sinf(t * 0.0613f + 22.0f);

    for (int i = 0; i < 200; i++) {
        float frac = (float)i / 199.0f;
        int px = (int)(x1 + (x2 - x1) * frac);
        int py = (int)(y1 + (y2 - y1) * frac);

        if (px < 0 || px >= w || py < 0 || py >= h)
            continue;

        int offset = py * w + px;
        uint8_t* p = reinterpret_cast<uint8_t*>(&fb[offset]);

        // Qt Format_RGB32 little-endian: byte[0]=B, byte[1]=G, byte[2]=R
        switch (channel) {
        case RED:
            p[GeissPixel::CH_R] = std::min(255, (int)p[GeissPixel::CH_R] + 16);
            break;
        case GREEN:
            p[GeissPixel::CH_G] = std::min(255, (int)p[GeissPixel::CH_G] + 16);
            break;
        case BLUE:
            p[GeissPixel::CH_B] = std::min(255, (int)p[GeissPixel::CH_B] + 16);
            break;
        case ALL:
            p[GeissPixel::CH_R] = std::min(255, (int)p[GeissPixel::CH_R] + 16);
            p[GeissPixel::CH_G] = std::min(255, (int)p[GeissPixel::CH_G] + 16);
            p[GeissPixel::CH_B] = std::min(255, (int)p[GeissPixel::CH_B] + 16);
            break;
        }
    }
}
