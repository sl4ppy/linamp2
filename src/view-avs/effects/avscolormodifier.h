#ifndef AVSCOLORMODIFIER_H
#define AVSCOLORMODIFIER_H

#include "avseffect.h"

class AvsColorModifier : public AvsEffect
{
public:
    enum Mode { Invert, Grayscale, HueShift, BrightnessBoost };

    explicit AvsColorModifier(Mode mode = Invert);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Color Modifier"; }

    Mode m_mode;

private:
    float m_hueOffset = 0.0f;
};

#endif // AVSCOLORMODIFIER_H
