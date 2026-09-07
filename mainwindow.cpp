#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "browser/browserconstants.h"
#include "browser/browserpane.h"
#include "directorysizesortproxymodel.h"
#include "sidebar/pinnedconstants.h"
#include "sidebar/pinnedsidebar.h"

// Model and filesystem types used to provide directory contents and tab labels.
#include <QDir>
#include <QFileInfo>

// Widgets created dynamically for each file-browser tab.
#include <QLineEdit>
#include <QTreeView>
#include <QPushButton>
#include <QWidget>

// Layouts arrange the navigation controls above the file tree.
#include <QVBoxLayout>
#include <QHBoxLayout>
// Splitter holds the pinned sidebar and the tab widget.
#include <QByteArray>
#include <QCloseEvent>
#include <QList>
#include <QSplitter>
#include <QSettings>
#include <QStringList>
#include <QTimer>

// The UI contains this widget, which manages the browser tabs.
#include <QTabWidget>

// Keyboard shortcut support for creating tabs.
#include <QShortcut>
#include <QKeySequence>

// Initialize the generated UI and the shared filesystem sorting model. The
// proxy owns the lazy source model, while MainWindow owns the proxy through
// QObject parenting; every tab receives indexes from this same model chain.
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , fileModel(new DirectorySizeSortProxyModel(this))
{
    // Create the widgets declared in mainwindow.ui.
    ui->setupUi(this);

    // Sidebar sits beside the tab widget, not inside each tab.
    setupSidebar();

    // Start QFileSystemModel's asynchronous directory loading at the home path.
    // Individual tabs later choose independent root indexes within this model.
    fileModel->setRootPath(QDir::homePath());

    // The Designer tab widget becomes the first independent browser group.
    // A second QTabWidget is added beside it only after the user clicks Split.
    paneTabWidgets.append(ui->tabWidget);
    activeTabWidget = ui->tabWidget;
    setupPaneTabWidget(ui->tabWidget);

    // Recreate the previous session's tabs and panes. First run, or a
    // corrupt/empty session, still opens a single home-directory tab.
    if (!restoreSession())
    {
        createTab(QDir::homePath());
    }

    // Ctrl+T creates another home-directory browser tab.
    auto *newTabShortcut = new QShortcut(QKeySequence("Ctrl+T"), this);

    connect(newTabShortcut, &QShortcut::activated, this, [this]() {
        createTab(QDir::homePath());
    });

}

