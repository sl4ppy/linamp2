#ifndef VBANSENDER_H
#define VBANSENDER_H

#include <QObject>
#include <QProcess>
#include <QSettings>

class VbanSender : public QObject
{
    Q_OBJECT
public:
    explicit VbanSender(QObject *parent = nullptr);
    ~VbanSender();

    void start();
    void stop();
    void setEnabled(bool enabled);
    bool isEnabled() const;

signals:
    void enabledChanged(bool enabled);

private:
    bool m_enabled = false;
    QString m_destinationIp;
    int m_port;
    QString m_streamName;

    QProcess *m_process = nullptr;

    void loadSettings();
    void saveSettings();
};

#endif // VBANSENDER_H
