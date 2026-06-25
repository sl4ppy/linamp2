#include <QFileDialog>

#include "mainwindow.h"
#include "apiserver.h"
#include "webstatehub.h"
#include "ssebroker.h"
#include "desktopplayerwindow.h"
#include "qstandardpaths.h"
#include "ui_desktopplayerwindow.h"
#include "scale.h"
#include "util.h"
#include "screensaverview.h"

#ifdef IS_EMBEDDED
#include "embeddedbasewindow.h"
#include "ui_embeddedbasewindow.h"
#else
#include "desktopbasewindow.h"
#include "ui_desktopbasewindow.h"
#endif

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include "playlistmodel.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QDebug>

#ifdef IS_EMBEDDED
const unsigned int WINDOW_W = 320 * UI_SCALE;
const unsigned int WINDOW_H = 100 * UI_SCALE;
#else
const unsigned int WINDOW_W = 277 * UI_SCALE;
const unsigned int WINDOW_H = 117 * UI_SCALE;
#endif

#define PY_SSIZE_T_CLEAN
#undef slots
#include <Python.h>
#define slots Q_SLOTS

#define LINAMP_PY_MODULE "linamp"

MainWindow::MainWindow(QWidget *parent)
     : QMainWindow{parent}
{
    // Initialize Python interpreter, required by audiosourcecd and audiosourcebt
    Py_Initialize();
    PyEval_SaveThread();

    // Setup playlist
    m_playlistModel = new PlaylistModel(this);
    m_playlist = m_playlistModel->playlist();

    m_musicRoot = QSettings().value("api/musicRoot", QDir::homePath() + "/Music").toString();
    if (!QDir(m_musicRoot).exists())
        m_musicRoot = QDir::homePath();

    // Setup views
    controlButtons = new ControlButtonsWidget(this);
    player = new PlayerView(this, controlButtons);
    player->setAttribute(Qt::WidgetAttribute::WA_StyledBackground,  true);

    playlist = new PlaylistView(this, m_playlistModel);
    playlist->setAttribute(Qt::WidgetAttribute::WA_StyledBackground,  true);

    coordinator = new AudioSourceCoordinator(this, player);
    fileSource = new AudioSourceFile(this, m_playlistModel);
    btSource = new AudioSourcePython(LINAMP_PY_MODULE, "BTPlayer", this);
    cdSource = new AudioSourceCD(this);
    spotSource = new AudioSourcePython(LINAMP_PY_MODULE, "SpotifyPlayer", this);
    coordinator->addSource(fileSource, "FILE", true);
    coordinator->addSource(btSource, "BT", false);
    coordinator->addSource(cdSource, "CD", false);
    coordinator->addSource(spotSource, "SPOT", false);


    // Connect events
    connect(fileSource, &AudioSourceFile::showPlaylistRequested, this, &MainWindow::showPlaylist);
    connect(playlist, &PlaylistView::showPlayerClicked, this, &MainWindow::showPlayer);
    connect(playlist, &PlaylistView::songSelected, fileSource, &AudioSourceFile::jump);
    connect(playlist, &PlaylistView::addSelectedFilesClicked, fileSource, &AudioSourceFile::addToPlaylist);

    connect(controlButtons, &ControlButtonsWidget::logoClicked, this, &MainWindow::showMenu);

    // Prepare player main view
    #ifdef IS_EMBEDDED
    EmbeddedBaseWindow *playerWindow = new EmbeddedBaseWindow(this);
    #else
    DesktopBaseWindow *playerWindow = new DesktopBaseWindow(this);
    #endif

    playerWindow->setAttribute(Qt::WidgetAttribute::WA_StyledBackground,  true);

    DesktopPlayerWindow *playerWindowContent = new DesktopPlayerWindow(this);
    QVBoxLayout *playerLayout = new QVBoxLayout;
    playerLayout->setContentsMargins(0, 0, 0, 0);
    playerLayout->addWidget(player);
    playerWindowContent->ui->playerViewContainer->setLayout(playerLayout);
    QVBoxLayout *buttonsLayout = new QVBoxLayout;
    buttonsLayout->setContentsMargins(0, 0, 0, 0);
    buttonsLayout->addWidget(controlButtons);
    playerWindowContent->ui->controlButtonsContainer->setLayout(buttonsLayout);

    QHBoxLayout *playerContentLayout = new QHBoxLayout;
    playerContentLayout->setContentsMargins(0, 0, 0, 0);
    playerContentLayout->addWidget(playerWindowContent);
    playerWindow->ui->body->setLayout(playerContentLayout);

    // Prepare playlist view
    #ifdef IS_EMBEDDED
    EmbeddedBaseWindow *playlistWindow = new EmbeddedBaseWindow(this);
    #else
    DesktopBaseWindow *playlistWindow = new DesktopBaseWindow(this);
    #endif

    playlistWindow->setAttribute(Qt::WidgetAttribute::WA_StyledBackground,  true);
    QVBoxLayout *playlistLayout = new QVBoxLayout;
    playlistLayout->setContentsMargins(0, 0, 0, 0);
    playlistLayout->addWidget(playlist);
    playlistWindow->ui->body->setLayout(playlistLayout);

    // Prepare menu view
    menu = new MainMenuView(this);
    menu->setAttribute(Qt::WidgetAttribute::WA_StyledBackground,  true);
    connect(menu, &MainMenuView::backClicked, this, &MainWindow::showPlayer);
    connect(menu, &MainMenuView::sourceSelected, coordinator, &AudioSourceCoordinator::setSource);
    connect(menu, &MainMenuView::avsClicked, this, &MainWindow::showAvs);

    // Setup VBAN sender
    vbanSender = new VbanSender(this);
    connect(menu, &MainMenuView::vbanToggled, vbanSender, &VbanSender::setEnabled);
    connect(vbanSender, &VbanSender::enabledChanged, menu, &MainMenuView::setVbanEnabled);
    menu->setVbanEnabled(vbanSender->isEnabled());

    // Prepare screensaver view
    screenSaver = new ScreenSaverView(this);
    screenSaver->setAttribute(Qt::WidgetAttribute::WA_StyledBackground, true);
    connect(screenSaver, &ScreenSaverView::userActivityDetected, this, &MainWindow::deactivateScreenSaver);

    // Prepare AVS visualization view
    avsView = new AvsView(this);
    avsView->setAttribute(Qt::WidgetAttribute::WA_StyledBackground, true);
    connect(avsView, &AvsView::userActivityDetected, this, &MainWindow::deactivateAvs);

    // Connect all audio sources' dataEmitted to AVS view (only active source emits)
    connect(fileSource, &AudioSource::dataEmitted, avsView, &AvsView::setAudioData);
    connect(btSource, &AudioSource::dataEmitted, avsView, &AvsView::setAudioData);
    connect(cdSource, &AudioSource::dataEmitted, avsView, &AvsView::setAudioData);
    connect(spotSource, &AudioSource::dataEmitted, avsView, &AvsView::setAudioData);

    // Connect metadata changes to AVS view for track info OSD
    connect(fileSource, &AudioSource::metadataChanged, avsView, &AvsView::setMetadata);
    connect(btSource, &AudioSource::metadataChanged, avsView, &AvsView::setMetadata);
    connect(cdSource, &AudioSource::metadataChanged, avsView, &AvsView::setMetadata);
    connect(spotSource, &AudioSource::metadataChanged, avsView, &AvsView::setMetadata);

    // Connect visualization click from player view to Geiss visualizer
    connect(player, &PlayerView::visualizationClicked, this, &MainWindow::showGeiss);

    // Prepare Geiss visualizer view
    geissVisualizer = new GeissWidget(this);
    geissVisualizer->setAttribute(Qt::WidgetAttribute::WA_StyledBackground, true);
    connect(geissVisualizer, &GeissWidget::userActivityDetected, this, &MainWindow::deactivateScreenSaver);

    // Prepare navigation stack
    viewStack = new QStackedLayout;
    viewStack->addWidget(playerWindow);     // Index 0
    viewStack->addWidget(playlistWindow);   // Index 1
    viewStack->addWidget(menu);             // Index 2
    viewStack->addWidget(screenSaver);      // Index 3
    viewStack->addWidget(avsView);          // Index 4
    viewStack->addWidget(geissVisualizer);  // Index 5

    // Final UI setup and show
    QVBoxLayout *centralLayout = new QVBoxLayout;
    centralLayout->addLayout(viewStack);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    QWidget *centralWidget = new QWidget;
    centralWidget->setLayout(centralLayout);

    setCentralWidget(centralWidget);

    // Crossfade transition for visualizer views
    viewTransition = new ViewTransition(centralWidget, viewStack, this);

    resize(WINDOW_W, WINDOW_H);
    this->setMaximumWidth(WINDOW_W);
    this->setMaximumHeight(WINDOW_H);
    this->setMinimumWidth(WINDOW_W);
    this->setMinimumHeight(WINDOW_H);

    #ifndef IS_EMBEDDED
    setWindowFlags(Qt::CustomizeWindowHint);
    #endif

    // Setup screensaver timer
    screenSaverTimer = new QTimer(this);
    screenSaverTimer->setSingleShot(true);
    screenSaverTimer->setInterval(SCREENSAVER_TIMEOUT_MS);
    connect(screenSaverTimer, &QTimer::timeout, this, &MainWindow::activateScreenSaver);

    // Feed audio data to Geiss visualizer from all sources
    connect(fileSource, &AudioSource::dataEmitted, geissVisualizer, &GeissWidget::feedAudio);
    connect(btSource, &AudioSource::dataEmitted, geissVisualizer, &GeissWidget::feedAudio);
    connect(cdSource, &AudioSource::dataEmitted, geissVisualizer, &GeissWidget::feedAudio);
    connect(spotSource, &AudioSource::dataEmitted, geissVisualizer, &GeissWidget::feedAudio);

    // Monitor playback state changes from all audio sources
    connect(fileSource, &AudioSource::playbackStateChanged, this, &MainWindow::onPlaybackStateChanged);
    connect(btSource, &AudioSource::playbackStateChanged, this, &MainWindow::onPlaybackStateChanged);
    connect(cdSource, &AudioSource::playbackStateChanged, this, &MainWindow::onPlaybackStateChanged);
    connect(spotSource, &AudioSource::playbackStateChanged, this, &MainWindow::onPlaybackStateChanged);

    // Start the screensaver timer (idle from beginning)
    screenSaverTimer->start();

    // Web state aggregation + HTTP/SSE API
    webState  = new WebStateHub(coordinator,
                                { fileSource, btSource, cdSource, spotSource }, this);
    sseBroker = new SseBroker(webState, this);
    apiServer = new ApiServer(coordinator, this, webState, sseBroker, this);
}

