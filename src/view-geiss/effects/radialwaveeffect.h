#ifndef RADIALWAVEEFFECT_H
#define RADIALWAVEEFFECT_H

#include "../geisseffect.h"
#include "../colorstate.h"

class RadialWaveEffect : public GeissEffect {
public:
    RadialWaveEffect();

    void activate() override;
    void render(uint32_t* fb, int width, int height,
                const AudioAnalyzer& audio, float frame) override;

private:
    ColorState m_color;
    float m_prevRad[314];
};

#endif // RADIALWAVEEFFECT_H