void MainWindow::setupSidebar()
{
    pinnedSidebar = new PinnedSidebar(ui->centralwidget);

    // Reparent the .ui tab widget into a splitter so the user can drag
    // the sidebar width. Children are not collapsible; collapse is the
    // chevron button, not dragging the handle to zero.
    sidebarSplitter = new QSplitter(Qt::Horizontal, ui->centralwidget);
    sidebarSplitter->setChildrenCollapsible(false);
    // Style sheets can ignore setHandleWidth unless the handle width is
    // also declared here. Hover fill shows the grab area without a hard line.
    sidebarSplitter->setHandleWidth(kSidebarSplitterHandleWidth);
    sidebarSplitter->setStyleSheet(
        QStringLiteral(
            "QSplitter::handle:horizontal {"
            "  width: %1px;"
            "}"
            "QSplitter::handle:horizontal:hover {"
            "  background-color: palette(mid);"
            "}"
        ).arg(kSidebarSplitterHandleWidth)
    );
    // The browser workspace owns one or two QTabWidgets. Keeping this split
    // outside every tab page is what lets each side switch tabs independently.
    browserPaneSplitter =
        new QSplitter(Qt::Horizontal, sidebarSplitter);
    browserPaneSplitter->setChildrenCollapsible(false);
    browserPaneSplitter->setHandleWidth(kPaneSplitterHandleWidth);
    browserPaneSplitter->setStyleSheet(
        QStringLiteral(
            "QSplitter::handle:horizontal {"
            "  width: %1px;"
            "}"
            "QSplitter::handle:horizontal:hover {"
            "  background-color: palette(mid);"
            "}"
        ).arg(kPaneSplitterHandleWidth)
    );

    // ui->tabWidget starts inside the Designer layout. Removing it before
    // addWidget() makes the reparenting explicit and avoids a layout warning.
    auto *rootLayout =
        qobject_cast<QHBoxLayout *>(ui->centralwidget->layout());
    rootLayout->removeWidget(ui->tabWidget);
    browserPaneSplitter->addWidget(ui->tabWidget);
    browserPaneSplitter->setStretchFactor(0, 1);

    sidebarSplitter->addWidget(pinnedSidebar);
    sidebarSplitter->addWidget(browserPaneSplitter);
    sidebarSplitter->setStretchFactor(0, 0);
    sidebarSplitter->setStretchFactor(1, 1);

    // QSettings::setValue can sync to disk. Doing that from splitterMoved
    // stalls the UI on every pixel. A single-shot timer writes once after
    // the handle stops moving. Restarting the timer coalesces the burst.
    sidebarLayoutSaveTimer = new QTimer(this);
    sidebarLayoutSaveTimer->setSingleShot(true);
    sidebarLayoutSaveTimer->setInterval(250);
    connect(
        sidebarLayoutSaveTimer,
        &QTimer::timeout,
        this,
        &MainWindow::saveSidebarLayout
    );

    rootLayout->addWidget(sidebarSplitter);

    QSettings settings(
        QStringLiteral("TurboFile"),
        QStringLiteral("TurboFile")
    );
    expandedSidebarWidth =
        settings.value(
            QStringLiteral("sidebar/width"),
            kSidebarHintWidth
        ).toInt();
    if (expandedSidebarWidth <= 0)
    {
        expandedSidebarWidth = kSidebarHintWidth;
    }

    const bool collapsed =
        settings.value(
            QStringLiteral("sidebar/collapsed"),
            false
        ).toBool();

    // Clicking a pin navigates the current tab in the most recently active
    // split group. A pin never forces navigation back to the left group.
    connect(
        pinnedSidebar,
        &PinnedSidebar::directoryActivated,
        this,
        [this](const QString &path)
        {
            if (activeTabWidget == nullptr)
            {
                return;
            }

            QWidget *page = activeTabWidget->currentWidget();
            QWidget *placeholder =
                newTabPlaceholders.value(activeTabWidget);

            if (page != nullptr && page != placeholder)
            {
                navigateTo(page, path);
            }
        }
    );
    connect(
        pinnedSidebar,
        &PinnedSidebar::collapsedChanged,
        this,
        [this](bool)
        {
            applySidebarSplitterSizes();
            // Collapse/expand is a single user action, so write immediately.
            saveSidebarLayout();
        }
    );
    // splitterMoved fires continuously while the mouse moves. Keep the new
    // width in memory only; scheduleSidebarLayoutSave() delays the disk write.
    // Ignore collapsed moves so the 24px strip is not stored as restore width.
    connect(
        sidebarSplitter,
        &QSplitter::splitterMoved,
        this,
        [this](int, int)
        {
            if (!pinnedSidebar->isCollapsed())
            {
                const int width = sidebarSplitter->sizes().value(0);
                if (width > 0)
                {
                    expandedSidebarWidth = width;
                    scheduleSidebarLayoutSave();
                }
            }
        }
    );

    pinnedSidebar->setCollapsed(collapsed);
    applySidebarSplitterSizes();
    // The splitter has no real width during construction. Re-apply on the
    // next event-loop turn after the window has been laid out.
    QTimer::singleShot(
        0,
        this,
        [this]()
        {
            applySidebarSplitterSizes();
        }
    );
}

void MainWindow::applySidebarSplitterSizes()
{
    // sizes() is a pixel pair for the two splitter panes. The second value
    // is the remaining window width so the tab widget still fills the rest.
    const int total = qMax(sidebarSplitter->width(), 1);
    const int sidebarWidth = pinnedSidebar->isCollapsed()
        ? kSidebarCollapsedWidth
        : expandedSidebarWidth;
    sidebarSplitter->setSizes(
        {sidebarWidth, qMax(total - sidebarWidth, 1)}
    );
}

