#ifndef AVSMIRROR_H
#define AVSMIRROR_H

#include "avseffect.h"

class AvsMirror : public AvsEffect
{
public:
    enum Mode { Horizontal, Vertical, Both };

    explicit AvsMirror(Mode mode = Horizontal);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Mirror"; }

    Mode m_mode;
};

#endif // AVSMIRROR_H
