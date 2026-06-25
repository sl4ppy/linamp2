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
#include "avsview.h"
#include "geisswidget.h"
#include "viewtransition.h"
#include "mediaplayer.h"
#include "vbansender.h"

// Screensaver timeout in milliseconds (5 minutes default)
#define SCREENSAVER_TIMEOUT_MS (5 * 60 * 1000)

class ApiServer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // HTTP API hooks (called on the GUI thread by ApiServer)
    void apiTriggerScreensaver();
    void apiDismissScreensaver();
    bool apiShowClockFace(int index); // false if index out of range
    MediaPlayer::PlaybackState playbackState() const { return currentPlaybackState; }

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
    AvsView *avsView;

public slots:
    void showPlayer();
    void showPlaylist();
    void showMenu();
    void showAvs();
    void showGeiss();
    void showShutdownModal();
    void open();

private slots:
    void onPlaybackStateChanged(MediaPlayer::PlaybackState state);
    void activateScreenSaver();
    void deactivateScreenSaver();
    void deactivateAvs();
    void resetScreenSaverTimer();

private:
    void showClockScreensaver(int themeIndex); // themeIndex < 0 = random
    QMediaPlaylist *m_playlist = nullptr;
    PlaylistModel *m_playlistModel = nullptr;
    QProcess *shutdownProcess = nullptr;
    void shutdown();

    // Screensaver and visualization
    ScreenSaverView *screenSaver = nullptr;
    GeissWidget *geissVisualizer = nullptr;
    ViewTransition *viewTransition = nullptr;
    QTimer *screenSaverTimer = nullptr;
    bool screenSaverActive = false;
    bool geissActive = false;
    MediaPlayer::PlaybackState currentPlaybackState = MediaPlayer::StoppedState;
    ApiServer *apiServer = nullptr;

};

#endif // MAINWINDOW_H
