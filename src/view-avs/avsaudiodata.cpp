#include "avsaudiodata.h"
#include "fft.h"
#include <cstring>
#include <cmath>
#include <algorithm>

AvsAudioData::AvsAudioData()
    : isBeat(false), beatDecay(0.0f),
      isBeatBass(false), isBeatMid(false), isBeatHigh(false),
      beatDecayBass(0.0f), beatDecayMid(0.0f), beatDecayHigh(0.0f),
      m_bassHistoryPos(0), m_beatCooldown(0), m_beatDecayValue(0.0f),
      m_midHistoryPos(0), m_beatCooldownMid(0), m_beatDecayMid(0.0f),
      m_highHistoryPos(0), m_beatCooldownHigh(0), m_beatDecayHigh(0.0f)
{
    memset(waveformLeft, 0, sizeof(waveformLeft));
    memset(waveformRight, 0, sizeof(waveformRight));
    memset(waveformMono, 0, sizeof(waveformMono));
    memset(spectrumMono, 0, sizeof(spectrumMono));
    memset(m_bassHistory, 0, sizeof(m_bassHistory));
    memset(m_midHistory, 0, sizeof(m_midHistory));
    memset(m_highHistory, 0, sizeof(m_highHistory));
}

// Read one sample as float [-1.0, 1.0] from raw PCM data at the given byte offset
static float readSample(const char *ptr, QAudioFormat::SampleFormat fmt)
{
    switch (fmt) {
    case QAudioFormat::Int16: {
        int16_t val = *reinterpret_cast<const int16_t *>(ptr);
        return static_cast<float>(val) / 32768.0f;
    }
    case QAudioFormat::Int32: {
        int32_t val = *reinterpret_cast<const int32_t *>(ptr);
        return static_cast<float>(val) / 2147483648.0f;
    }
    case QAudioFormat::Float: {
        return *reinterpret_cast<const float *>(ptr);
    }
    case QAudioFormat::UInt8: {
        uint8_t val = *reinterpret_cast<const uint8_t *>(ptr);
        return (static_cast<float>(val) - 128.0f) / 128.0f;
    }
    default:
        return 0.0f;
    }
}

