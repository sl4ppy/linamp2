cmake_minimum_required(VERSION 3.16)
project(player VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Concurrent DBus Gui Multimedia MultimediaWidgets Network Widgets)

qt_standard_project_setup()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/audiosource-base)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/audiosource-coordinator)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/audiosourcepython)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/audiosourcecd)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/audiosourcefile)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/shared)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-basewindow)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-menu)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-player)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-playlist)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-screensaver)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-avs)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/view-avs/effects)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/vban)

qt_add_executable(player WIN32 MACOSX_BUNDLE
    src/audiosource-base/audiosource.cpp
    src/audiosource-base/audiosource.h
    src/audiosource-base/audiosourcewspectrumcapture.cpp
    src/audiosource-base/audiosourcewspectrumcapture.h
    src/audiosourcecd/audiosourcecd.cpp
    src/audiosourcecd/audiosourcecd.h
    src/audiosourcepython/audiosourcepython.cpp
    src/audiosourcepython/audiosourcepython.h
    src/audiosource-coordinator/audiosourcecoordinator.cpp
    src/audiosource-coordinator/audiosourcecoordinator.h
    src/audiosourcefile/audiosourcefile.cpp
    src/audiosourcefile/audiosourcefile.h
    src/audiosourcefile/mediaplayer.cpp
    src/audiosourcefile/mediaplayer.h
    src/view-player/controlbuttonswidget.cpp
    src/view-player/controlbuttonswidget.h
    src/view-player/controlbuttonswidget.ui
    src/view-player/scrolltext.cpp
    src/view-player/scrolltext.h
    src/view-player/spectrumwidget.cpp
    src/view-player/spectrumwidget.h
    src/view-player/playerview.cpp
    src/view-player/playerview.h
    src/view-player/playerview.ui
    src/view-basewindow/desktopbasewindow.cpp
    src/view-basewindow/desktopbasewindow.h
    src/view-basewindow/desktopbasewindow.ui
    src/view-basewindow/desktopplayerwindow.cpp
    src/view-basewindow/desktopplayerwindow.h
    src/view-basewindow/desktopplayerwindow.ui
    src/view-basewindow/embeddedbasewindow.cpp
    src/view-basewindow/embeddedbasewindow.h
    src/view-basewindow/embeddedbasewindow.ui
    src/view-basewindow/mainwindow.cpp
    src/view-basewindow/mainwindow.h
    src/view-basewindow/titlebar.cpp
    src/view-basewindow/titlebar.h
    src/view-basewindow/titlebar.ui
    src/view-playlist/filebrowsericonprovider.cpp
    src/view-playlist/filebrowsericonprovider.h
    src/view-playlist/playlistmodel.cpp
    src/view-playlist/playlistmodel.h
    src/view-playlist/playlistview.cpp
    src/view-playlist/playlistview.h
    src/view-playlist/playlistview.ui
    src/view-playlist/qmediaplaylist.cpp
    src/view-playlist/qmediaplaylist.h
    src/view-playlist/qmediaplaylist_p.cpp
    src/view-playlist/qmediaplaylist_p.h
    src/view-playlist/qplaylistfileparser.cpp
    src/view-playlist/qplaylistfileparser.h
    src/view-menu/mainmenuview.cpp
    src/view-menu/mainmenuview.h
    src/view-menu/mainmenuview.ui
    src/view-screensaver/screensaverview.cpp
    src/view-screensaver/screensaverview.h
    src/view-screensaver/screensaverview.ui
    src/view-avs/avsframebuffer.cpp
    src/view-avs/avsframebuffer.h
    src/view-avs/avsaudiodata.cpp
    src/view-avs/avsaudiodata.h
    src/view-avs/avseffect.h
    src/view-avs/avsengine.cpp
    src/view-avs/avsengine.h
    src/view-avs/avsview.cpp
    src/view-avs/avsview.h
    src/view-avs/effects/avsclearscreen.cpp
    src/view-avs/effects/avsclearscreen.h
    src/view-avs/effects/avsfadeout.cpp
    src/view-avs/effects/avsfadeout.h
    src/view-avs/effects/avssuperscope.cpp
    src/view-avs/effects/avssuperscope.h
    src/view-avs/effects/avsmovement.cpp
    src/view-avs/effects/avsmovement.h
    src/view-avs/effects/avscolormodifier.cpp
    src/view-avs/effects/avscolormodifier.h
    src/view-avs/effects/avsonbeatclear.cpp
    src/view-avs/effects/avsonbeatclear.h
    src/view-avs/effects/avsgrain.cpp
    src/view-avs/effects/avsgrain.h
    src/view-avs/effects/avsmirror.cpp
    src/view-avs/effects/avsmirror.h
    src/view-avs/effects/avsring.cpp
    src/view-avs/effects/avsring.h
    src/view-avs/effects/avsstarfield.cpp
    src/view-avs/effects/avsstarfield.h
    src/view-avs/effects/avswater.cpp
    src/view-avs/effects/avswater.h
    src/view-avs/effects/avsdynamicmovement.cpp
    src/view-avs/effects/avsdynamicmovement.h
    src/view-avs/effects/avsblur.cpp
    src/view-avs/effects/avsblur.h
    src/view-avs/effects/avsmosaic.cpp
    src/view-avs/effects/avsmosaic.h
    src/view-avs/effects/avsbufferblend.cpp
    src/view-avs/effects/avsbufferblend.h
    src/view-avs/effects/avsclock.cpp
    src/view-avs/effects/avsclock.h
    src/vban/vbansender.cpp
    src/vban/vbansender.h
    src/shared/scale.cpp
    src/shared/scale.h
    src/shared/systemaudiocontrol.cpp
    src/shared/systemaudiocontrol.h
    src/shared/fft.cpp
    src/shared/fft.h
    src/shared/util.cpp
    src/shared/util.h
    src/shared/linampslider.h
    src/shared/linampslider.cpp
    src/main.cpp
    uiassets.qrc
)

target_include_directories(player PRIVATE
    /usr/include/pipewire-0.3
    /usr/include/python3.12
    /usr/include/spa-0.2
)

target_link_libraries(player PRIVATE
    # Remove: L/usr/lib/python3.11/config-3.11-x86_64-linux-gnu/
    Qt::Concurrent
    Qt::Core
    Qt::DBus
    Qt::Gui
    Qt::Multimedia
    Qt::MultimediaWidgets
    Qt::Network
    Qt::Widgets
    asound
    pipewire-0.3
    pulse
    pulse-simple
    python3.12
    tag
)

install(TARGETS player
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET player
    FILENAME_VARIABLE deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
