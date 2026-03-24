#include "shadebobseffect.h"
#include "../audioanalyzer.h"
#include "../warpparams.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

ShadeBobsEffect::ShadeBobsEffect()
{
    randomizeParams();
}

void ShadeBobsEffect::activate()
{
    randomizeParams();
}

void ShadeBobsEffect::randomizeParams()
{
    m_numBobs = 3 + rand() % 4; // 3 to 6
    for (int i = 0; i < m_numBobs; i++) {
        m_f1[i] = 0.01f + (rand() % 1000) * 0.00004f; // 0.01 - 0.05
        m_f2[i] = 0.01f + (rand() % 1000) * 0.00004f;
        m_f3[i] = 0.01f + (rand() % 1000) * 0.00004f;
        m_f4[i] = 0.01f + (rand() % 1000) * 0.00004f;
        m_rad[i] = FB_W * 0.3f * (0.3f + (rand() % 1000) * 0.0007f);
        m_c1[i] = 0.005f + (rand() % 1000) * 0.000015f; // 0.005 - 0.02
        m_c2[i] = 0.005f + (rand() % 1000) * 0.000015f;
        m_c3[i] = 0.005f + (rand() % 1000) * 0.000015f;
    }
}

void ShadeBobsEffect::render(uint32_t* fb, int width, int height,
                              const AudioAnalyzer& audio, float frame)
{
    (void)audio;

    int cx = width / 2;
    int cy = height / 2;

    for (int i = 0; i < m_numBobs; i++) {
        float radX = m_rad[i];
        float radY = m_rad[i] * ((float)FB_H / (float)FB_W);

        float bx = (float)cx + radX * cosf(frame * m_f1[i])
                   + radX * 0.5f * cosf(frame * m_f2[i]);
        float by = (float)cy + radY * sinf(frame * m_f3[i])
                   + radY * 0.5f * sinf(frame * m_f4[i]);

        // Compute per-bob color channels (0 or 1 oscillation)
        int cr = (int)(1.0f + sinf(frame * m_c1[i]));
        int cg = (int)(1.0f + sinf(frame * m_c2[i]));
        int cb = (int)(1.0f + sinf(frame * m_c3[i]));

        // Ensure at least one channel is on
        if (cr == 0 && cg == 0 && cb == 0)
            cr = 1;

        uint8_t pr = cr ? 5 : 0;
        uint8_t pg = cg ? 5 : 0;
        uint8_t pb = cb ? 5 : 0;

        // Draw at 4 jittered positions
        for (int j = 0; j < 4; j++) {
            int jx = (int)bx + (rand() % 5) - 2;
            int jy = (int)by + (rand() % 5) - 2;
            GeissPixel::accumulateSafe(fb, jx, jy, width, height, pr, pg, pb);
        }
    }
}