void MainWindow::scheduleSidebarLayoutSave()
{
    // start() on an already-active single-shot timer resets the interval,
    // so a drag of many splitterMoved events becomes one save.
    sidebarLayoutSaveTimer->start();
}

void MainWindow::saveSidebarLayout()
{
    QSettings settings(
        QStringLiteral("TurboFile"),
        QStringLiteral("TurboFile")
    );
    settings.setValue(
        QStringLiteral("sidebar/collapsed"),
        pinnedSidebar->isCollapsed()
    );
    settings.setValue(
        QStringLiteral("sidebar/width"),
        expandedSidebarWidth
    );
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // The debounce timer may still be pending if the user quit mid-drag.
    if (sidebarLayoutSaveTimer != nullptr)
    {
        sidebarLayoutSaveTimer->stop();
    }
    saveSidebarLayout();
    saveSession();
    QMainWindow::closeEvent(event);
}

// Persist the browser workspace as one or two independent tab groups. Each
// group stores its own visual tab order and current tab, while the outer
// splitter stores the width allocated to each side.
void MainWindow::saveSession() const
{
    QSettings settings(
        QStringLiteral("TurboFile"),
        QStringLiteral("TurboFile")
    );

    // Remove the superseded format where one global tab contained a splitter.
    // Keeping both formats would allow stale state to recreate the old bugs.
    settings.remove(QStringLiteral("session/tabs"));
    settings.remove(QStringLiteral("session/groups"));
    settings.setValue(QStringLiteral("session/version"), 2);
    settings.setValue(
        QStringLiteral("session/groupSplitter"),
        browserPaneSplitter != nullptr
            ? browserPaneSplitter->saveState()
            : QByteArray()
    );
    settings.setValue(
        QStringLiteral("session/activeGroup"),
        qMax(paneTabWidgets.indexOf(activeTabWidget), 0)
    );

    settings.beginWriteArray(QStringLiteral("session/groups"));

    for (int groupIndex = 0; groupIndex < paneTabWidgets.size(); ++groupIndex)
    {
        QTabWidget *tabWidget = paneTabWidgets.at(groupIndex);
        QWidget *placeholder = newTabPlaceholders.value(tabWidget);
        settings.setArrayIndex(groupIndex);

        int currentRealTab = 0;
        int writtenTabCount = 0;
        settings.beginWriteArray(QStringLiteral("tabs"));

        for (int tabIndex = 0; tabIndex < tabWidget->count(); ++tabIndex)
        {
            QWidget *page = tabWidget->widget(tabIndex);

            if (page == nullptr || page == placeholder)
            {
                continue;
            }

            auto stateIterator = tabStates.constFind(page);
            if (
                stateIterator == tabStates.constEnd() ||
                stateIterator->activePane == nullptr
            )
            {
                continue;
            }

            if (page == tabWidget->currentWidget())
            {
                currentRealTab = writtenTabCount;
            }

            BrowserPane *pane = stateIterator->activePane;
            settings.setArrayIndex(writtenTabCount);
            settings.setValue(QStringLiteral("path"), pane->currentPath());
            settings.setValue(QStringLiteral("history"), pane->history());
            settings.setValue(
                QStringLiteral("historyIndex"),
                pane->historyIndex()
            );
            ++writtenTabCount;
        }

        settings.endArray();
        settings.setValue(QStringLiteral("currentTab"), currentRealTab);
    }

    settings.endArray();
}

