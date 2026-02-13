#ifndef AVSCLEARSCREEN_H
#define AVSCLEARSCREEN_H

#include "avseffect.h"
#include <cstdint>

class AvsClearScreen : public AvsEffect
{
public:
    explicit AvsClearScreen(uint32_t color = 0xFF000000);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Clear Screen"; }

    uint32_t m_color;
};

#endif // AVSCLEARSCREEN_H
