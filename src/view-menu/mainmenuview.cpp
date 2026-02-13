#include "mainmenuview.h"
#include "ui_mainmenuview.h"
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpacerItem>

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

    // Create AVS "Visuals" tile programmatically
    QWidget *avsWidget = new QWidget();
    QVBoxLayout *avsLayout = new QVBoxLayout(avsWidget);
    avsLayout->setSpacing(14);
    avsLayout->setContentsMargins(0, 0, 0, 0);

    QPushButton *avsButton = new QPushButton();
    avsButton->setFixedSize(225, 225);
    avsButton->setFont(QFont("DejaVu Sans Mono", 32, QFont::Bold));
    avsButton->setText("AVS");
    avsButton->setStyleSheet(
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
    avsLayout->addWidget(avsButton);

    QHBoxLayout *avsLabelLayout = new QHBoxLayout();
    avsLabelLayout->setSpacing(0);
    avsLabelLayout->setContentsMargins(0, 0, 0, 0);
    QSpacerItem *avsLabelSpacer = new QSpacerItem(4, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);
    avsLabelLayout->addItem(avsLabelSpacer);
    QLabel *avsLabel = new QLabel("Visuals");
    avsLabel->setFixedHeight(24);
    avsLabel->setFont(QFont("DejaVu Sans", 19));
    avsLabel->setStyleSheet("QLabel { color: #D6DEFF; }");
    avsLabelLayout->addWidget(avsLabel);
    avsLayout->addLayout(avsLabelLayout);

    avsLayout->addSpacerItem(new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));

    // Insert before the trailing spacer in the scroll area's horizontal layout
    QHBoxLayout *scrollLayout = qobject_cast<QHBoxLayout *>(ui->scrollAreaWidgetContents->layout());
    if (scrollLayout) {
        // Insert before the last item (trailing spacer)
        int insertPos = scrollLayout->count() - 1;
        scrollLayout->insertWidget(insertPos, avsWidget);
    }

    // Widen scroll area contents to accommodate new tile
    ui->scrollAreaWidgetContents->setFixedWidth(1800);

    connect(avsButton, &QPushButton::clicked, this, &MainMenuView::avsClicked);

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
