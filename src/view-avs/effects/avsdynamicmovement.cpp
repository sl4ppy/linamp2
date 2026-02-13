#include "avsdynamicmovement.h"
#include "avsframebuffer.h"
#include "avsaudiodata.h"
#include <cmath>

AvsDynamicMovement::AvsDynamicMovement(float baseZoom, float beatMultiplier, bool rotate)
    : m_baseZoom(baseZoom), m_beatMultiplier(beatMultiplier), m_rotate(rotate),
      m_backBuffer(AVS_FB_WIDTH, AVS_FB_HEIGHT)
{
}

void AvsDynamicMovement::render(AvsFramebuffer &fb, const AvsAudioData &audio)
{
    int w = fb.width();
    int h = fb.height();
    float cx = w * 0.5f;
    float cy = h * 0.5f;

    float zoom = 1.0f + m_baseZoom * (1.0f + audio.beatDecay * m_beatMultiplier);
    float rot = m_rotate ? 0.01f * audio.beatDecay : 0.0f;

    float cosR = cosf(rot);
    float sinR = sinf(rot);

    m_backBuffer.copyFrom(fb);
    const uint32_t *src = m_backBuffer.pixels();
    uint32_t *dst = fb.pixels();

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float dx = x - cx;
            float dy = y - cy;

            // Apply zoom and rotation
            float rx = (dx * cosR - dy * sinR) * zoom;
            float ry = (dx * sinR + dy * cosR) * zoom;

            int srcX = static_cast<int>(rx + cx);
            int srcY = static_cast<int>(ry + cy);

            int idx = y * w + x;
            if (srcX >= 0 && srcX < w && srcY >= 0 && srcY < h) {
                dst[idx] = src[srcY * w + srcX];
            } else {
                dst[idx] = 0xFF000000;
            }
        }
    }
}
