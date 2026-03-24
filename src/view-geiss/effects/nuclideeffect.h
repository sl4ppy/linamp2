#ifndef NUCLIDEEFFECT_H
#define NUCLIDEEFFECT_H

#include "../geisseffect.h"
#include "../colorstate.h"

class NuclideEffect : public GeissEffect {
public:
    NuclideEffect();

    void activate() override;
    void render(uint32_t* fb, int width, int height,
                const AudioAnalyzer& audio, float frame) override;

private:
    ColorState m_color;
};

#endif // NUCLIDEEFFECT_H
