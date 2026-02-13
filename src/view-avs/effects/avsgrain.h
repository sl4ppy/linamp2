#ifndef AVSGRAIN_H
#define AVSGRAIN_H

#include "avseffect.h"

class AvsGrain : public AvsEffect
{
public:
    explicit AvsGrain(int amount = 8, bool beatReactive = true);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Grain"; }

    int m_amount;
    bool m_beatReactive;

private:
    uint32_t m_rng = 0xDEADBEEF;
    uint32_t xorshift32();
};

#endif // AVSGRAIN_H
