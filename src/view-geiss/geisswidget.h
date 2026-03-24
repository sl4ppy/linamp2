#ifndef GEISSWIDGET_H
#define GEISSWIDGET_H

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QByteArray>
#include <QAudioFormat>
#include <vector>

#include "warpparams.h"
#include "colorstate.h"
#include "audioanalyzer.h"
#include "warpengine.h"
#include "effectengine.h"
#include "warpmapgenerator.h"

class GeissWidget : public QWidget
{
    Q_OBJECT
public:
    explicit GeissWidget(QWidget *parent = nullptr);
    ~GeissWidget() override;

public slots:
    void feedAudio(const QByteArray& data, QAudioFormat format);

signals:
    void userActivityDetected();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onFrameTick();
    void onMapReady(std::vector<WarpEntry> newMap);

private:
    void initWarpMap();
    void startGeneratingNextMap();
    void selectNewEffects();

    // Framebuffers (ping-pong)
    QImage m_fb[2];
    int m_activeFB = 0;

    // Components
    AudioAnalyzer m_audio;
    WarpEngine m_warp;
    EffectEngine m_effects;
    WarpMapGenerator *m_mapGen = nullptr;
    ColorState m_colorState;

    // Warp maps
    std::vector<WarpEntry> m_warpMap;
    std::vector<WarpEntry> m_warpMapNext;
    bool m_nextMapReady = false;
    bool m_forceSwap = false;
    WarpParams m_currentParams;

    // Frame state
    QTimer *m_frameTimer = nullptr;
    float m_frame = 0.0f;
    int m_framesSinceSwap = 0;
    static constexpr int FRAMES_TIL_AUTO_SWITCH = 300; // ~10 seconds at 30fps
};

#endif // GEISSWIDGET_H
