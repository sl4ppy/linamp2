#include "effectengine.h"
#include "effects/waveformeffect.h"
#include "effects/radialwaveeffect.h"
#include "effects/solarparticles.h"
#include "effects/nuclideeffect.h"
#include "effects/shadebobseffect.h"
#include "effects/solidlineeffect.h"
#include "effects/chasereffect.h"
#include "effects/grideffect.h"
#include <cstdlib>

EffectEngine::EffectEngine()
{
    // Overlay effects with their default frequencies (out of 1000)
    auto addOverlay = [this](GeissEffect* e, int freq) {
        m_overlayEffects.push_back({std::unique_ptr<GeissEffect>(e), freq, false});
    };

    addOverlay(new ShadeBobsEffect(),  400);
    addOverlay(new SolidLineEffect(),  150);
    addOverlay(new SolarParticles(),   500);
    addOverlay(new ChaserEffect(),     220);
    addOverlay(new GridEffect(),         4);

    // Waveform effects (index matches mode)
    m_waveformEffects.push_back(std::make_unique<WaveformEffect>(0));  // Horizontal
    m_waveformEffects.push_back(std::make_unique<WaveformEffect>(1));  // Stereo
    m_waveformEffects.push_back(std::make_unique<WaveformEffect>(2));  // Vertical
    m_waveformEffects.push_back(std::make_unique<RadialWaveEffect>()); // Radial

    // Post-warp beat-reactive effect
    m_nuclide = std::make_unique<NuclideEffect>();
}

EffectEngine::~EffectEngine() = default;

void EffectEngine::selectEffects(bool hasSoundData)
{
    for (auto& slot : m_overlayEffects) {
        int threshold = slot.frequency;
        if (hasSoundData)
            threshold = threshold * 7 / 10;

        bool wasActive = slot.active;
        slot.active = (rand() % 1000) < threshold;

        if (slot.active && !wasActive)
            slot.effect->activate();
        else if (!slot.active && wasActive)
            slot.effect->deactivate();
    }

    // Cycle waveform mode
    m_waveformMode = rand() % (int)m_waveformEffects.size();
    m_waveformEffects[m_waveformMode]->activate();
}

void EffectEngine::renderOverlays(uint32_t* fb, int width, int height,
                                  const AudioAnalyzer& audio, float frame)
{
    for (auto& slot : m_overlayEffects) {
        if (slot.active)
            slot.effect->render(fb, width, height, audio, frame);
    }
}

void EffectEngine::renderWaveform(uint32_t* fb, int width, int height,
                                  const AudioAnalyzer& audio, float frame)
{
    if (m_waveformMode >= 0 && m_waveformMode < (int)m_waveformEffects.size()) {
        m_waveformEffects[m_waveformMode]->render(fb, width, height, audio, frame);
    }
}

void EffectEngine::renderPostWarp(uint32_t* fb, int width, int height,
                                  const AudioAnalyzer& audio, float frame)
{
    if (m_nuclide) {
        m_nuclide->render(fb, width, height, audio, frame);
    }
}
