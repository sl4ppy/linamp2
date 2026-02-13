#include "avsstarfield.h"
#include "avsframebuffer.h"
#include "avsaudiodata.h"
#include <algorithm>

AvsStarfield::AvsStarfield(int starCount, float baseSpeed)
    : m_starCount(std::min(starCount, MAX_STARS)), m_baseSpeed(baseSpeed)
{
    for (int i = 0; i < m_starCount; i++) {
        m_stars[i].x = randFloat();
        m_stars[i].y = randFloat();
        m_stars[i].z = (randFloat() + 1.0f) * 0.5f; // [0, 1]
    }
}

float AvsStarfield::randFloat()
{
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0xFFFF) / 32768.0f - 1.0f;
}

void AvsStarfield::respawnStar(Star &s)
{
    s.x = randFloat();
    s.y = randFloat();
    s.z = 1.0f;
}

void AvsStarfield::render(AvsFramebuffer &fb, const AvsAudioData &audio)
{
    int w = fb.width();
    int h = fb.height();
    float cx = w * 0.5f;
    float cy = h * 0.5f;
    uint32_t *px = fb.pixels();

    float speed = m_baseSpeed * (1.0f + audio.beatDecay * 2.0f);

    for (int i = 0; i < m_starCount; i++) {
        Star &s = m_stars[i];
        s.z -= speed;

        if (s.z <= 0.01f) {
            respawnStar(s);
            continue;
        }

        // Project to 2D
        float screenX = cx + (s.x / s.z) * cx;
        float screenY = cy + (s.y / s.z) * cy;

        int px_x = static_cast<int>(screenX);
        int px_y = static_cast<int>(screenY);

        if (px_x < 0 || px_x >= w || px_y < 0 || px_y >= h) {
            respawnStar(s);
            continue;
        }

        // Brightness based on closeness (lower z = brighter)
        float brightness = 1.0f - s.z;
        brightness = std::clamp(brightness, 0.0f, 1.0f);
        int bval = static_cast<int>(brightness * 255.0f);
        uint32_t col = 0xFF000000 | (bval << 16) | (bval << 8) | bval;

        px[px_y * w + px_x] = col;
    }
}
