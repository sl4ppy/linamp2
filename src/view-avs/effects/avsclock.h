#ifndef AVSCLOCK_H
#define AVSCLOCK_H

#include "avseffect.h"
#include <cstdint>

class AvsClock : public AvsEffect
{
public:
    explicit AvsClock(uint32_t color = 0xFFFFFFFF, int scale = 3, bool blinkColon = true);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Clock"; }

    uint32_t color;
    int scale;
    bool blinkColon;

private:
    void drawGlyph(AvsFramebuffer &fb, int glyphIndex, int x, int y, uint32_t drawColor, int drawScale);
    static const uint8_t FONT_5x7[11][7]; // 0-9, colon
};

#endif // AVSCLOCK_H
