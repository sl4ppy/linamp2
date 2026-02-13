#ifndef AVSRING_H
#define AVSRING_H

#include "avseffect.h"
#include <cstdint>

class AvsRing : public AvsEffect
{
public:
    explicit AvsRing(uint32_t color = 0xFF00FF80, int ringCount = 16, float scaleY = 0.5f);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Ring"; }

    uint32_t m_color;
    int m_ringCount;
    float m_scaleY;

private:
    void drawCircle(AvsFramebuffer &fb, int cx, int cy, int radius, uint32_t col);
};

#endif // AVSRING_H
