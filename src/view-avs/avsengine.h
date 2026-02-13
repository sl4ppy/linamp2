#ifndef AVSENGINE_H
#define AVSENGINE_H

#include "avsframebuffer.h"
#include "avseffect.h"
#include "avsaudiodata.h"
#include <QImage>
#include <QString>
#include <memory>
#include <vector>
#include <functional>

class AvsEngine
{
public:
    AvsEngine();

    void addEffect(std::unique_ptr<AvsEffect> effect);
    void clearEffects();
    const QImage &renderFrame(const AvsAudioData &audioData);

    void loadPreset(int index);
    void nextPreset();
    void prevPreset();
    int presetIndex() const;
    int presetCount() const;
    QString presetName() const;

private:
    AvsFramebuffer m_frontBuffer;
    QImage m_frameImage;
    std::vector<std::unique_ptr<AvsEffect>> m_effects;

    int m_presetIndex = 0;
    struct PresetDef {
        QString name;
        std::function<void(AvsEngine&)> builder;
    };
    std::vector<PresetDef> m_presets;
    void initPresets();

    // Crossfade transition
    AvsFramebuffer m_transitionBuffer;
    bool m_transitioning = false;
    int m_transitionFramesRemaining = 0;
    static constexpr int TRANSITION_DURATION = 30; // ~1 sec at 30 FPS
    bool m_hasRenderedFrame = false; // suppress crossfade on first load
};

#endif // AVSENGINE_H
