#include "avsbufferblend.h"
#include "avsaudiodata.h"
#include <algorithm>

AvsBufferBlend::AvsBufferBlend(float blendRatio)
    : blendRatio(blendRatio)
    , m_accumulator(AVS_FB_WIDTH, AVS_FB_HEIGHT)
{
}

void AvsBufferBlend::render(AvsFramebuffer &fb, const AvsAudioData &)
{
    int count = fb.pixelCount();
    uint32_t *cur = fb.pixels();
    uint32_t *acc = m_accumulator.pixels();

    if (m_firstFrame) {
        // Seed accumulator with current frame
        m_accumulator.copyFrom(fb);
        m_firstFrame = false;
        return;
    }

    float oldW = blendRatio;
    float newW = 1.0f - blendRatio;

    for (int i = 0; i < count; ++i) {
        uint32_t c = cur[i];
        uint32_t a = acc[i];

        int r = static_cast<int>(((a >> 16) & 0xFF) * oldW + ((c >> 16) & 0xFF) * newW);
        int g = static_cast<int>(((a >> 8) & 0xFF) * oldW + ((c >> 8) & 0xFF) * newW);
        int b = static_cast<int>((a & 0xFF) * oldW + (c & 0xFF) * newW);

        r = std::clamp(r, 0, 255);
        g = std::clamp(g, 0, 255);
        b = std::clamp(b, 0, 255);

        uint32_t blended = 0xFF000000 | (r << 16) | (g << 8) | b;
        acc[i] = blended;
        cur[i] = blended;
    }
}
