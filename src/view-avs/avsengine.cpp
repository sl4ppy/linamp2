#include "avsengine.h"
#include "avsfadeout.h"
#include "avsclearscreen.h"
#include "avssuperscope.h"
#include "avsmovement.h"
#include "avscolormodifier.h"
#include "avsonbeatclear.h"
#include "avsgrain.h"
#include "avsmirror.h"
#include "avsring.h"
#include "avsstarfield.h"
#include "avswater.h"
#include "avsdynamicmovement.h"
#include "avsblur.h"
#include "avsmosaic.h"
#include "avsbufferblend.h"
#include "avsclock.h"

AvsEngine::AvsEngine()
    : m_frontBuffer(AVS_FB_WIDTH, AVS_FB_HEIGHT)
    , m_transitionBuffer(AVS_FB_WIDTH, AVS_FB_HEIGHT)
{
    initPresets();
    loadPreset(0);
}

void AvsEngine::addEffect(std::unique_ptr<AvsEffect> effect)
{
    m_effects.push_back(std::move(effect));
}

void AvsEngine::clearEffects()
{
    m_effects.clear();
}

const QImage &AvsEngine::renderFrame(const AvsAudioData &audioData)
{
    for (auto &effect : m_effects) {
        if (effect->enabled) {
            effect->render(m_frontBuffer, audioData);
        }
    }

    m_hasRenderedFrame = true;

    // Crossfade: blend old snapshot (fading out) with new frame
    if (m_transitioning && m_transitionFramesRemaining > 0) {
        float oldAlpha = static_cast<float>(m_transitionFramesRemaining) / TRANSITION_DURATION;
        float newAlpha = 1.0f - oldAlpha;

        const uint32_t *oldPx = m_transitionBuffer.pixels();
        uint32_t *newPx = m_frontBuffer.pixels();
        int count = m_frontBuffer.pixelCount();

        for (int i = 0; i < count; ++i) {
            uint32_t o = oldPx[i];
            uint32_t n = newPx[i];

            int r = static_cast<int>(((o >> 16) & 0xFF) * oldAlpha + ((n >> 16) & 0xFF) * newAlpha);
            int g = static_cast<int>(((o >> 8) & 0xFF) * oldAlpha + ((n >> 8) & 0xFF) * newAlpha);
            int b = static_cast<int>((o & 0xFF) * oldAlpha + (n & 0xFF) * newAlpha);

            newPx[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }

        --m_transitionFramesRemaining;
        if (m_transitionFramesRemaining <= 0)
            m_transitioning = false;
    }

    m_frameImage = m_frontBuffer.toImage();
    return m_frameImage;
}

void AvsEngine::loadPreset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_presets.size()))
        return;

    // Snapshot current framebuffer for crossfade (only if we've rendered at least one frame)
    if (m_hasRenderedFrame) {
        m_transitionBuffer.copyFrom(m_frontBuffer);
        m_transitioning = true;
        m_transitionFramesRemaining = TRANSITION_DURATION;
    }

    m_presetIndex = index;
    clearEffects();
    m_frontBuffer.clear();
    m_presets[index].builder(*this);
}

void AvsEngine::nextPreset()
{
    int next = (m_presetIndex + 1) % static_cast<int>(m_presets.size());
    loadPreset(next);
}

void AvsEngine::prevPreset()
{
    int count = static_cast<int>(m_presets.size());
    int prev = (m_presetIndex - 1 + count) % count;
    loadPreset(prev);
}

int AvsEngine::presetIndex() const
{
    return m_presetIndex;
}

int AvsEngine::presetCount() const
{
    return static_cast<int>(m_presets.size());
}

QString AvsEngine::presetName() const
{
    if (m_presetIndex >= 0 && m_presetIndex < static_cast<int>(m_presets.size()))
        return m_presets[m_presetIndex].name;
    return QString();
}

