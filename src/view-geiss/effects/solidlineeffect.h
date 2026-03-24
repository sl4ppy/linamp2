#ifndef SOLIDLINEEFFECT_H
#define SOLIDLINEEFFECT_H

#include "../geisseffect.h"

class SolidLineEffect : public GeissEffect {
public:
    SolidLineEffect() = default;

    void render(uint32_t* fb, int width, int height,
                const AudioAnalyzer& audio, float frame) override;

private:
    enum Channel { ALL, RED, GREEN, BLUE };

    void renderLine(uint32_t* fb, int w, int h, float t, Channel channel);
};

#endif // SOLIDLINEEFFECT_H
