#include "avsmovement.h"
#include "avsframebuffer.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AvsMovement::AvsMovement(MovementType type)
    : m_type(type), m_backBuffer(AVS_FB_WIDTH, AVS_FB_HEIGHT),
      m_width(AVS_FB_WIDTH), m_height(AVS_FB_HEIGHT)
{
    m_displaceTable.resize(m_width * m_height);
    buildTable();
}

void AvsMovement::setMovementType(MovementType type)
{
    m_type = type;
    buildTable();
}

void AvsMovement::buildTable()
{
    float cx = m_width * 0.5f;
    float cy = m_height * 0.5f;
    float maxDist = sqrtf(cx * cx + cy * cy);

    for (int y = 0; y < m_height; y++) {
        for (int x = 0; x < m_width; x++) {
            float dx = x - cx;
            float dy = y - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx);

            float srcX, srcY;

            switch (m_type) {
            case ZoomIn: {
                // Pull pixels toward center (zoom in effect)
                float scale = 1.02f;
                srcX = cx + dx * scale;
                srcY = cy + dy * scale;
                break;
            }
            case ZoomOut: {
                float scale = 0.98f;
                srcX = cx + dx * scale;
                srcY = cy + dy * scale;
                break;
            }
            case Swirl: {
                float rotAmount = 0.03f * (1.0f - dist / maxDist);
                float newAngle = angle + rotAmount;
                srcX = cx + cosf(newAngle) * dist;
                srcY = cy + sinf(newAngle) * dist;
                break;
            }
            case SwirlOut: {
                float rotAmount = -0.03f * (1.0f - dist / maxDist);
                float scale = 0.98f;
                float newAngle = angle + rotAmount;
                srcX = cx + cosf(newAngle) * dist * scale;
                srcY = cy + sinf(newAngle) * dist * scale;
                break;
            }
            case Tunnel: {
                // Zoom + rotation combined
                float scale = 1.01f;
                float rotAmount = 0.02f;
                float newAngle = angle + rotAmount;
                srcX = cx + cosf(newAngle) * dist * scale;
                srcY = cy + sinf(newAngle) * dist * scale;
                break;
            }
            case SuckIn: {
                // Pull toward center more strongly near edges
                float pull = 0.97f + 0.02f * (dist / maxDist);
                srcX = cx + dx * pull;
                srcY = cy + dy * pull;
                break;
            }
            }

            int idx = y * m_width + x;
            m_displaceTable[idx].srcX = static_cast<int>(srcX);
            m_displaceTable[idx].srcY = static_cast<int>(srcY);
        }
    }
}

void AvsMovement::render(AvsFramebuffer &fb, const AvsAudioData &)
{
    m_backBuffer.copyFrom(fb);
    const uint32_t *src = m_backBuffer.pixels();
    uint32_t *dst = fb.pixels();
    int w = m_width;
    int h = m_height;

    for (int i = 0; i < w * h; i++) {
        int sx = m_displaceTable[i].srcX;
        int sy = m_displaceTable[i].srcY;

        if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
            dst[i] = src[sy * w + sx];
        } else {
            dst[i] = 0xFF000000; // black for out of bounds
        }
    }
}
