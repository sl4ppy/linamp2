#include "avsgrain.h"
#include "avsframebuffer.h"
#include "avsaudiodata.h"
#include <algorithm>
#include <cstdint>

AvsGrain::AvsGrain(int amount, bool beatReactive)
    : m_amount(amount), m_beatReactive(beatReactive)
{
}

uint32_t AvsGrain::xorshift32()
{
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return m_rng;
}

void AvsGrain::render(AvsFramebuffer &fb, const AvsAudioData &audio)
{
    int amt = m_amount;
    if (m_beatReactive)
        amt = static_cast<int>(amt + audio.beatDecay * amt * 2.0f);

    if (amt <= 0)
        return;

    uint32_t *px = fb.pixels();
    int count = fb.pixelCount();
    int range = amt * 2;

    for (int i = 0; i < count; i++) {
        uint32_t p = px[i];
        int r = (p >> 16) & 0xFF;
        int g = (p >> 8) & 0xFF;
        int b = p & 0xFF;

        int nr = static_cast<int>(xorshift32() % range) - amt;
        int ng = static_cast<int>(xorshift32() % range) - amt;
        int nb = static_cast<int>(xorshift32() % range) - amt;

        r = std::clamp(r + nr, 0, 255);
        g = std::clamp(g + ng, 0, 255);
        b = std::clamp(b + nb, 0, 255);

        px[i] = (p & 0xFF000000) | (r << 16) | (g << 8) | b;
    }
}
