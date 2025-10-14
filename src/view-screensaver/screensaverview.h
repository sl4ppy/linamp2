#ifndef SCREENSAVERVIEW_H
#define SCREENSAVERVIEW_H

#include <QWidget>
#include <QTimer>
#include <QDateTime>

namespace Ui {
class ScreenSaverView;
}

class ScreenSaverView : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenSaverView(QWidget *parent = nullptr);
    ~ScreenSaverView();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

signals:
    void userActivityDetected();

private slots:
    void updateClock();

private:
    Ui::ScreenSaverView *ui;
    QTimer *clockUpdateTimer = nullptr;
    QString currentTime;
    QString currentDate;

    void formatTime();
};

#endif // SCREENSAVERVIEW_H
