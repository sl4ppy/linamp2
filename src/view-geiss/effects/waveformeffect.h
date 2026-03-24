#ifndef WAVEFORMEFFECT_H
#define WAVEFORMEFFECT_H

#include "../geisseffect.h"
#include "../colorstate.h"

class WaveformEffect : public GeissEffect {
public:
    explicit WaveformEffect(int mode = 0);

    void activate() override;
    void render(uint32_t* fb, int width, int height,
                const AudioAnalyzer& audio, float frame) override;

private:
    ColorState m_color;
    int m_mode; // 0=horizontal, 1=stereo, 2=vertical
    float m_prevZ[512];
};

#endif // WAVEFORMEFFECT_H
