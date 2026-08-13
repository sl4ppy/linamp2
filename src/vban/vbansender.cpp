#include "vbansender.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>

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

    // A previous player process that died without running our destructor (crash,
    // SIGKILL, OOM killer, hard shutdown) leaves its emitter behind, reparented
    // to init. Two emitters pushing the same stream name to the same host:port
    // interleave packets and the receiver renders that as garbled audio, so
    // clear any leftovers before starting ours.
    killStaleEmitters();

    if (m_process == nullptr) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::ForwardedChannels);
        connect(m_process, &QProcess::errorOccurred, this, [](QProcess::ProcessError error) {
            if (error != QProcess::Crashed) {
                qWarning() << "VbanSender: process error:" << error;
            }
        });
        // Have the kernel terminate the emitter if we die abnormally. stop()
        // only covers the orderly shutdown path; this covers the rest.
        m_process->setChildProcessModifier([]() {
            prctl(PR_SET_PDEATHSIG, SIGTERM);
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

// Terminate any vban_emitter left running by an earlier player process
void VbanSender::killStaleEmitters()
{
    const qint64 ownChild = (m_process != nullptr) ? m_process->processId() : -1;

    const QStringList pids = QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : pids) {
        bool isPid = false;
        const qint64 pid = entry.toLongLong(&isPid);
        if (!isPid || pid == ownChild || pid == getpid()) continue;

        QFile cmdline(QString("/proc/%1/cmdline").arg(entry));
        if (!cmdline.open(QIODevice::ReadOnly)) continue;

        // /proc/<pid>/cmdline is NUL separated; the first field is the binary
        const QByteArray exe = cmdline.readAll().split('\0').value(0);
        if (exe != QByteArray(VBAN_EMITTER_PATH)) continue;

        qWarning() << "VbanSender: terminating stale emitter, pid" << pid;
        ::kill(static_cast<pid_t>(pid), SIGTERM);
    }
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
