#ifndef AVSVIEW_H
#define AVSVIEW_H

#include <QWidget>
#include <QTimer>
#include <QAudioFormat>
#include <QElapsedTimer>
#include <QMediaMetaData>
#include "avsengine.h"
#include "avsaudiodata.h"

class AvsView : public QWidget
{
    Q_OBJECT

public:
    explicit AvsView(QWidget *parent = nullptr);
    ~AvsView();

public slots:
    void setAudioData(const QByteArray &data, QAudioFormat format);
    void setMetadata(QMediaMetaData metadata);
    void start();
    void stop();

signals:
    void userActivityDetected();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void onPresetChanged();

    AvsEngine m_engine;
    AvsAudioData m_audioData;
    QTimer *m_renderTimer = nullptr;
    bool m_running = false;

    // Swipe gesture tracking
    QPoint m_pressPos;
    bool m_swiping = false;

    // Preset name OSD
    QElapsedTimer m_presetNameTimer;
    bool m_showPresetName = false;
    static constexpr int PRESET_NAME_DURATION_MS = 2000;

    // Auto-cycle
    QTimer *m_autoCycleTimer = nullptr;
    bool m_autoCycleEnabled = true;
    static constexpr int AUTO_CYCLE_INTERVAL_MS = 30000;

    // Track info OSD
    QString m_trackArtist;
    QString m_trackTitle;
    QElapsedTimer m_trackInfoTimer;
    bool m_showTrackInfo = false;
    static constexpr int TRACK_INFO_DURATION_MS = 3000;
};

#endif // AVSVIEW_H