void AvsAudioData::processFromPcm(const QByteArray &data, QAudioFormat format)
{
    QAudioFormat::SampleFormat sampleFmt = format.sampleFormat();
    if (sampleFmt == QAudioFormat::Unknown)
        return;

    int channels = format.channelCount();
    if (channels < 1)
        return;

    int bytesPerSample = format.bytesPerSample();
    if (bytesPerSample < 1)
        return;

    int bytesPerFrame = format.bytesPerFrame();
    if (bytesPerFrame < 1)
        return;

    int totalFrames = data.size() / bytesPerFrame;
    if (totalFrames < 1)
        return;

    int framesToProcess = std::min(totalFrames, AVS_WAVEFORM_SIZE);

    const char *ptr = data.constData();

    // Extract waveform samples, reading interleaved frames
    for (int i = 0; i < framesToProcess; i++) {
        const char *framePtr = ptr + i * bytesPerFrame;
        float left = readSample(framePtr, sampleFmt);
        float right = (channels >= 2)
            ? readSample(framePtr + bytesPerSample, sampleFmt)
            : left;

        waveformLeft[i] = left;
        waveformRight[i] = right;
        waveformMono[i] = (left + right) * 0.5f;
    }

    // Zero-pad if we have fewer samples
    for (int i = framesToProcess; i < AVS_WAVEFORM_SIZE; i++) {
        waveformLeft[i] = 0.0f;
        waveformRight[i] = 0.0f;
        waveformMono[i] = 0.0f;
    }

    // Compute FFT spectrum using existing calc_freq()
    // calc_freq expects 512 samples, output is 256 frequency bins
    float fftInput[N];
    int fftFrames = std::min(framesToProcess, N);
    for (int i = 0; i < fftFrames; i++)
        fftInput[i] = waveformMono[i];
    for (int i = fftFrames; i < N; i++)
        fftInput[i] = 0.0f;

    float rawSpectrum[N / 2];
    calc_freq(fftInput, rawSpectrum);

    // Normalize spectrum to [0.0, 1.0] range
    for (int i = 0; i < AVS_SPECTRUM_SIZE; i++) {
        float val = rawSpectrum[i] * 5.0f;
        spectrumMono[i] = std::clamp(val, 0.0f, 1.0f);
    }

    // === Bass beat detection (bins 0-7) ===
    float bassEnergy = 0.0f;
    int bassBins = 8;
    for (int i = 0; i < bassBins; i++)
        bassEnergy += spectrumMono[i];
    bassEnergy /= bassBins;

    float avgBass = 0.0f;
    for (int i = 0; i < AVS_BEAT_HISTORY_SIZE; i++)
        avgBass += m_bassHistory[i];
    avgBass /= AVS_BEAT_HISTORY_SIZE;

    m_bassHistory[m_bassHistoryPos] = bassEnergy;
    m_bassHistoryPos = (m_bassHistoryPos + 1) % AVS_BEAT_HISTORY_SIZE;

    if (m_beatCooldown > 0)
        m_beatCooldown--;

    if (bassEnergy > avgBass * AVS_BEAT_THRESHOLD && m_beatCooldown == 0 && avgBass > 0.01f) {
        isBeat = true;
        isBeatBass = true;
        m_beatCooldown = AVS_BEAT_COOLDOWN_FRAMES;
        m_beatDecayValue = 1.0f;
    } else {
        isBeat = false;
        isBeatBass = false;
    }

    m_beatDecayValue *= 0.92f;
    beatDecay = m_beatDecayValue;
    beatDecayBass = m_beatDecayValue;

    // === Mid beat detection (bins 8-63) ===
    float midEnergy = 0.0f;
    int midBins = 56; // bins 8..63
    for (int i = 8; i < 64; i++)
        midEnergy += spectrumMono[i];
    midEnergy /= midBins;

    float avgMid = 0.0f;
    for (int i = 0; i < AVS_BEAT_HISTORY_SIZE; i++)
        avgMid += m_midHistory[i];
    avgMid /= AVS_BEAT_HISTORY_SIZE;

    m_midHistory[m_midHistoryPos] = midEnergy;
    m_midHistoryPos = (m_midHistoryPos + 1) % AVS_BEAT_HISTORY_SIZE;

    if (m_beatCooldownMid > 0)
        m_beatCooldownMid--;

    if (midEnergy > avgMid * AVS_BEAT_THRESHOLD && m_beatCooldownMid == 0 && avgMid > 0.01f) {
        isBeatMid = true;
        m_beatCooldownMid = AVS_BEAT_COOLDOWN_FRAMES;
        m_beatDecayMid = 1.0f;
    } else {
        isBeatMid = false;
    }

    m_beatDecayMid *= 0.92f;
    beatDecayMid = m_beatDecayMid;

    // === High beat detection (bins 64-255) ===
    float highEnergy = 0.0f;
    int highBins = 192; // bins 64..255
    for (int i = 64; i < 256; i++)
        highEnergy += spectrumMono[i];
    highEnergy /= highBins;

    float avgHigh = 0.0f;
    for (int i = 0; i < AVS_BEAT_HISTORY_SIZE; i++)
        avgHigh += m_highHistory[i];
    avgHigh /= AVS_BEAT_HISTORY_SIZE;

    m_highHistory[m_highHistoryPos] = highEnergy;
    m_highHistoryPos = (m_highHistoryPos + 1) % AVS_BEAT_HISTORY_SIZE;

    if (m_beatCooldownHigh > 0)
        m_beatCooldownHigh--;

    if (highEnergy > avgHigh * AVS_BEAT_THRESHOLD && m_beatCooldownHigh == 0 && avgHigh > 0.005f) {
        isBeatHigh = true;
        m_beatCooldownHigh = AVS_BEAT_COOLDOWN_FRAMES;
        m_beatDecayHigh = 1.0f;
    } else {
        isBeatHigh = false;
    }

    m_beatDecayHigh *= 0.92f;
    beatDecayHigh = m_beatDecayHigh;
}