MainWindow::~MainWindow()
{
    PyGILState_Ensure();
    Py_Finalize();
}

void MainWindow::showPlayer()
{
    viewStack->setCurrentIndex(0);
    if (webState) webState->setView("player");
    resetScreenSaverTimer();
}

void MainWindow::showPlaylist()
{
    viewStack->setCurrentIndex(1);
    if (webState) webState->setView("playlist");
    resetScreenSaverTimer();
}

void MainWindow::showMenu()
{
    viewStack->setCurrentIndex(2);
    if (webState) webState->setView("menu");
    resetScreenSaverTimer();
}

void MainWindow::showAvs()
{
    avsView->start();
    viewTransition->fadeTo(4);
    if (webState) webState->setView("avs");
}

void MainWindow::showGeiss()
{
    geissActive = true;
    viewTransition->fadeTo(5);
    if (webState) webState->setView("geiss");
    screenSaverTimer->stop();
}

void MainWindow::showShutdownModal()
{
    QMessageBox msgBox;
    msgBox.setText("Shutdown");
    msgBox.setInformativeText("Turn off Linamp?");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Cancel);
    msgBox.setWindowFlags(Qt::FramelessWindowHint);
    int ret = msgBox.exec();

    switch (ret) {
    case QMessageBox::Yes:
        shutdown();
        break;
    default:
        break;
    }
}