bool MainWindow::restoreSession()
{
    QSettings settings(
        QStringLiteral("TurboFile"),
        QStringLiteral("TurboFile")
    );

    // Only the independent-group format is safe to restore. A previous
    // per-tab split session falls back to one home tab and is replaced on exit.
    if (settings.value(QStringLiteral("session/version"), 0).toInt() != 2)
    {
        return false;
    }

    const int groupCount =
        settings.beginReadArray(QStringLiteral("session/groups"));

    if (groupCount <= 0)
    {
        settings.endArray();
        return false;
    }

    bool restoredAnyTab = false;

    for (
        int groupIndex = 0;
        groupIndex < groupCount && groupIndex < kMaxPaneGroups;
        ++groupIndex
    )
    {
        settings.setArrayIndex(groupIndex);

        QTabWidget *tabWidget =
            groupIndex == 0
                ? paneTabWidgets.first()
                : createPaneTabWidget();

        if (tabWidget == nullptr)
        {
            continue;
        }

        const int savedCurrentTab =
            settings.value(QStringLiteral("currentTab"), 0).toInt();
        const int tabCount =
            settings.beginReadArray(QStringLiteral("tabs"));
        QList<QWidget *> restoredPages;

        for (int tabIndex = 0; tabIndex < tabCount; ++tabIndex)
        {
            settings.setArrayIndex(tabIndex);
            QString path = settings.value(QStringLiteral("path")).toString();
            QStringList history =
                settings.value(QStringLiteral("history")).toStringList();
            int historyIndex =
                settings.value(QStringLiteral("historyIndex"), -1).toInt();

            if (path.isEmpty())
            {
                path = QDir::homePath();
            }

            QWidget *page = createTabInGroup(tabWidget, path);
            auto stateIterator = tabStates.find(page);

            if (
                page == nullptr ||
                stateIterator == tabStates.end() ||
                stateIterator->activePane == nullptr
            )
            {
                continue;
            }

            stateIterator->activePane->restoreSession(
                path,
                history,
                historyIndex
            );
            restoredPages.append(page);
            restoredAnyTab = true;
        }

        settings.endArray();

        if (restoredPages.isEmpty() && groupIndex > 0)
        {
            // Corrupt settings may describe an empty second group. Remove it
            // immediately so global actions cannot target a plus-only pane.
            paneTabWidgets.removeAll(tabWidget);
            newTabPlaceholders.remove(tabWidget);
            tabWidget->hide();
            tabWidget->deleteLater();
        }
        else if (!restoredPages.isEmpty())
        {
            int currentIndex =
                qBound(0, savedCurrentTab, restoredPages.size() - 1);
            tabWidget->setCurrentWidget(restoredPages.at(currentIndex));
        }
    }

    settings.endArray();

    if (!restoredAnyTab)
    {
        return false;
    }

    const QByteArray splitterState =
        settings.value(QStringLiteral("session/groupSplitter")).toByteArray();
    bool splitterRestored = false;

    if (!splitterState.isEmpty() && browserPaneSplitter != nullptr)
    {
        splitterRestored = browserPaneSplitter->restoreState(splitterState);
    }

    // Missing or incompatible splitter state should still produce two usable,
    // evenly sized groups instead of allowing either side to collapse.
    if (
        !splitterRestored &&
        browserPaneSplitter != nullptr &&
        paneTabWidgets.size() == 2
    )
    {
        const int total = qMax(browserPaneSplitter->width(), 2);
        const int each = total / 2;
        browserPaneSplitter->setSizes({each, total - each});
    }

    int activeGroup =
        settings.value(QStringLiteral("session/activeGroup"), 0).toInt();
    activeTabWidget = paneTabWidgets.value(activeGroup, paneTabWidgets.first());

    // An out-of-range or removed group can never receive actions. Fall back to
    // the left group, which is guaranteed to contain a restored real tab.
    QWidget *activePlaceholder =
        newTabPlaceholders.value(activeTabWidget);
    if (
        activeTabWidget == nullptr ||
        activeTabWidget->currentWidget() == activePlaceholder
    )
    {
        activeTabWidget = paneTabWidgets.first();
    }

    updatePaneChrome();

    return true;
}

// The UI object is allocated manually because it is generated at build time.
MainWindow::~MainWindow() {
    delete ui;
}
