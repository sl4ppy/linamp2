#include "vbansender.h"
#include <QDebug>

#define VBAN_EMITTER_PATH "/usr/local/bin/vban_emitter"
#define VBAN_PULSE_SOURCE "vban_out.monitor"
#define VBAN_SAMPLE_RATE 44100
#define VBAN_CHANNELS 2

VbanSender::VbanSender(QObject *parent)
    : QObject(parent)
{
    loadSettings();

    if (m_enabled) {
        start();
    }
}

VbanSender::~VbanSender()
{
    stop();
}

void VbanSender::loadSettings()
{
    QSettings settings;
    m_enabled = settings.value("vban/enabled", false).toBool();
    m_destinationIp = settings.value("vban/destinationIp", "255.255.255.255").toString();
    m_port = settings.value("vban/port", 6980).toInt();
    m_streamName = settings.value("vban/streamName", "Linamp").toString();
}

void VbanSender::saveSettings()
{
    QSettings settings;
    settings.setValue("vban/enabled", m_enabled);
}

void VbanSender::start()
{
    if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
        qDebug() << "VbanSender: already running";
        return;
    }

    if (m_process == nullptr) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::ForwardedChannels);
        connect(m_process, &QProcess::errorOccurred, this, [](QProcess::ProcessError error) {
            if (error != QProcess::Crashed) {
                qWarning() << "VbanSender: process error:" << error;
            }
        });
    }

    QStringList args;
    args << "-i" << m_destinationIp
         << "-p" << QString::number(m_port)
         << "-s" << m_streamName
         << "-r" << QString::number(VBAN_SAMPLE_RATE)
         << "-b" << "pulseaudio"
         << "-n" << QString::number(VBAN_CHANNELS)
         << "-d" << VBAN_PULSE_SOURCE;

    // Set low PulseAudio latency so vban_emitter receives data in small,
    // evenly-paced chunks (~6ms) matching VBAN packet timing, instead of
    // large bursts that overwhelm the receptor.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PULSE_LATENCY_MSEC", "6");
    m_process->setProcessEnvironment(env);

    qDebug() << "VbanSender: starting" << VBAN_EMITTER_PATH << args;
    m_process->start(VBAN_EMITTER_PATH, args);
}

void VbanSender::stop()
{
    if (m_process == nullptr || m_process->state() == QProcess::NotRunning) {
        return;
    }

    qDebug() << "VbanSender: stopping";
    m_process->terminate();
    if (!m_process->waitForFinished(3000)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void VbanSender::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;

    m_enabled = enabled;
    saveSettings();

    if (m_enabled) {
        start();
    } else {
        stop();
    }

    emit enabledChanged(m_enabled);
}

bool VbanSender::isEnabled() const
{
    return m_enabled;
}
