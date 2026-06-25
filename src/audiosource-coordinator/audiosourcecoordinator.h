#ifndef AUDIOSOURCECOORDINATOR_H
#define AUDIOSOURCECOORDINATOR_H

#include <QObject>
#include <QStringList>
#include "audiosource.h"
#include "systemaudiocontrol.h"
#include "playerview.h"

class AudioSourceCoordinator : public QObject
{
    Q_OBJECT
public:
    explicit AudioSourceCoordinator(QObject *parent = nullptr, PlayerView *playerView = nullptr);

    void addSource(AudioSource *source, QString label, bool activate = false);

    int currentVolume() const;
    int currentBalance() const;
    QString currentSourceLabel() const;
    QStringList sourceLabelList() const;   // all source labels in index order

signals:
    void sourceChanged(int source);
    void volumeChanged(int volume);
    void balanceChanged(int balance);

public slots:
    void setSource(int source);
    void setVolume(int volume);
    void setBalance(int balance);

    // Transport forwarding to the active source (used by the HTTP API)
    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void seek(int ms);
    void shuffle();
    void repeat();

private:
    AudioSource *activeSource() const;
    QList<AudioSource*> sources;
    QList<QString> sourceLabels;
    int currentSource = -1;

    SystemAudioControl *system_audio = nullptr;
    PlayerView *view = nullptr;
};

#endif // AUDIOSOURCECOORDINATOR_H
