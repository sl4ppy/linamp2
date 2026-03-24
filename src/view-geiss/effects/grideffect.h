#ifndef GRIDEFFECT_H
#define GRIDEFFECT_H

#include "../geisseffect.h"

class GridEffect : public GeissEffect {
public:
    GridEffect();

    void activate() override;
    void render(uint32_t* fb, int width, int height,
                const AudioAnalyzer& audio, float frame) override;

private:
    int m_dir;
};

#endif // GRIDEFFECT_H
