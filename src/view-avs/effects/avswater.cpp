#include "avswater.h"
#include "avsframebuffer.h"
#include "avsaudiodata.h"
#include <algorithm>
#include <cstring>

AvsWater::AvsWater(float damping, float beatInjection)
    : m_damping(damping), m_beatInjection(beatInjection),
      m_backBuffer(AVS_FB_WIDTH, AVS_FB_HEIGHT)
{
}

void AvsWater::ensureBuffers(int w, int h)
{
    if (m_width == w && m_height == h)
        return;
    m_width = w;
    m_height = h;
    int size = w * h;
    m_heightCurrent.assign(size, 0.0f);
    m_heightPrevious.assign(size, 0.0f);
}

void AvsWater::render(AvsFramebuffer &fb, const AvsAudioData &audio)
{
    int w = fb.width();
    int h = fb.height();
    ensureBuffers(w, h);

    // Inject energy on beat at random position
    if (audio.isBeat) {
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        int rx = 2 + static_cast<int>(m_rng % (w - 4));
        m_rng ^= m_rng << 13;
        m_rng ^= m_rng >> 17;
        m_rng ^= m_rng << 5;
        int ry = 2 + static_cast<int>(m_rng % (h - 4));

        // Inject a small splash area
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                m_heightCurrent[(ry + dy) * w + (rx + dx)] = m_beatInjection;
            }
        }
    }

    // Wave propagation
    std::swap(m_heightCurrent, m_heightPrevious);

    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int idx = y * w + x;
            float val = (m_heightPrevious[idx - 1] +
                         m_heightPrevious[idx + 1] +
                         m_heightPrevious[idx - w] +
                         m_heightPrevious[idx + w]) * 0.5f
                        - m_heightCurrent[idx];
            val *= m_damping;
            m_heightCurrent[idx] = val;
        }
    }

    // Apply displacement to framebuffer
    m_backBuffer.copyFrom(fb);
    const uint32_t *src = m_backBuffer.pixels();
    uint32_t *dst = fb.pixels();

    for (int y = 1; y < h - 1; y++) {
        for (int x = 1; x < w - 1; x++) {
            int idx = y * w + x;
            int dx = static_cast<int>(m_heightCurrent[idx + 1] - m_heightCurrent[idx - 1]);
            int dy = static_cast<int>(m_heightCurrent[idx + w] - m_heightCurrent[idx - w]);

            int srcX = std::clamp(x + dx, 0, w - 1);
            int srcY = std::clamp(y + dy, 0, h - 1);

            dst[idx] = src[srcY * w + srcX];
        }
    }
}
