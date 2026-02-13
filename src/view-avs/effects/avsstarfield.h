#ifndef AVSSTARFIELD_H
#define AVSSTARFIELD_H

#include "avseffect.h"
#include <array>
#include <cstdint>

class AvsStarfield : public AvsEffect
{
public:
    explicit AvsStarfield(int starCount = 128, float baseSpeed = 0.02f);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Starfield"; }

    int m_starCount;
    float m_baseSpeed;

private:
    struct Star {
        float x, y, z;
    };

    static constexpr int MAX_STARS = 256;
    std::array<Star, MAX_STARS> m_stars;
    uint32_t m_rng = 0xACE1u;

    float randFloat(); // returns [-1.0, 1.0]
    void respawnStar(Star &s);
};

#endif // AVSSTARFIELD_H
