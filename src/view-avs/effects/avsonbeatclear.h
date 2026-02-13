#ifndef AVSONBEATCLEAR_H
#define AVSONBEATCLEAR_H

#include "avseffect.h"
#include <cstdint>

class AvsOnBeatClear : public AvsEffect
{
public:
    enum Mode { ClearBlack, FlashWhite, FlashColor, RandomColor };

    explicit AvsOnBeatClear(Mode mode = FlashWhite, uint32_t flashColor = 0xFFFFFFFF);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "On Beat Clear"; }

    Mode m_mode;
    uint32_t m_flashColor;

private:
    uint32_t m_rngState = 0x12345678;
};

#endif // AVSONBEATCLEAR_H