void MainWindow::shutdown()
{
    QString appPath = QCoreApplication::applicationDirPath();
    QString cmd = appPath + "/shutdown.sh";

    shutdownProcess = new QProcess(this);
    shutdownProcess->start(cmd);
}

// Shows a standard file picker for adding items to the playlist
// Not used currently
void MainWindow::open()
{
    QList<QUrl> urls;
    urls << QUrl::fromLocalFile(QStandardPaths::standardLocations(QStandardPaths::HomeLocation).first())
         << QUrl::fromLocalFile(QStandardPaths::standardLocations(QStandardPaths::DownloadLocation).first())
         << QUrl::fromLocalFile(QStandardPaths::standardLocations(QStandardPaths::MusicLocation).first());


    QFileDialog fileDialog(this);
    QString filters = audioFileFilters().join(" ");
    fileDialog.setNameFilter("Audio (" + filters + ")");
    fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    fileDialog.setFileMode(QFileDialog::ExistingFiles);
    fileDialog.setWindowTitle(tr("Open Files"));
    fileDialog.setDirectory(QStandardPaths::standardLocations(QStandardPaths::MusicLocation)
                                .value(0, QDir::homePath()));
    fileDialog.setOption(QFileDialog::ReadOnly, true);
    fileDialog.setOption(QFileDialog::DontUseNativeDialog, true);
    fileDialog.setViewMode(QFileDialog::Detail);
    fileDialog.setSidebarUrls(urls);

#ifdef IS_EMBEDDED
    fileDialog.setWindowState(Qt::WindowFullScreen);
#endif

    if (fileDialog.exec() == QDialog::Accepted)
        fileSource->addToPlaylist(fileDialog.selectedUrls());

}

