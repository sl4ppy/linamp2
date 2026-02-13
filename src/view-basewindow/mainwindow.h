#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedLayout>
#include <QProcess>
#include <QTimer>
#include "audiosourcecd.h"
#include "audiosourcecoordinator.h"
#include "audiosourcefile.h"
#include "audiosourcepython.h"
#include "controlbuttonswidget.h"
#include "mainmenuview.h"
#include "playerview.h"
#include "playlistview.h"
#include "qmediaplaylist.h"
#include "playlistmodel.h"
#include "screensaverview.h"
#include "mediaplayer.h"
#include "vbansender.h"

// Screensaver timeout in milliseconds (5 minutes default)
#define SCREENSAVER_TIMEOUT_MS (5 * 60 * 1000)

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    QStackedLayout *viewStack;

    PlayerView *player;
    ControlButtonsWidget *controlButtons;
    PlaylistView *playlist;
    MainMenuView *menu;
    AudioSourceCoordinator *coordinator;
    AudioSourceFile *fileSource;
    AudioSourcePython *btSource;
    AudioSourceCD *cdSource;
    AudioSourcePython *spotSource;
    VbanSender *vbanSender;

public slots:
    void showPlayer();
    void showPlaylist();
    void showMenu();
    void showShutdownModal();
    void open();

private slots:
    void onPlaybackStateChanged(MediaPlayer::PlaybackState state);
    void activateScreenSaver();
    void deactivateScreenSaver();
    void resetScreenSaverTimer();

private:
    QMediaPlaylist *m_playlist = nullptr;
    PlaylistModel *m_playlistModel = nullptr;
    QProcess *shutdownProcess = nullptr;
    void shutdown();

    // Screensaver related
    ScreenSaverView *screenSaver = nullptr;
    QTimer *screenSaverTimer = nullptr;
    bool screenSaverActive = false;
    MediaPlayer::PlaybackState currentPlaybackState = MediaPlayer::StoppedState;

};

#endif // MAINWINDOW_H
