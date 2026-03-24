#ifndef SOLARPARTICLES_H
#define SOLARPARTICLES_H

#include "../geisseffect.h"
#include "../colorstate.h"

class SolarParticles : public GeissEffect {
public:
    SolarParticles();

    void activate() override;
    void render(uint32_t* fb, int width, int height,
                const AudioAnalyzer& audio, float frame) override;

private:
    ColorState m_color;
};

#endif // SOLARPARTICLES_H