// Screensaver implementation

void MainWindow::onPlaybackStateChanged(MediaPlayer::PlaybackState state)
{
    if (state == currentPlaybackState) return; // Ignore repeated emissions of same state
    currentPlaybackState = state;

    if (state == MediaPlayer::PlayingState) {
        // Music is playing — if clock screensaver is showing, dismiss it
        // (Geiss stays active through track transitions — that's the point)
        if (screenSaverActive) {
            deactivateScreenSaver();
        }
        resetScreenSaverTimer();
    } else if (state == MediaPlayer::StoppedState || state == MediaPlayer::PausedState) {
        // Music stopped or paused — leave Geiss running.
        // The idle timer will eventually switch to clock screensaver if
        // playback doesn't resume (playlist ended, user paused, etc.)
        resetScreenSaverTimer();
    }
}

void MainWindow::activateScreenSaver()
{
    if (currentPlaybackState == MediaPlayer::PlayingState) {
        if (!geissActive) {
            // Music is playing, not yet visualizing — show Geiss
            qDebug() << "Activating Geiss visualizer";
            geissActive = true;
            viewTransition->fadeTo(5);
            if (webState) webState->setView("geiss");
        }
        // If Geiss is already active, do nothing (let it keep running)
    } else {
        // No music — transition to clock screensaver
        // (this handles: playlist ended while Geiss was showing,
        //  or idle timeout with no playback at all)
        if (geissActive) {
            qDebug() << "Playlist ended, switching Geiss to screensaver";
            geissActive = false;
        }
        if (!screenSaverActive) {
            qDebug() << "Activating screensaver";
            screenSaverActive = true;
            screenSaver->start();
            viewStack->setCurrentIndex(3);
            if (webState) webState->setView("screensaver");
        }
    }
}

