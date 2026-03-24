#ifndef SHADEBOBSEFFECT_H
#define SHADEBOBSEFFECT_H

#include "../geisseffect.h"

class ShadeBobsEffect : public GeissEffect {
public:
    ShadeBobsEffect();

    void activate() override;
    void render(uint32_t* fb, int width, int height,
                const AudioAnalyzer& audio, float frame) override;

private:
    void randomizeParams();

    int m_numBobs;
    float m_f1[6], m_f2[6], m_f3[6], m_f4[6];
    float m_rad[6];
    float m_c1[6], m_c2[6], m_c3[6];
};

#endif // SHADEBOBSEFFECT_H
