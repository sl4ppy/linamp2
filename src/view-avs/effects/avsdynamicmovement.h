#ifndef AVSDYNAMICMOVEMENT_H
#define AVSDYNAMICMOVEMENT_H

#include "avseffect.h"
#include "avsframebuffer.h"

class AvsDynamicMovement : public AvsEffect
{
public:
    explicit AvsDynamicMovement(float baseZoom = 0.01f, float beatMultiplier = 3.0f, bool rotate = true);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Dynamic Movement"; }

    float m_baseZoom;
    float m_beatMultiplier;
    bool m_rotate;

private:
    AvsFramebuffer m_backBuffer;
};

#endif // AVSDYNAMICMOVEMENT_H
