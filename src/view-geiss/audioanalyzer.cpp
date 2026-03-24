#include "audioanalyzer.h"

#include <cstring>
#include <cmath>
#include <algorithm>

AudioAnalyzer::AudioAnalyzer()
    : m_bassEnergy(0.0f)
    , m_midEnergy(0.0f)
    , m_trebleEnergy(0.0f)
    , m_currentVol(0.0f)
    , m_avgVol(0.0f)
    , m_avgVolWide(0.0f)
    , m_volHistoryPos(0)
    , m_beatMode(false)
    , m_bigBeat(false)
    , m_beatThreshold(1.10f)
    , m_waveformSize(0)
    , m_hasSoundData(false)
    , m_silenceFrames(0)
{
    std::memset(m_waveL, 0, sizeof(m_waveL));
    std::memset(m_waveR, 0, sizeof(m_waveR));
    std::memset(m_smoothWaveL, 0, sizeof(m_smoothWaveL));
    std::memset(m_smoothWaveR, 0, sizeof(m_smoothWaveR));
    std::memset(m_spectrum, 0, sizeof(m_spectrum));
    std::memset(m_volHistory, 0, sizeof(m_volHistory));
}

void AudioAnalyzer::process(const QByteArray& pcm, const QAudioFormat& format)
{
    Q_UNUSED(format);

    // Step 1: Convert int16 stereo PCM to float L/R
    const int16_t* samples = reinterpret_cast<const int16_t*>(pcm.constData());
    int totalSamples = pcm.size() / static_cast<int>(sizeof(int16_t));
    int frameCount = totalSamples / 2; // stereo: 2 samples per frame
    int count = std::min(frameCount, WAVEFORM_SIZE);

    for (int i = 0; i < count; ++i) {
        m_waveL[i] = samples[i * 2]     / 32768.0f;
        m_waveR[i] = samples[i * 2 + 1] / 32768.0f;
    }

    // Zero-fill remainder if fewer than WAVEFORM_SIZE frames
    for (int i = count; i < WAVEFORM_SIZE; ++i) {
        m_waveL[i] = 0.0f;
        m_waveR[i] = 0.0f;
    }

    m_waveformSize = count;

    // Step 2: Geiss-style smoothed waveform: smoothed[i] = 0.8*wave[i] + 0.2*wave[i+1]
    for (int i = 0; i < WAVEFORM_SIZE - 1; ++i) {
        m_smoothWaveL[i] = 0.8f * m_waveL[i] + 0.2f * m_waveL[i + 1];
        m_smoothWaveR[i] = 0.8f * m_waveR[i] + 0.2f * m_waveR[i + 1];
    }
    m_smoothWaveL[WAVEFORM_SIZE - 1] = m_waveL[WAVEFORM_SIZE - 1];
    m_smoothWaveR[WAVEFORM_SIZE - 1] = m_waveR[WAVEFORM_SIZE - 1];

    // Step 3: Mono mix for FFT
    float mono[WAVEFORM_SIZE];
    for (int i = 0; i < WAVEFORM_SIZE; ++i) {
        mono[i] = (m_waveL[i] + m_waveR[i]) * 0.5f;
    }
    calc_freq(mono, m_spectrum);

    // Step 4: Band energy
    m_bassEnergy = 0.0f;
    for (int i = 1; i <= 9; ++i)
        m_bassEnergy += m_spectrum[i];

    m_midEnergy = 0.0f;
    for (int i = 10; i <= 79; ++i)
        m_midEnergy += m_spectrum[i];

    m_trebleEnergy = 0.0f;
    for (int i = 80; i < SPECTRUM_SIZE; ++i)
        m_trebleEnergy += m_spectrum[i];

    // Step 5: Volume — max of abs(L[i]) + abs(R[i]) across all samples
    float vol = 0.0f;
    for (int i = 0; i < count; ++i) {
        float v = std::fabs(m_waveL[i]) + std::fabs(m_waveR[i]);
        if (v > vol)
            vol = v;
    }
    m_currentVol = vol;

    // Step 6: IIR averaging
    m_avgVol = m_avgVol * 0.85f + vol * 0.15f;
    m_avgVolWide = m_avgVolWide * 0.96f + vol * 0.04f;

    // Step 7: Sound-data presence tracking
    if (vol > 0.01f) {
        m_hasSoundData = true;
        m_silenceFrames = 0;
    } else {
        m_silenceFrames++;
        if (m_silenceFrames > 60)
            m_hasSoundData = false;
    }

    // Step 8: Beat detection
    detectBeat();
}

