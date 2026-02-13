#include "mainmenuview.h"
#include "ui_mainmenuview.h"

MainMenuView::MainMenuView(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MainMenuView)
{
    ui->setupUi(this);

    connect(ui->backButton, &QPushButton::clicked, this, &MainMenuView::backClicked);
    connect(ui->fileSourceButton, &QPushButton::clicked, this, &MainMenuView::fileSourceClicked);
    connect(ui->btSourceButton, &QPushButton::clicked, this, &MainMenuView::btSourceClicked);
    connect(ui->spotifySourceButton, &QPushButton::clicked, this, &MainMenuView::spotifySourceClicked);
    connect(ui->cdSourceButton, &QPushButton::clicked, this, &MainMenuView::cdSourceClicked);
    connect(ui->vbanButton, &QPushButton::clicked, this, &MainMenuView::vbanButtonClicked);

    updateVbanButtonStyle();
}

MainMenuView::~MainMenuView()
{
    delete ui;
}

void MainMenuView::fileSourceClicked()
{
    emit sourceSelected(0);
    emit backClicked();
}

void MainMenuView::btSourceClicked()
{
    emit sourceSelected(1);
    emit backClicked();
}

void MainMenuView::spotifySourceClicked()
{
    emit sourceSelected(3);
    emit backClicked();
}

void MainMenuView::cdSourceClicked()
{
    emit sourceSelected(2);
    emit backClicked();
}

void MainMenuView::vbanButtonClicked()
{
    vbanEnabled = !vbanEnabled;
    updateVbanButtonStyle();
    emit vbanToggled(vbanEnabled);
}

void MainMenuView::setVbanEnabled(bool enabled)
{
    vbanEnabled = enabled;
    updateVbanButtonStyle();
}

void MainMenuView::updateVbanButtonStyle()
{
    if (vbanEnabled) {
        ui->vbanButton->setStyleSheet(
            "QPushButton {"
            "  color: #D6DEFF;"
            "  background-color: #3A5A3A;"
            "  border: 0px;"
            "  border-radius: 0px;"
            "}"
            "QPushButton:pressed {"
            "  color: #D6DEFF;"
            "  background-color: #4A6A4A;"
            "  border: 0px;"
            "}"
        );
    } else {
        ui->vbanButton->setStyleSheet(
            "QPushButton {"
            "  color: #D6DEFF;"
            "  background-color: #3F3F60;"
            "  border: 0px;"
            "  border-radius: 0px;"
            "}"
            "QPushButton:pressed {"
            "  color: #D6DEFF;"
            "  background-color: #4A4A71;"
            "  border: 0px;"
            "}"
        );
    }
}

void MainMenuView::shutdown()
{
    QString appPath = QCoreApplication::applicationDirPath();
    QString cmd = appPath + "/shutdown.sh";

    shutdownProcess = new QProcess(this);
    shutdownProcess->start(cmd);
}
