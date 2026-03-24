#ifndef EFFECTENGINE_H
#define EFFECTENGINE_H

#include <vector>
#include <memory>
#include <cstdint>
#include "geisseffect.h"
#include "audioanalyzer.h"

class EffectEngine {
public:
    EffectEngine();
    ~EffectEngine();

    void selectEffects(bool hasSoundData);
    void renderOverlays(uint32_t* fb, int width, int height,
                        const AudioAnalyzer& audio, float frame);
    void renderWaveform(uint32_t* fb, int width, int height,
                        const AudioAnalyzer& audio, float frame);
    void renderPostWarp(uint32_t* fb, int width, int height,
                        const AudioAnalyzer& audio, float frame);

    int waveformMode() const { return m_waveformMode; }

private:
    struct EffectSlot {
        std::unique_ptr<GeissEffect> effect;
        int frequency;  // out of 1000
        bool active;
    };

    std::vector<EffectSlot> m_overlayEffects;  // Drawn before warp
    std::vector<std::unique_ptr<GeissEffect>> m_waveformEffects; // One active at a time
    std::unique_ptr<GeissEffect> m_nuclide;   // Post-warp beat-reactive
    int m_waveformMode = 0;
};

#endif // EFFECTENGINE_H
