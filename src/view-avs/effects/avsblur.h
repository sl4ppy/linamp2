#ifndef AVSBLUR_H
#define AVSBLUR_H

#include "avseffect.h"
#include "avsframebuffer.h"

class AvsBlur : public AvsEffect
{
public:
    explicit AvsBlur(int passes = 1);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Blur"; }

    int passes;

private:
    AvsFramebuffer m_temp;
};

#endif // AVSBLUR_H
