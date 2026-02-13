#include "avsfadeout.h"
#include "avsframebuffer.h"
#include <algorithm>
#include <cstdint>

AvsFadeOut::AvsFadeOut(int speed)
    : m_speed(speed)
{
}

void AvsFadeOut::render(AvsFramebuffer &fb, const AvsAudioData &)
{
    uint32_t *px = fb.pixels();
    int count = fb.pixelCount();
    int spd = m_speed;

    for (int i = 0; i < count; i++) {
        uint32_t p = px[i];
        int a = (p >> 24) & 0xFF;
        int r = (p >> 16) & 0xFF;
        int g = (p >> 8) & 0xFF;
        int b = p & 0xFF;

        r = std::max(0, r - spd);
        g = std::max(0, g - spd);
        b = std::max(0, b - spd);

        px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}