void MainWindow::deactivateScreenSaver()
{
    if (!screenSaverActive && !geissActive) return;

    qDebug() << "Deactivating screensaver/visualizer";
    bool wasVisualizerActive = geissActive;
    screenSaverActive = false;
    geissActive = false;

    if (wasVisualizerActive) {
        viewTransition->fadeTo(0); // Crossfade back to player
    } else {
        viewStack->setCurrentIndex(0); // Screensaver exits instantly
    }
    if (webState) webState->setView("player");
    resetScreenSaverTimer();
}

void MainWindow::deactivateAvs()
{
    avsView->stop();
    viewTransition->fadeTo(0); // Crossfade back to player
    if (webState) webState->setView("player");
    resetScreenSaverTimer();
}

void MainWindow::resetScreenSaverTimer()
{
    // Restart the idle timer — it fires for both screensaver (no music)
    // and Geiss visualizer (music playing). activateScreenSaver() decides which.
    screenSaverTimer->stop();
    screenSaverTimer->start();
}

void MainWindow::showClockScreensaver(int themeIndex)
{
    // Force the clock screensaver regardless of playback/Geiss state.
    geissActive = false;
    screenSaverActive = true;
    screenSaver->start(themeIndex);
    viewStack->setCurrentIndex(3); // ScreenSaverView
    if (webState) webState->setView("screensaver");
    // Keep the idle timer running so normal idle behavior resumes after the
    // timeout: an API-selected face is shown now but is not pinned forever —
    // if music is playing it will yield to Geiss at the next idle activation.
    resetScreenSaverTimer();
}

void MainWindow::apiTriggerScreensaver()
{
    showClockScreensaver(-1); // random face
}

void MainWindow::apiDismissScreensaver()
{
    deactivateScreenSaver();
}

bool MainWindow::apiShowClockFace(int index)
{
    if (index < 0 || index >= ScreenSaverView::faceNames().size())
        return false;
    showClockScreensaver(index);
    return true;
}

static const QStringList kAudioExt = { "mp3","flac","wav","ogg","m4a","aac","opus" };

int MainWindow::apiPlaylistCurrent() const
{
    return m_playlist ? m_playlist->currentIndex() : -1;
}

QJsonArray MainWindow::apiPlaylist() const
{
    QJsonArray arr;
    if (!m_playlist || !m_playlistModel) return arr;
    const int cur = m_playlist->currentIndex();
    const int n = m_playlist->mediaCount();
    for (int i = 0; i < n; ++i) {
        QJsonObject o;
        o["index"] = i;
        QString title = m_playlistModel->data(m_playlistModel->index(i, PlaylistModel::Title)).toString();
        const QUrl u = m_playlist->media(i);
        if (title.isEmpty()) title = u.fileName();
        o["title"]    = title;
        o["artist"]   = m_playlistModel->data(m_playlistModel->index(i, PlaylistModel::Artist)).toString();
        o["duration"] = m_playlistModel->data(m_playlistModel->index(i, PlaylistModel::Duration)).toString();
        o["current"]  = (i == cur);
        arr.append(o);
    }
    return arr;
}

bool MainWindow::apiPlaylistPlay(int index)
{
    if (!m_playlist || !m_playlistModel || index < 0 || index >= m_playlist->mediaCount())
        return false;
    fileSource->jump(m_playlistModel->index(index, 0));
    return true;
}

bool MainWindow::apiPlaylistRemove(int index)
{
    if (!m_playlist || index < 0 || index >= m_playlist->mediaCount())
        return false;
    return m_playlist->removeMedia(index);
}

void MainWindow::apiPlaylistClear()
{
    if (m_playlist) m_playlist->clear();
}

