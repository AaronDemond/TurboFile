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
#include <QTabBar>

#include <QShortcut>
#include <QKeySequence>

// Add a permanent trailing tab that acts as the new-tab button.
void MainWindow::setupNewTabButton()
{
    // The placeholder is a real tab page so it stays directly beside
    // the rightmost browser tab instead of at the window's far edge.
    newTabPlaceholder =
        new QWidget(ui->tabWidget);

    // Add the placeholder before any browser tabs are created.
    // Browser tabs will always be inserted immediately before it.
    int newTabIndex =
        ui->tabWidget->addTab(
            newTabPlaceholder,
            "+"
        );

    // QTabWidget owns the QTabBar. We borrow its pointer to configure
    // the placeholder's appearance and tab-specific interactions.
    QTabBar *tabBar =
        ui->tabWidget->tabBar();

    // Explain the compact plus control when the user hovers over it.
    tabBar->setTabToolTip(newTabIndex, "New tab");

    // Closing the active rightmost browser tab should activate the
    // previous browser tab instead of exposing the placeholder page.
    tabBar->setSelectionBehaviorOnRemove(
        QTabBar::SelectPreviousTab
    );

    // Tabs may place their close button on either side depending on
    // the desktop style, so clear both positions for the placeholder.
    tabBar->setTabButton(
        newTabIndex,
        QTabBar::LeftSide,
        nullptr
    );

    tabBar->setTabButton(
        newTabIndex,
        QTabBar::RightSide,
        nullptr
    );

    // Clicking the placeholder creates a normal home-directory tab.
    // createTab() makes that new browser tab current immediately.
    connect(
        tabBar,
        &QTabBar::tabBarClicked,
        this,
        [this](int index)
        {
            if (ui->tabWidget->widget(index) == newTabPlaceholder)
            {
                createTab(QDir::homePath());
            }
        }
    );

    // Browser tabs remain movable, but the plus placeholder must always
    // return to the final position after any drag-and-drop reordering.
    connect(
        tabBar,
        &QTabBar::tabMoved,
        this,
        [this](int, int)
        {
            // Find the placeholder again because moving tabs changes indexes.
            int newTabIndex =
                ui->tabWidget->indexOf(newTabPlaceholder);

            // The final tab index is always one less than the tab count.
            int lastIndex =
                ui->tabWidget->count() - 1;

            // Moving the placeholder to an already-correct position would
            // emit another tabMoved signal, so only move it when necessary.
            if (newTabIndex != lastIndex)
            {
                ui->tabWidget->tabBar()->moveTab(
                    newTabIndex,
                    lastIndex
                );
            }
        }
    );
}

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



    // Generate the file tree vie used by the page
    auto *fileTreeView = new QTreeView(page);
    configureFileTreeView(fileTreeView);
    fileTreeView->setContextMenuPolicy(Qt::CustomContextMenu);


    // Place the navigation controls above the file tree.
    topLayout->addWidget(backButton);
    topLayout->addWidget(forwardButton);
    topLayout->addWidget(upButton);
    topLayout->addWidget(pathLineEdit);

    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(fileTreeView);


    // Save the per-tab widgets and history state before navigation begins.
    TabState state;
    state.fileTreeView = fileTreeView;
    state.pathLineEdit = pathLineEdit;
    state.backButton = backButton;
    state.forwardButton = forwardButton;

    tabStates[page] = state;

    // Find the trailing plus placeholder at its current index. Its index can
    // change when users reorder existing tabs, so it must be looked up here.
    int newTabIndex =
        ui->tabWidget->indexOf(newTabPlaceholder);

    // Insert the browser tab directly before the plus placeholder. This keeps
    // the plus control immediately to the right of every open browser tab.
    int tabIndex =
        ui->tabWidget->insertTab(
            newTabIndex,
            page,
            ""
        );

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
            openItem(page, index);
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

   // ---------------------------------------------------  
   // FILE CONTEXT MENU
   // ---------------------------------------------------

   // This signal fires when a user right clicks inside the QTreeView
   connect(
        fileTreeView,
        &QTreeView::customContextMenuRequested,
        this,
        [this, page] (const QPoint &position) {
            showFileContextMenu(
                page,
                position
            );
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