void AvsEngine::initPresets()
{
    // 0: Classic Scope — the original Phase 1 preset
    m_presets.push_back({"Classic Scope", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(4));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::Oscilloscope;
        scope->drawMode = AvsSuperScope::Lines;
        scope->color = 0xFF00FFFF; // cyan
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsMovement>(AvsMovement::ZoomIn));
    }});

    // 1: Spiral Tunnel
    m_presets.push_back({"Spiral Tunnel", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(2));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::Spiral;
        scope->drawMode = AvsSuperScope::Lines;
        scope->color = 0xFF00FF80; // green
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsMovement>(AvsMovement::Tunnel));
    }});

    // 2: Neon Ring
    m_presets.push_back({"Neon Ring", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(6));
        e.addEffect(std::make_unique<AvsRing>(0xFFFF00FF, 16, 0.5f)); // magenta
        e.addEffect(std::make_unique<AvsMovement>(AvsMovement::Swirl));
        e.addEffect(std::make_unique<AvsColorModifier>(AvsColorModifier::HueShift));
    }});

    // 3: Starfield
    m_presets.push_back({"Starfield", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(3));
        e.addEffect(std::make_unique<AvsStarfield>(128, 0.02f));
        e.addEffect(std::make_unique<AvsMovement>(AvsMovement::ZoomOut));
    }});

    // 4: Spectrum Fire
    m_presets.push_back({"Spectrum Fire", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsClearScreen>(0xFF000000));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::SpectrumBars;
        scope->sourceType = AvsSuperScope::Spectrum;
        scope->drawMode = AvsSuperScope::Lines;
        scope->color = 0xFFFF8800; // orange
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsMirror>(AvsMirror::Vertical));
    }});

    // 5: Mirror Scope
    m_presets.push_back({"Mirror Scope", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(4));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::Oscilloscope;
        scope->drawMode = AvsSuperScope::Lines;
        scope->color = 0xFFFFFFFF; // white
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsMirror>(AvsMirror::Horizontal));
        e.addEffect(std::make_unique<AvsMovement>(AvsMovement::ZoomIn));
    }});

    // 6: Water World
    m_presets.push_back({"Water World", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(3));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::Circle;
        scope->drawMode = AvsSuperScope::Points;
        scope->color = 0xFF00FFFF; // cyan
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsWater>());
    }});

    // 7: Grain Storm
    m_presets.push_back({"Grain Storm", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(2));
        e.addEffect(std::make_unique<AvsGrain>(8, true));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::Oscilloscope;
        scope->drawMode = AvsSuperScope::Lines;
        scope->color = 0xFFFF4444; // red
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsMovement>(AvsMovement::SwirlOut));
    }});

    // 8: Beat Pulse
    m_presets.push_back({"Beat Pulse", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(8));
        e.addEffect(std::make_unique<AvsOnBeatClear>(AvsOnBeatClear::FlashWhite));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::Circle;
        scope->drawMode = AvsSuperScope::Lines;
        scope->color = 0xFF00FF00; // green
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsMovement>(AvsMovement::SuckIn));
    }});

    // 9: Dynamic Warp
    m_presets.push_back({"Dynamic Warp", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(4));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::Oscilloscope;
        scope->drawMode = AvsSuperScope::Lines;
        scope->color = 0xFF00FFFF; // cyan
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsDynamicMovement>());
    }});

    // 10: Soft Dream
    m_presets.push_back({"Soft Dream", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(2));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::Circle;
        scope->drawMode = AvsSuperScope::Lines;
        scope->color = 0xFFFF00FF; // magenta
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsBlur>(1));
        e.addEffect(std::make_unique<AvsMovement>(AvsMovement::Swirl));
    }});

    // 11: LED Wall
    m_presets.push_back({"LED Wall", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsClearScreen>(0xFF000000));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::SpectrumBars;
        scope->sourceType = AvsSuperScope::Spectrum;
        scope->drawMode = AvsSuperScope::Lines;
        scope->color = 0xFF00FF00; // green
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsMosaic>(4, true));
    }});

    // 12: Echo Chamber
    m_presets.push_back({"Echo Chamber", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsBufferBlend>(0.7f));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::Oscilloscope;
        scope->drawMode = AvsSuperScope::Lines;
        scope->color = 0xFF00FFFF; // cyan
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsDynamicMovement>());
    }});

    // 13: Neon Stars
    m_presets.push_back({"Neon Stars", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(4));
        e.addEffect(std::make_unique<AvsStarfield>(200, 0.02f));
        e.addEffect(std::make_unique<AvsRing>(0xFF00FFFF, 16, 0.5f)); // cyan
        e.addEffect(std::make_unique<AvsBlur>(1));
        e.addEffect(std::make_unique<AvsColorModifier>(AvsColorModifier::HueShift));
    }});

    // 14: Chaos
    m_presets.push_back({"Chaos", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(3));
        e.addEffect(std::make_unique<AvsOnBeatClear>(AvsOnBeatClear::RandomColor));
        auto scope = std::make_unique<AvsSuperScope>();
        scope->shapePreset = AvsSuperScope::Spiral;
        scope->drawMode = AvsSuperScope::Lines;
        scope->color = 0xFFFFFFFF; // white
        e.addEffect(std::move(scope));
        e.addEffect(std::make_unique<AvsGrain>(12, true));
        e.addEffect(std::make_unique<AvsMovement>(AvsMovement::Tunnel));
    }});

    // 15: Clockwork — old digits trail into a zoom tunnel, current time stays crisp
    m_presets.push_back({"Clockwork", [](AvsEngine &e) {
        e.addEffect(std::make_unique<AvsFadeOut>(3));
        e.addEffect(std::make_unique<AvsMovement>(AvsMovement::ZoomIn));
        e.addEffect(std::make_unique<AvsStarfield>(60, 0.012f));
        e.addEffect(std::make_unique<AvsClock>(0xFF00FFFF, 3, true)); // cyan, scale 3, blinking colon
    }});
}
