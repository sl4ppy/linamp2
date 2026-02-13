#ifndef MAINMENUVIEW_H
#define MAINMENUVIEW_H

#include <QWidget>
#include <QProcess>

namespace Ui {
class MainMenuView;
}

class MainMenuView : public QWidget
{
    Q_OBJECT

public:
    explicit MainMenuView(QWidget *parent = nullptr);
    ~MainMenuView();

public slots:
    void setVbanEnabled(bool enabled);

signals:
    void sourceSelected(int source);
    void backClicked();
    void vbanToggled(bool enabled);

private:
    Ui::MainMenuView *ui;

    void fileSourceClicked();
    void btSourceClicked();
    void spotifySourceClicked();
    void cdSourceClicked();
    void vbanButtonClicked();

    void updateVbanButtonStyle();

    bool vbanEnabled = false;

    QProcess *shutdownProcess = nullptr;
    void shutdown();
};

#endif // MAINMENUVIEW_H
