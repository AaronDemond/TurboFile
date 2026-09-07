#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "browser/browserconstants.h"
#include "browser/browserpane.h"

#include <QFileSystemModel>
#include <QDir>
#include <QFileInfo>

#include <QLineEdit>
#include <QTreeView>
#include <QPushButton>
#include <QWidget>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QTabBar>

#include <QShortcut>
#include <QKeySequence>
#include <QUrl>

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

// Build a tab that starts with one independent explorer pane. A second
// pane is added later through BrowserPane::splitRequested, not here.
void MainWindow::createTab(const QString &path)
{
    auto *page = new QWidget();
    auto *mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Horizontal splitter holds one or two BrowserPane widgets. Children
    // are not collapsible so a pane cannot be dragged away; Close is the
    // only way to remove the extra explorer.
    auto *splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(kPaneSplitterHandleWidth);
    splitter->setStyleSheet(
        QStringLiteral(
            "QSplitter::handle:horizontal {"
            "  width: %1px;"
            "}"
            "QSplitter::handle:horizontal:hover {"
            "  background-color: palette(mid);"
            "}"
        ).arg(kPaneSplitterHandleWidth)
    );

    TabState state;
    state.paneSplitter = splitter;
    tabStates[page] = state;

    BrowserPane *pane = createBrowserPane(page);
    splitter->addWidget(pane);
    splitter->setStretchFactor(0, 1);

    tabStates[page].panes.append(pane);
    tabStates[page].activePane = pane;
    updatePaneChrome(page);

    mainLayout->addWidget(splitter);

    pane->navigateTo(path);

    int newTabIndex =
        ui->tabWidget->indexOf(newTabPlaceholder);

    int tabIndex =
        ui->tabWidget->insertTab(
            newTabIndex,
            page,
            ""
        );

    ui->tabWidget->setCurrentIndex(tabIndex);
    updateTabTitle(page, pane->currentPath());
}

// Shared construction for the first pane and for a later split pane.
// configureFileTreeView attaches the window-wide proxy so both panes
// share one size cache instead of walking the disk twice.
BrowserPane *MainWindow::createBrowserPane(QWidget *page)
{
    auto *pane = new BrowserPane(fileModel, page);
    configureFileTreeView(pane->fileTreeView());
    pane->fileTreeView()->setContextMenuPolicy(Qt::CustomContextMenu);
    setupPaneConnections(page, pane);
    return pane;
}

void MainWindow::setupPaneConnections(QWidget *page, BrowserPane *pane)
{
    connect(
        pane,
        &BrowserPane::activated,
        this,
        [this, page, pane]()
        {
            setActivePane(page, pane);
        }
    );

    connect(
        pane,
        &BrowserPane::splitRequested,
        this,
        [this, page]()
        {
            splitPane(page);
        }
    );

    connect(
        pane,
        &BrowserPane::closeRequested,
        this,
        [this, page, pane]()
        {
            closePane(page, pane);
        }
    );

    // Tab titles follow the left pane only so browsing in the right pane
    // does not rename the tab out from under the user.
    connect(
        pane,
        &BrowserPane::pathChanged,
        this,
        [this, page, pane](const QString &path)
        {
            if (tabStates.value(page).panes.value(0) == pane)
            {
                updateTabTitle(page, path);
            }
        }
    );

    connect(
        pane,
        &BrowserPane::filesDropped,
        this,
        [this](const QList<QUrl> &urls, const QString &destination)
        {
            copyUrlsIntoDirectory(urls, destination);
        }
    );

    QTreeView *fileTreeView = pane->fileTreeView();

    connect(
        fileTreeView,
        &QTreeView::doubleClicked,
        this,
        [this, page, pane](const QModelIndex &index)
        {
            setActivePane(page, pane);
            openItem(page, index);
        }
    );

    connect(
        fileTreeView,
        &QTreeView::customContextMenuRequested,
        this,
        [this, page, pane](const QPoint &position)
        {
            setActivePane(page, pane);
            showFileContextMenu(page, position);
        }
    );
}

void MainWindow::updatePaneChrome(QWidget *page)
{
    const TabState &state = tabStates[page];
    const bool canSplit = state.panes.size() < kMaxPanesPerTab;
    const bool canClose = state.panes.size() > 1;

    for (BrowserPane *pane : state.panes)
    {
        pane->setSplitButtonVisible(canSplit);
        pane->setCloseButtonVisible(canClose);
    }
}

void MainWindow::splitPane(QWidget *page)
{
    TabState &state = tabStates[page];
    if (state.panes.size() >= kMaxPanesPerTab)
    {
        return;
    }

    const QString startPath =
        state.activePane != nullptr
            ? state.activePane->currentPath()
            : QDir::homePath();

    BrowserPane *pane = createBrowserPane(page);
    state.paneSplitter->addWidget(pane);
    state.paneSplitter->setStretchFactor(state.panes.size(), 1);
    state.panes.append(pane);
    pane->navigateTo(startPath);
    setActivePane(page, pane);
    updatePaneChrome(page);

    const int total = qMax(state.paneSplitter->width(), 2);
    const int each = total / 2;
    state.paneSplitter->setSizes({each, total - each});
}

void MainWindow::closePane(QWidget *page, BrowserPane *pane)
{
    TabState &state = tabStates[page];
    if (state.panes.size() <= 1)
    {
        return;
    }

    state.panes.removeAll(pane);
    if (state.activePane == pane)
    {
        state.activePane = state.panes.first();
    }

    pane->deleteLater();
    updatePaneChrome(page);
    updateTabTitle(page, state.panes.first()->currentPath());
}

void MainWindow::setActivePane(QWidget *page, BrowserPane *pane)
{
    auto it = tabStates.find(page);
    if (it == tabStates.end())
    {
        return;
    }

    it->activePane = pane;
}

QTreeView *MainWindow::fileTreeForPage(QWidget *page) const
{
    auto it = tabStates.constFind(page);
    if (it == tabStates.constEnd() || it->activePane == nullptr)
    {
        return nullptr;
    }

    return it->activePane->fileTreeView();
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
