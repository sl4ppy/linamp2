#include "screensaverview.h"
#include "ui_screensaverview.h"
#include "scale.h"
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QKeyEvent>

ScreenSaverView::ScreenSaverView(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ScreenSaverView)
{
    ui->setupUi(this);

    // Set black background
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    // Setup clock update timer (update every second)
    clockUpdateTimer = new QTimer(this);
    connect(clockUpdateTimer, &QTimer::timeout, this, &ScreenSaverView::updateClock);
    clockUpdateTimer->setInterval(1000);

    // Initial time format
    formatTime();

    // Start the timer
    clockUpdateTimer->start();
}

ScreenSaverView::~ScreenSaverView()
{
    delete ui;
}

void ScreenSaverView::formatTime()
{
    QDateTime now = QDateTime::currentDateTime();
    currentTime = now.toString("hh:mm:ss");
    currentDate = now.toString("dddd, MMMM d, yyyy");
}

void ScreenSaverView::updateClock()
{
    formatTime();
    update(); // Trigger repaint
}

void ScreenSaverView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Fill background with black
    painter.fillRect(rect(), Qt::black);

    // Set up fonts for time
    QFont timeFont;
    timeFont.setFamily("Arial");
    timeFont.setPixelSize(48 * UI_SCALE);
    timeFont.setBold(true);

    QFont dateFont;
    dateFont.setFamily("Arial");
    dateFont.setPixelSize(16 * UI_SCALE);

    // Draw time in center
    painter.setFont(timeFont);
    painter.setPen(QColor(0, 255, 0)); // Green text (retro look)

    QFontMetrics timeFm(timeFont);
    int timeWidth = timeFm.horizontalAdvance(currentTime);
    int timeHeight = timeFm.height();
    int timeX = (width() - timeWidth) / 2;
    int timeY = (height() / 2) - (timeHeight / 4);

    painter.drawText(timeX, timeY, currentTime);

    // Draw date below time
    painter.setFont(dateFont);
    painter.setPen(QColor(0, 200, 0)); // Slightly darker green

    QFontMetrics dateFm(dateFont);
    int dateWidth = dateFm.horizontalAdvance(currentDate);
    int dateX = (width() - dateWidth) / 2;
    int dateY = timeY + timeHeight + (10 * UI_SCALE);

    painter.drawText(dateX, dateY, currentDate);
}

void ScreenSaverView::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    emit userActivityDetected();
}

void ScreenSaverView::keyPressEvent(QKeyEvent *event)
{
    Q_UNUSED(event);
    emit userActivityDetected();
}
