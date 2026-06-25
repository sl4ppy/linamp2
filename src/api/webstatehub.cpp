#include "webstatehub.h"
#include "audiosource.h"
#include "audiosourcecoordinator.h"
#include "fft.h"
#include <algorithm>
#include <cmath>
#include <cstring>

static float pcmToFloatSample(qint16 pcm) { return float(pcm) / 32768.0f; }

static void computeLogXscaleBands(float *xscale, int bands)
{
    for (int i = 0; i <= bands; i++)
        xscale[i] = powf(256, (float)i / bands) - 0.5f;
}

static float computeOneBand(const float *freq, const float *xscale, int band, int bands)
{
    int a = ceilf(xscale[band]);
    int b = floorf(xscale[band + 1]);
    float n = 0;
    if (b < a) {
        n += freq[b] * (xscale[band + 1] - xscale[band]);
    } else {
        if (a > 0) n += freq[a - 1] * (a - xscale[band]);
        for (; a < b; a++) n += freq[a];
        if (b < 256) n += freq[b] * (xscale[band + 1] - b);
    }
    n *= (float)bands / 12;
    return 20 * log10f(n);
}

static QString stateString(MediaPlayer::PlaybackState s)
{
    switch (s) {
    case MediaPlayer::PlayingState: return QStringLiteral("playing");
    case MediaPlayer::PausedState:  return QStringLiteral("paused");
    default:                        return QStringLiteral("stopped");
    }
}

WebStateHub::WebStateHub(AudioSourceCoordinator *coordinator,
                         const QList<AudioSource *> &sources, QObject *parent)
    : QObject(parent), m_coord(coordinator)
{
    for (AudioSource *s : sources) {
        connect(s, &AudioSource::playbackStateChanged, this, &WebStateHub::onPlaybackState);
        connect(s, &AudioSource::metadataChanged,      this, &WebStateHub::onMetadata);
        connect(s, &AudioSource::durationChanged,      this, &WebStateHub::onDuration);
        connect(s, &AudioSource::positionChanged,      this, &WebStateHub::onPosition);
        connect(s, &AudioSource::dataEmitted,          this, &WebStateHub::onFormat);
        connect(s, &AudioSource::eqEnabledChanged,     this, &WebStateHub::onEq);
        connect(s, &AudioSource::plEnabledChanged,     this, &WebStateHub::onPl);
        connect(s, &AudioSource::shuffleEnabledChanged,this, &WebStateHub::onShuffle);
        connect(s, &AudioSource::repeatEnabledChanged, this, &WebStateHub::onRepeat);
    }
    connect(coordinator, &AudioSourceCoordinator::volumeChanged,  this, &WebStateHub::onVolume);
    connect(coordinator, &AudioSourceCoordinator::balanceChanged, this, &WebStateHub::onBalance);
    connect(coordinator, &AudioSourceCoordinator::sourceChanged,  this, &WebStateHub::onSourceChanged);

    m_volume  = coordinator->currentVolume();
    m_balance = coordinator->currentBalance();
    m_source  = coordinator->currentSourceLabel();

    computeLogXscaleBands(m_xscale, 19);
    m_spectrumClock.start();
}

QJsonObject WebStateHub::snapshot() const
{
    QJsonObject o;
    o["state"]        = m_state;
    o["artist"]       = m_artist;
    o["title"]        = m_title;
    o["album"]        = m_album;
    o["bitrate"]      = m_bitrateKbps;
    o["codec"]        = m_codec;
    o["sampleRateHz"] = m_sampleRateHz;
    o["channels"]     = m_channels;
    o["durationMs"]   = static_cast<double>(m_durationMs);
    o["positionMs"]   = static_cast<double>(m_positionMs);
    o["volume"]       = m_volume;
    o["balance"]      = m_balance;
    o["eq"]           = m_eq;
    o["pl"]           = m_pl;
    o["shuffle"]      = m_shuffle;
    o["repeat"]       = m_repeat;
    o["source"]       = m_source;
    o["view"]         = m_view;
    return o;
}

void WebStateHub::setView(const QString &view)
{
    if (m_view == view) return;
    m_view = view;
    emit stateChanged();
}

