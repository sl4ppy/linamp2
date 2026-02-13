#ifndef AVSFADEOUT_H
#define AVSFADEOUT_H

#include "avseffect.h"

class AvsFadeOut : public AvsEffect
{
public:
    explicit AvsFadeOut(int speed = 4);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Fade Out"; }

    int m_speed; // 1-32
};

#endif // AVSFADEOUT_H
