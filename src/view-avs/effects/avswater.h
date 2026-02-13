#ifndef AVSWATER_H
#define AVSWATER_H

#include "avseffect.h"
#include "avsframebuffer.h"
#include <vector>
#include <cstdint>

class AvsWater : public AvsEffect
{
public:
    explicit AvsWater(float damping = 0.98f, float beatInjection = 50.0f);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Water"; }

    float m_damping;
    float m_beatInjection;

private:
    std::vector<float> m_heightCurrent;
    std::vector<float> m_heightPrevious;
    AvsFramebuffer m_backBuffer;
    int m_width = 0;
    int m_height = 0;
    uint32_t m_rng = 0x1337BEEF;

    void ensureBuffers(int w, int h);
};

#endif // AVSWATER_H
