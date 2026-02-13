#ifndef AVSBUFFERBLEND_H
#define AVSBUFFERBLEND_H

#include "avseffect.h"
#include "avsframebuffer.h"

class AvsBufferBlend : public AvsEffect
{
public:
    explicit AvsBufferBlend(float blendRatio = 0.7f);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "BufferBlend"; }

    float blendRatio;

private:
    AvsFramebuffer m_accumulator;
    bool m_firstFrame = true;
};

#endif // AVSBUFFERBLEND_H
