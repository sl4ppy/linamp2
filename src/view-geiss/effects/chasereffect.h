#ifndef CHASEREFFECT_H
#define CHASEREFFECT_H

#include "../geisseffect.h"
#include "../colorstate.h"

class ChaserEffect : public GeissEffect {
public:
    ChaserEffect();

    void activate() override;
    void render(uint32_t* fb, int width, int height,
                const AudioAnalyzer& audio, float frame) override;

private:
    ColorState m_color;
    int m_numChasers;
};

#endif // CHASEREFFECT_H
