#ifndef AVSMOSAIC_H
#define AVSMOSAIC_H

#include "avseffect.h"

class AvsMosaic : public AvsEffect
{
public:
    explicit AvsMosaic(int blockSize = 4, bool beatReactive = true);

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "Mosaic"; }

    int blockSize;
    bool beatReactive;
};

#endif // AVSMOSAIC_H
