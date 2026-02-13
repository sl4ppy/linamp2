#ifndef AVSEFFECT_H
#define AVSEFFECT_H

#include <QString>

class AvsFramebuffer;
class AvsAudioData;

class AvsEffect
{
public:
    virtual ~AvsEffect() = default;
    virtual void render(AvsFramebuffer &fb, const AvsAudioData &audio) = 0;
    virtual QString name() const = 0;
    bool enabled = true;
};

#endif // AVSEFFECT_H
