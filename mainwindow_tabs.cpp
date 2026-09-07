#include "mainwindow.h"
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
#include <QSizePolicy>
#include <QTabWidget>
#include <QTabBar>

#include <QShortcut>
#include <QKeySequence>
#include <QUrl>

// Configure one tab group. Every split group owns its own trailing plus tab,
// close handling, and tab-bar geometry rather than sharing window-global state.
void MainWindow::setupPaneTabWidget(QTabWidget *tabWidget)
{
    // Ignored horizontal size lets QSplitter allocate less than the tab bar's
    // ideal width. QTabBar then scrolls and elides instead of painting across
    // the neighboring pane's visual territory.
    tabWidget->setMinimumWidth(0);
    tabWidget->setSizePolicy(
        QSizePolicy::Ignored,
        QSizePolicy::Expanding
    );
    tabWidget->setTabsClosable(true);
    tabWidget->setMovable(true);

    QTabBar *tabBar = tabWidget->tabBar();
    tabBar->setExpanding(false);
    tabBar->setUsesScrollButtons(true);
    tabBar->setElideMode(Qt::ElideRight);

    // The placeholder is a real page so the compact plus remains immediately
    // after this group's rightmost real tab instead of at the pane's far edge.
    auto *newTabPlaceholder =
        new QWidget(tabWidget);
    newTabPlaceholders.insert(tabWidget, newTabPlaceholder);

    // Add the placeholder before browser pages. createTabInGroup() always
    // inserts immediately before this page, preserving the trailing position.
    int newTabIndex =
        tabWidget->addTab(
            newTabPlaceholder,
            "+"
        );

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

    // Clicking inside either group's tab bar activates that group. Its plus
    // button creates locally, so the right group never routes back to left.
    connect(
        tabBar,
        &QTabBar::tabBarClicked,
        this,
        [this, tabWidget, newTabPlaceholder](int index)
        {
            activeTabWidget = tabWidget;

            if (tabWidget->widget(index) == newTabPlaceholder)
            {
                createTabInGroup(tabWidget, QDir::homePath());
            }
        }
    );

    // Keyboard-driven tab changes do not necessarily click the tab bar. Keep
    // the group active whenever one of its real pages becomes current.
    connect(
        tabWidget,
        &QTabWidget::currentChanged,
        this,
        [this, tabWidget, newTabPlaceholder](int index)
        {
            QWidget *page = tabWidget->widget(index);

            if (page != nullptr && page != newTabPlaceholder)
            {
                activeTabWidget = tabWidget;
            }
        }
    );

    // Each tab close belongs to this one group. Closing its final real tab
    // closes the entire split group only when another group remains.
    connect(
        tabWidget,
        &QTabWidget::tabCloseRequested,
        this,
        [this, tabWidget, newTabPlaceholder](int index)
        {
            QWidget *page = tabWidget->widget(index);

            if (page == nullptr || page == newTabPlaceholder)
            {
                return;
            }

            auto stateIterator = tabStates.find(page);
            if (stateIterator == tabStates.end())
            {
                return;
            }

            if (tabWidget->count() <= 2)
            {
                closePane(page, stateIterator->activePane);
                return;
            }

            tabStates.erase(stateIterator);
            tabWidget->removeTab(index);
            page->deleteLater();
        }
    );

    // Browser tabs remain movable, but the plus placeholder must always
    // return to the final position after any drag-and-drop reordering.
    connect(
        tabBar,
        &QTabBar::tabMoved,
        this,
        [this, tabWidget, newTabPlaceholder](int, int)
        {
            // Find the placeholder again because moving tabs changes indexes.
            int newTabIndex =
                tabWidget->indexOf(newTabPlaceholder);

            // The final tab index is always one less than the tab count.
            int lastIndex =
                tabWidget->count() - 1;

            // Moving the placeholder to an already-correct position would
            // emit another tabMoved signal, so only move it when necessary.
            if (newTabIndex != lastIndex)
            {
                tabWidget->tabBar()->moveTab(
                    newTabIndex,
                    lastIndex
                );
            }
        }
    );
}

// Add a tab to whichever split group the user most recently interacted with.
void MainWindow::createTab(const QString &path)
{
    QTabWidget *target = activeTabWidget;

    if (target == nullptr && !paneTabWidgets.isEmpty())
    {
        target = paneTabWidgets.first();
    }

    createTabInGroup(target, path);
}

// Construct one real tab page inside a specific split group. A tab contains
// one BrowserPane, so changing tabs affects only that group and never replaces
// the neighboring group's current page.
QWidget *MainWindow::createTabInGroup(
    QTabWidget *tabWidget,
    const QString &path
)
{
    if (tabWidget == nullptr || !newTabPlaceholders.contains(tabWidget))
    {
        return nullptr;
    }

    auto *page = new QWidget(tabWidget);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    BrowserPane *pane = createBrowserPane(page);
    layout->addWidget(pane);

    TabState state;
    state.tabWidget = tabWidget;
    state.activePane = pane;
    tabStates.insert(page, state);

    pane->navigateTo(path);

    QWidget *placeholder = newTabPlaceholders.value(tabWidget);
    int placeholderIndex = tabWidget->indexOf(placeholder);
    int tabIndex = tabWidget->insertTab(placeholderIndex, page, QString());

    activeTabWidget = tabWidget;
    tabWidget->setCurrentIndex(tabIndex);
    updateTabTitle(page, pane->currentPath());
    updatePaneChrome();
    return page;
}