bool MainWindow::resolveSandboxed(const QString &rel, QString &outAbs) const
{
    const QString rootCanon = QDir(m_musicRoot).canonicalPath();
    if (rootCanon.isEmpty()) return false;
    const QString candidate = QDir(rootCanon).filePath(rel.isEmpty() ? QStringLiteral(".") : rel);
    const QString canon = QFileInfo(candidate).canonicalFilePath();
    if (canon.isEmpty()) return false;                // does not exist
    if (canon != rootCanon && !canon.startsWith(rootCanon + "/")) return false; // escaped root
    outAbs = canon;
    return true;
}

QJsonObject MainWindow::apiBrowse(const QString &rel) const
{
    QJsonObject res;
    QString abs;
    if (!resolveSandboxed(rel, abs)) { res["ok"] = false; res["error"] = "invalid path"; return res; }
    QFileInfo fi(abs);
    if (!fi.isDir()) { res["ok"] = false; res["error"] = "not a directory"; return res; }

    const QString rootCanon = QDir(m_musicRoot).canonicalPath();
    QString relPath = QDir(rootCanon).relativeFilePath(abs);
    if (relPath == ".") relPath = "";

    res["ok"] = true;
    res["path"] = relPath;

    QJsonArray entries;
    QDir dir(abs);
    const auto list = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                        QDir::Name | QDir::DirsFirst);
    for (const QFileInfo &e : list) {
        const QString childRel = relPath.isEmpty() ? e.fileName() : relPath + "/" + e.fileName();
        if (e.isDir()) {
            QJsonObject o; o["name"] = e.fileName(); o["type"] = "dir"; o["path"] = childRel;
            entries.append(o);
        } else if (kAudioExt.contains(e.suffix().toLower())) {
            QJsonObject o; o["name"] = e.fileName(); o["type"] = "file"; o["path"] = childRel;
            o["size"] = static_cast<double>(e.size());
            entries.append(o);
        }
    }
    res["entries"] = entries;
    return res;
}

QJsonObject MainWindow::apiAddPath(const QString &rel)
{
    QJsonObject res;
    QString abs;
    if (!resolveSandboxed(rel, abs)) { res["ok"] = false; res["error"] = "invalid path"; return res; }
    QFileInfo fi(abs);
    QList<QUrl> urls;
    if (fi.isDir()) {
        QDir dir(abs);
        const auto files = dir.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &f : files)
            if (kAudioExt.contains(f.suffix().toLower()))
                urls << QUrl::fromLocalFile(f.absoluteFilePath());
    } else if (kAudioExt.contains(fi.suffix().toLower())) {
        urls << QUrl::fromLocalFile(abs);
    }
    if (urls.isEmpty()) { res["ok"] = false; res["error"] = "no audio files"; return res; }
    fileSource->addToPlaylist(urls);
    res["ok"] = true;
    res["added"] = urls.size();
    return res;
}

QJsonObject MainWindow::apiSources() const
{
    QJsonObject o;
    o["ok"] = true;
    o["current"] = coordinator->currentSourceLabel();
    QJsonArray arr;
    for (const QString &l : coordinator->sourceLabelList())
        arr.append(l);
    o["sources"] = arr;
    o["vban"] = apiVbanState();
    return o;
}

bool MainWindow::apiSetSource(const QString &nameOrIndex)
{
    const QStringList labels = coordinator->sourceLabelList();
    int idx = -1;
    for (int i = 0; i < labels.size(); ++i)
        if (labels[i].compare(nameOrIndex, Qt::CaseInsensitive) == 0) { idx = i; break; }
    if (idx < 0) {
        bool ok = false;
        const int n = nameOrIndex.toInt(&ok);
        if (ok && n >= 0 && n < labels.size()) idx = n;
    }
    if (idx < 0) return false;
    coordinator->setSource(idx);
    return true;
}

void MainWindow::apiVban(bool on)
{
    if (vbanSender) vbanSender->setEnabled(on);
}

bool MainWindow::apiVbanState() const
{
    return vbanSender ? vbanSender->isEnabled() : false;
}