void WebStateHub::onPlaybackState(MediaPlayer::PlaybackState s)
{
    const QString ns = stateString(s);
    if (ns == m_state) return;
    m_state = ns;
    emit stateChanged();
}

void WebStateHub::onMetadata(const QMediaMetaData &md)
{
    m_artist = md.value(QMediaMetaData::AlbumArtist).toString();
    m_album  = md.value(QMediaMetaData::AlbumTitle).toString();
    m_title  = md.value(QMediaMetaData::Title).toString();
    m_codec  = md.value(QMediaMetaData::Description).toString();

    const int br = md.value(QMediaMetaData::AudioBitRate).toInt();
    m_bitrateKbps = br > 0 ? br / 1000 : 0;

    const int sr = md.value(QMediaMetaData::Comment).toString().toInt();
    if (sr > 0) m_sampleRateHz = sr;

    const qint64 dur = md.value(QMediaMetaData::Duration).toLongLong();
    if (dur > 0) m_durationMs = dur;

    emit stateChanged();
}

void WebStateHub::onDuration(qint64 ms)
{
    if (ms == m_durationMs) return;
    m_durationMs = ms;
    emit stateChanged();
}

void WebStateHub::onPosition(qint64 ms)
{
    m_positionMs = ms;
    emit positionChanged(ms);
}

void WebStateHub::onFormat(const QByteArray &data, QAudioFormat format)
{
    bool changed = false;
    if (format.channelCount() > 0 && format.channelCount() != m_channels) {
        m_channels = format.channelCount();
        changed = true;
    }
    if (format.sampleRate() > 0 && format.sampleRate() != m_sampleRateHz) {
        m_sampleRateHz = format.sampleRate();
        changed = true;
    }
    if (changed) emit stateChanged();

    if (m_spectrumActive && m_spectrumClock.elapsed() >= 33) {
        m_spectrumClock.restart();
        computeSpectrum(data, format);
    }
}

void WebStateHub::setSpectrumActive(bool active)
{
    m_spectrumActive = active;
}

void WebStateHub::computeSpectrum(const QByteArray &data, const QAudioFormat &format)
{
    const int channels = format.channelCount();
    if (channels < 1 || format.sampleFormat() != QAudioFormat::Int16)
        return;
    const int bytesPerFrame = 2 * channels;
    if (data.size() < N * bytesPerFrame)   // N = 512 from fft.h
        return;

    float mono[N];
    const char *ptr = data.constData();
    for (int i = 0; i < N; ++i) {
        if (channels == 1) {
            mono[i] = pcmToFloatSample(*reinterpret_cast<const qint16 *>(ptr));
        } else {
            const qint16 l = *reinterpret_cast<const qint16 *>(ptr);
            const qint16 r = *reinterpret_cast<const qint16 *>(ptr + 2);
            mono[i] = (pcmToFloatSample(l) + pcmToFloatSample(r)) / 2.0f;
        }
        ptr += bytesPerFrame;
    }

    float freq[N / 2];
    calc_freq(mono, freq);

    QVector<int> bars(19);
    for (int i = 0; i < 19; ++i) {
        int x = 40 + static_cast<int>(computeOneBand(freq, m_xscale, i, 19));
        bars[i] = std::clamp(x, 0, 40);
    }
    emit spectrum(bars);
}

void WebStateHub::onVolume(int v)   { if (v != m_volume)  { m_volume = v;  emit stateChanged(); } }
void WebStateHub::onBalance(int b)  { if (b != m_balance) { m_balance = b; emit stateChanged(); } }
void WebStateHub::onEq(bool e)      { if (e != m_eq)      { m_eq = e;      emit stateChanged(); } }
void WebStateHub::onPl(bool p)      { if (p != m_pl)      { m_pl = p;      emit stateChanged(); } }
void WebStateHub::onShuffle(bool s) { if (s != m_shuffle) { m_shuffle = s; emit stateChanged(); } }
void WebStateHub::onRepeat(bool r)  { if (r != m_repeat)  { m_repeat = r;  emit stateChanged(); } }

void WebStateHub::onSourceChanged(int)
{
    m_source = m_coord->currentSourceLabel();
    emit stateChanged();
}
