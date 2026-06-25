#ifndef WEBSTATEHUB_H
#define WEBSTATEHUB_H

#include <QObject>
#include <QJsonObject>
#include <QMediaMetaData>
#include <QAudioFormat>
#include <QVector>
#include <QElapsedTimer>
#include "mediaplayer.h"

class AudioSource;
class AudioSourceCoordinator;

// Aggregates the live player state the web UI mirrors. Connects to all sources
// (inactive sources don't emit) plus the coordinator, and holds the current
// snapshot. Emits stateChanged() on any non-position change, positionChanged()
// on playback position updates.
class WebStateHub : public QObject
{
    Q_OBJECT
public:
    WebStateHub(AudioSourceCoordinator *coordinator,
                const QList<AudioSource *> &sources,
                QObject *parent = nullptr);

    QJsonObject snapshot() const;

public slots:
    void setView(const QString &view);
    void setSpectrumActive(bool active);

signals:
    void stateChanged();
    void positionChanged(qint64 positionMs);
    void spectrum(const QVector<int> &bars);

private slots:
    void onPlaybackState(MediaPlayer::PlaybackState state);
    void onMetadata(const QMediaMetaData &md);
    void onDuration(qint64 ms);
    void onPosition(qint64 ms);
    void onFormat(const QByteArray &data, QAudioFormat format);
    void onVolume(int v);
    void onBalance(int b);
    void onEq(bool e);
    void onPl(bool p);
    void onShuffle(bool s);
    void onRepeat(bool r);
    void onSourceChanged(int index);

private:
    AudioSourceCoordinator *m_coord;

    // Spectrum (computed from PCM, gated on SSE clients)
    bool m_spectrumActive = false;
    float m_xscale[20] = {0};
    QElapsedTimer m_spectrumClock;
    void computeSpectrum(const QByteArray &data, const QAudioFormat &format);

    QString m_state = "stopped";
    QString m_artist, m_title, m_album, m_codec, m_source;
    QString m_view = "player";
    int m_bitrateKbps = 0;
    int m_sampleRateHz = 0;
    int m_channels = 0;
    qint64 m_durationMs = 0;
    qint64 m_positionMs = 0;
    int m_volume = 0;
    int m_balance = 0;
    bool m_eq = false, m_pl = false, m_shuffle = false, m_repeat = false;
};

#endif // WEBSTATEHUB_H