// Create a second independently clipped tab group inside the workspace split.
QTabWidget *MainWindow::createPaneTabWidget()
{
    if (
        browserPaneSplitter == nullptr ||
        paneTabWidgets.size() >= kMaxPaneGroups
    )
    {
        return nullptr;
    }

    auto *tabWidget = new QTabWidget(browserPaneSplitter);
    browserPaneSplitter->addWidget(tabWidget);
    browserPaneSplitter->setStretchFactor(
        paneTabWidgets.size(),
        1
    );

    paneTabWidgets.append(tabWidget);
    setupPaneTabWidget(tabWidget);

    const int total = qMax(browserPaneSplitter->width(), 2);
    const int each = total / paneTabWidgets.size();
    QList<int> sizes(paneTabWidgets.size(), each);
    sizes.last() += total - (each * paneTabWidgets.size());
    browserPaneSplitter->setSizes(sizes);

    return tabWidget;
}

// Shared construction for every real tab in either group.
// configureFileTreeView attaches the window-wide proxy so both groups and all
// their tabs share one size cache instead of walking the disk repeatedly.
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

    // Every pane owns one tab page, so its path always names that local tab.
    connect(
        pane,
        &BrowserPane::pathChanged,
        this,
        [this, page, pane](const QString &path)
        {
            updateTabTitle(page, path);
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

void MainWindow::updatePaneChrome()
{
    const bool canSplit = paneTabWidgets.size() < kMaxPaneGroups;
    const bool canClose = paneTabWidgets.size() > 1;

    for (const TabState &state : std::as_const(tabStates))
    {
        if (state.activePane != nullptr)
        {
            state.activePane->setSplitButtonVisible(canSplit);
            state.activePane->setCloseButtonVisible(canClose);
        }
    }
}

void MainWindow::splitPane(QWidget *page)
{
    auto stateIterator = tabStates.find(page);
    if (
        stateIterator == tabStates.end() ||
        stateIterator->activePane == nullptr ||
        paneTabWidgets.size() >= kMaxPaneGroups
    )
    {
        return;
    }

    QString startPath = stateIterator->activePane->currentPath();

    // A root can disappear between filesystem-model updates. A new group must
    // still begin on a valid page rather than showing an unrooted tree.
    if (startPath.isEmpty() || !QFileInfo(startPath).isDir())
    {
        startPath = QDir::homePath();
    }

    QTabWidget *newGroup = createPaneTabWidget();

    if (newGroup != nullptr)
    {
        createTabInGroup(newGroup, startPath);
    }
}

void MainWindow::closePane(QWidget *page, BrowserPane *pane)
{
    auto stateIterator = tabStates.find(page);
    if (
        stateIterator == tabStates.end() ||
        stateIterator->activePane != pane ||
        paneTabWidgets.size() <= 1
    )
    {
        return;
    }

    QTabWidget *group = stateIterator->tabWidget;

    // Removing a split group closes all tabs it owns. Collect page pointers
    // first because deleting the QTabWidget recursively destroys those pages.
    QList<QWidget *> groupPages;
    QWidget *placeholder = newTabPlaceholders.value(group);

    for (int index = 0; index < group->count(); ++index)
    {
        QWidget *groupPage = group->widget(index);
        if (groupPage != nullptr && groupPage != placeholder)
        {
            groupPages.append(groupPage);
        }
    }

    for (QWidget *groupPage : groupPages)
    {
        tabStates.remove(groupPage);
    }

    paneTabWidgets.removeAll(group);
    newTabPlaceholders.remove(group);

    // Hiding immediately removes the group from QSplitter's active layout.
    // deleteLater() then destroys it safely after the current button signal
    // finishes, including every tab page and BrowserPane it still owns.
    group->hide();
    group->deleteLater();

    activeTabWidget = paneTabWidgets.value(0, nullptr);

    if (activeTabWidget != nullptr)
    {
        activeTabWidget->setFocus(Qt::OtherFocusReason);
    }

    updatePaneChrome();
}

void MainWindow::setActivePane(QWidget *page, BrowserPane *pane)
{
    auto it = tabStates.find(page);
    if (it == tabStates.end())
    {
        return;
    }

    // A tab page owns exactly one immutable BrowserPane. Reject a mismatched
    // signal instead of allowing one page to point at another page's explorer.
    if (it->activePane != pane)
    {
        return;
    }

    activeTabWidget = it->tabWidget;

    if (it->tabWidget != nullptr)
    {
        it->tabWidget->setCurrentWidget(page);
    }
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
    auto stateIterator = tabStates.constFind(page);
    if (stateIterator == tabStates.constEnd() || stateIterator->tabWidget == nullptr)
    {
        return;
    }

    int tabIndex = stateIterator->tabWidget->indexOf(page);
    if (tabIndex != -1) {
        stateIterator->tabWidget->setTabText(tabIndex, tabName);
    }
}