void AudioAnalyzer::detectBeat()
{
    // Step 1: Store current volume in history
    m_volHistory[m_volHistoryPos] = m_currentVol;
    m_volHistoryPos = (m_volHistoryPos + 1) % VOL_HISTORY_SIZE;

    // Step 2: Compute average of entire history
    float avg = 0.0f;
    for (int i = 0; i < VOL_HISTORY_SIZE; ++i)
        avg += m_volHistory[i];
    avg /= static_cast<float>(VOL_HISTORY_SIZE);

    // Step 3: Beat strength
    float beatStrength = 0.0f;
    if (avg > 0.01f) {
        float threshold = avg * 0.15f;
        for (int i = 1; i < VOL_HISTORY_SIZE; ++i) {
            float diff = std::fabs(m_volHistory[i] - m_volHistory[i - 1]);
            float contrib = diff - threshold;
            if (contrib > 0.0f)
                beatStrength += contrib;
        }
        beatStrength = (beatStrength / avg) * 10.0f;
    } else {
        beatStrength = 0.0f;
    }

    // Step 4: Hysteresis for beat mode
    if (beatStrength > 109.0f)
        m_beatMode = true;
    else if (beatStrength < 71.0f)
        m_beatMode = false;

    // Step 5: Recent max — most recent 40 entries in circular buffer
    float recentMax = 0.0f;
    for (int i = 0; i < 40; ++i) {
        int idx = (m_volHistoryPos - 1 - i + VOL_HISTORY_SIZE) % VOL_HISTORY_SIZE;
        if (m_volHistory[idx] > recentMax)
            recentMax = m_volHistory[idx];
    }

    // Step 6: Narrow average
    float narrowAvg = m_avgVol * 0.3f + m_currentVol * 0.7f;

    // Step 7: Big beat detection
    m_bigBeat = narrowAvg > recentMax * m_beatThreshold;

    // Step 8: Threshold adaptation
    if (!m_bigBeat && m_beatMode) {
        m_beatThreshold -= 0.002f;
    } else {
        m_beatThreshold = 1.10f;
    }

    // Step 9: Clamp threshold
    if (m_beatThreshold < 1.02f)
        m_beatThreshold = 1.02f;
}

const float* AudioAnalyzer::waveformL() const
{
    return m_waveL;
}

const float* AudioAnalyzer::waveformR() const
{
    return m_waveR;
}

const float* AudioAnalyzer::smoothWaveL() const
{
    return m_smoothWaveL;
}

const float* AudioAnalyzer::smoothWaveR() const
{
    return m_smoothWaveR;
}

int AudioAnalyzer::waveformSize() const
{
    return m_waveformSize;
}

const float* AudioAnalyzer::spectrum() const
{
    return m_spectrum;
}

float AudioAnalyzer::bassEnergy() const
{
    return m_bassEnergy;
}

float AudioAnalyzer::midEnergy() const
{
    return m_midEnergy;
}

float AudioAnalyzer::trebleEnergy() const
{
    return m_trebleEnergy;
}

float AudioAnalyzer::currentVol() const
{
    return m_currentVol;
}

float AudioAnalyzer::avgVol() const
{
    return m_avgVol;
}

float AudioAnalyzer::avgVolWide() const
{
    return m_avgVolWide;
}

bool AudioAnalyzer::isBeatMode() const
{
    return m_beatMode;
}

bool AudioAnalyzer::isBigBeat() const
{
    return m_bigBeat;
}

bool AudioAnalyzer::hasSoundData() const
{
    return m_hasSoundData;
}
