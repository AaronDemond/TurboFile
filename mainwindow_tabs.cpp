#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileSystemModel>
#include <QDir>
#include <QFileInfo>

#include <QLineEdit>
#include <QTreeView>
#include <QPushButton>
#include <QWidget>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>

#include <QShortcut>
#include <QKeySequence>

// Build a single browser tab with its own layout, widgets, and tab state.
void MainWindow::createTab(const QString &path)
{
    // QWidget acts as the container for everything displayed in a single tab.
    auto *page = new QWidget();

    // Vertical layout: navigation bar at the top, file tree below it.
    auto *mainLayout = new QVBoxLayout(page);

    // Horizontal navigation bar: Back | Forward | Up | path.
    auto *topLayout = new QHBoxLayout();

    // Create the controls that make up the tab's browser UI.
    auto *backButton = new QPushButton("<", page);
    auto *forwardButton = new QPushButton(">", page);
    auto *upButton = new QPushButton("Up", page);
    auto *pathLineEdit = new QLineEdit(page);
    auto *fileTreeView = new QTreeView(page);

    // button styling
    QString buttonStyle =
        "QPushButton { background-color: orange; color: black; border: 1px solid black; }"
        "QPushButton:hover { background-color: green; }"
        "QPushButton:pressed { background-color: red }"
        "QPushButton:disabled {background-color: grey; color: black; border: 1px solid black}";
    backButton->setFixedSize(32, 28);
    forwardButton->setFixedSize(32, 28);
    upButton->setFixedSize(50, 28);
    upButton->setStyleSheet(buttonStyle);


    // Place the navigation controls above the file tree.
    topLayout->addWidget(backButton);
    topLayout->addWidget(forwardButton);
    topLayout->addWidget(upButton);
    topLayout->addWidget(pathLineEdit);

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(fileTreeView);

    // All tabs share the same filesystem model so each view can show different roots.
    fileTreeView->setModel(fileModel);

    // set the default width of of the file columns
    // 0 = Name, 1 = Size, 2 = Type, 3 = Date Modified
    fileTreeView->setColumnWidth(0, 355);
    fileTreeView->setColumnWidth(2, 100);
    fileTreeView->setColumnWidth(3, 100);
    fileTreeView->setColumnWidth(3, 170);

    // Save the per-tab widgets and history state before navigation begins.
    TabState state;
    state.fileTreeView = fileTreeView;
    state.pathLineEdit = pathLineEdit;
    state.backButton = backButton;
    state.forwardButton = forwardButton;

    tabStates[page] = state;

    // Add the tab to the UI and position it for the newly created page.
    int tabIndex = ui->tabWidget->addTab(page, "");

    navigateTo(page, path);
    ui->tabWidget->setCurrentIndex(tabIndex);

    // Connect all of the user interactions for this tab.
    setupTabConnections(
        page,
        backButton,
        forwardButton,
        upButton,
        fileTreeView
    );
}

// Connect the relevant signals for a single browser tab.
void MainWindow::setupTabConnections(
    QWidget *page,
    QPushButton *backButton,
    QPushButton *forwardButton,
    QPushButton *upButton,
    QTreeView *fileTreeView
)
{
    // Double-clicking a directory should open it in the current tab.
    connect(
        fileTreeView,
        &QTreeView::doubleClicked,
        page,
        [this, page](const QModelIndex &index)
        {
            openDirectory(page, index);
        }
    );

    // Going up navigates to the parent directory of the current view.
    connect(
        upButton,
        &QPushButton::clicked,
        page,
        [this, page]()
        {
            goUp(page);
        }
    );

    // Back steps through the current tab's history in reverse chronological order.
    connect(
        backButton,
        &QPushButton::clicked,
        page,
        [this, page]()
        {
            goBack(page);
        }
    );

    // Forward steps forward through the current tab's history.
    connect(
        forwardButton,
        &QPushButton::clicked,
        page,
        [this, page]()
        {
            goForward(page);
        }
    );

    // -----------------------
    // PATH BAR
    // -----------------------

    // Get path bar belonging to this tab
    // Press enter while editing the path bar should trigger navigation.

    QLineEdit *pathLineEdit = tabStates[page].pathLineEdit;

    // QLineEdit emites returnPressed signal when the user presses Enter.
    connect(
        pathLineEdit,
        &QLineEdit::returnPressed,
        page,
        [this, page]()
        {
            navigateFromPathBar(page);
        }
    );

}

// Update the tab label to match the name of the directory currently displayed.
void MainWindow::updateTabTitle(QWidget *page, const QString &path) {

    // normalize the path
    QString cleanPath = QDir::cleanPath(path);

    // get the tab name
    QString tabName = QFileInfo(cleanPath).fileName();

    // check if path is "/"
    if (tabName.isEmpty()) {
        tabName = cleanPath;
    }

    // ensure the tab has not been closed and then set the tab text
    int tabIndex = ui->tabWidget->indexOf(page);
    if (tabIndex != -1) {
        ui->tabWidget->setTabText(tabIndex, tabName);
    }
}
