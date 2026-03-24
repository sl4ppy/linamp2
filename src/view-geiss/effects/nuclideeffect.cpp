#include "nuclideeffect.h"
#include "../audioanalyzer.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

NuclideEffect::NuclideEffect()
{
    m_color.randomize();
}

void NuclideEffect::activate()
{
    m_color.randomize();
}

void NuclideEffect::render(uint32_t* fb, int width, int height,
                           const AudioAnalyzer& audio, float frame)
{
    // Only render on beats or loud moments
    bool trigger = audio.isBigBeat() ||
                   (audio.currentVol() > audio.avgVol() * 1.25f && audio.hasSoundData());
    if (!trigger)
        return;

    int nodes = 3 + rand() % 5;
    float phase = (rand() % 1000) * 0.001f * 2.0f * (float)M_PI;
    float rad = std::min(width, height) * 0.2f + (float)(rand() % 8);

    float volRatio = audio.currentVol() / std::max(0.01f, audio.avgVol()) - 1.0f;
    int nodeRad = 3 + (int)(8.0f * volRatio);
    nodeRad = std::clamp(nodeRad, 2, 8);

    int cx = width / 2;
    int cy = height / 2;

    float base = std::min(255.0f, audio.currentVol() * 200.0f + audio.avgVol() * 50.0f);
    uint8_t r, g, b;
    m_color.getColor(frame, base, r, g, b);

    for (int n = 0; n < nodes; n++) {
        float angle = (float)n / (float)nodes * 2.0f * (float)M_PI + phase;
        int nx = cx + (int)(rad * cosf(angle));
        int ny = cy + (int)(rad * sinf(angle));

        for (int dy = -nodeRad; dy <= nodeRad; dy++) {
            for (int dx = -nodeRad; dx <= nodeRad; dx++) {
                float dist = sqrtf((float)(dx * dx + dy * dy));
                float val = ((float)nodeRad - dist) * 25.0f;
                if (val > 0.0f) {
                    int iv = std::min(255, (int)val);
                    uint8_t pr = (uint8_t)(r * iv / 255);
                    uint8_t pg = (uint8_t)(g * iv / 255);
                    uint8_t pb = (uint8_t)(b * iv / 255);
                    GeissPixel::accumulateSafe(fb, nx + dx, ny + dy,
                                               width, height, pr, pg, pb);
                }
            }
        }
    }
}
