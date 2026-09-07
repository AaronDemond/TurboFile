#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "browser/browserconstants.h"
#include "browser/browserlineedit.h"
#include "browser/browserpane.h"
#include "directorysizesortproxymodel.h"
#include "sidebar/pinnedconstants.h"
#include "sidebar/pinnedsidebar.h"

// Model and filesystem types used to provide directory contents and tab labels.
#include <QDir>
#include <QFileInfo>

// Widgets created dynamically for each file-browser tab.
#include <QLabel>
#include <QLineEdit>
#include <QTreeView>
#include <QPushButton>
#include <QStatusBar>
#include <QWidget>

// Layouts arrange the navigation controls above the file tree.
#include <QVBoxLayout>
#include <QHBoxLayout>
// Splitter holds the pinned sidebar and the tab widget.
#include <QCloseEvent>
#include <QEvent>
#include <QList>
#include <QLayout>
#include <QSplitter>
#include <QSettings>
#include <QSignalBlocker>
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

    // The status bar is the one window-wide bottom row, so these controls do
    // not duplicate when the workspace is split into two tab groups.
    setupViewModeControls();

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

void MainWindow::setupViewModeControls()
{
    // QStatusBar stretches this container across its normal message area. The
    // search and view controls begin at the left edge; one expanding spacer
    // pushes the active directory's regular-file count to the far right.
    bottomControlsWidget = new QWidget(ui->statusbar);
    auto *layout = new QHBoxLayout(bottomControlsWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // The status row should adapt to the window, not establish the window's
    // horizontal minimum from the combined preferred widths of search, three
    // buttons, and the file count. SetNoConstraint permits those controls to
    // compress or clip only at very narrow widths while the explicit browser
    // pane minimums remain the authoritative window constraint.
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    bottomControlsWidget->setMinimumWidth(0);
    bottomControlsWidget->setSizePolicy(
        QSizePolicy::Ignored,
        QSizePolicy::Fixed
    );

    // The search field and every pane path field are BrowserLineEdit objects.
    // Their visual styling therefore comes from one constructor instead of
    // relying on different parent-widget palette inheritance.
    fileSearchLineEdit =
        new BrowserLineEdit(bottomControlsWidget);
    detailsViewButton =
        new QPushButton(tr("Details"), bottomControlsWidget);
    smallIconsViewButton =
        new QPushButton(tr("Small Icons"), bottomControlsWidget);
    bigIconsViewButton =
        new QPushButton(tr("Big Icons"), bottomControlsWidget);
    fileCountLabel =
        new QLabel(tr("0 Files"), bottomControlsWidget);

    // Right alignment keeps changing digit counts anchored against the status
    // bar edge. A zero explicit minimum allows the label to yield space when
    // the overall window approaches the browser pane minimum.
    fileCountLabel->setMinimumWidth(0);
    fileCountLabel->setAlignment(
        Qt::AlignRight |
        Qt::AlignVCenter
    );

    // The lowercase placeholder is centered inside the empty field as
    // requested. QLineEdit applies the same alignment while typing, and its
    // built-in clear action appears only while text exists.
    fileSearchLineEdit->setPlaceholderText(
        tr("search")
    );
    fileSearchLineEdit->setAlignment(Qt::AlignCenter);
    fileSearchLineEdit->setClearButtonEnabled(true);

    // Auto-exclusive checkable buttons provide a compact mode selector using
    // the platform's normal QPushButton styling rather than custom colors.
    for (
        QPushButton *button :
        {detailsViewButton, smallIconsViewButton, bigIconsViewButton}
    )
    {
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setMinimumWidth(0);
    }

    layout->addWidget(fileSearchLineEdit);
    layout->addSpacing(6);
    layout->addWidget(detailsViewButton);
    layout->addWidget(smallIconsViewButton);
    layout->addWidget(bigIconsViewButton);

    // This is the only stretch in the row. It preserves the requested left
    // alignment while reserving the opposite corner for a stable count label.
    layout->addStretch();
    layout->addWidget(fileCountLabel);

    ui->statusbar->addWidget(bottomControlsWidget, 1);

    // Geometry is not final until QMainWindow lays out its central widget and
    // status bar. The sidebar event filter keeps this alignment current after
    // the first pass and after every splitter resize or collapse transition.
    alignBottomControlsToSidebar();

    // textChanged fires for each insertion and deletion, producing the live
    // filtering behavior. activeBrowserPane() keeps the query scoped to the
    // tab and split side the user most recently interacted with.
    connect(
        fileSearchLineEdit,
        &QLineEdit::textChanged,
        this,
        [this](const QString &text)
        {
            if (BrowserPane *pane = activeBrowserPane())
            {
                pane->setSearchText(text);
            }
        }
    );

    connect(
        detailsViewButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            if (BrowserPane *pane = activeBrowserPane())
            {
                pane->setFileViewMode(
                    BrowserPane::FileViewMode::Details
                );
            }
        }
    );

    connect(
        smallIconsViewButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            if (BrowserPane *pane = activeBrowserPane())
            {
                pane->setFileViewMode(
                    BrowserPane::FileViewMode::SmallIcons
                );
            }
        }
    );

    connect(
        bigIconsViewButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            if (BrowserPane *pane = activeBrowserPane())
            {
                pane->setFileViewMode(
                    BrowserPane::FileViewMode::BigIcons
                );
            }
        }
    );

    updateViewModeControls();
}

BrowserPane *MainWindow::activeBrowserPane() const
{
    if (activeTabWidget == nullptr)
    {
        return nullptr;
    }

    QWidget *page = activeTabWidget->currentWidget();
    auto stateIterator = tabStates.constFind(page);

    if (stateIterator == tabStates.constEnd())
    {
        return nullptr;
    }

    return stateIterator->activePane;
}

void MainWindow::updateViewModeControls()
{
    BrowserPane *pane = activeBrowserPane();
    const bool hasActivePane = pane != nullptr;

    detailsViewButton->setEnabled(hasActivePane);
    smallIconsViewButton->setEnabled(hasActivePane);
    bigIconsViewButton->setEnabled(hasActivePane);
    fileSearchLineEdit->setEnabled(hasActivePane);

    // The count describes every direct regular file in the active directory,
    // not only rows matching the current search. Directories and symbolic
    // links are excluded by BrowserPane's proxy-safe metadata check.
    const int fileCount =
        hasActivePane ? pane->regularFileCount() : 0;
    fileCountLabel->setText(
        QStringLiteral("%1 %2").arg(
            fileCount
        ).arg(
            fileCount == 1 ? tr("File") : tr("Files")
        )
    );

    // Changing tabs or split sides must display that pane's own search text.
    // Blocking the signal prevents setText() from applying the old pane's
    // query to the newly active pane during this synchronization step.
    const QSignalBlocker searchSignalBlocker(fileSearchLineEdit);
    fileSearchLineEdit->setText(
        hasActivePane
            ? pane->searchText()
            : QString()
    );

    if (!hasActivePane)
    {
        return;
    }

    BrowserPane::FileViewMode mode = pane->fileViewMode();
    detailsViewButton->setChecked(
        mode == BrowserPane::FileViewMode::Details
    );
    smallIconsViewButton->setChecked(
        mode == BrowserPane::FileViewMode::SmallIcons
    );
    bigIconsViewButton->setChecked(
        mode == BrowserPane::FileViewMode::BigIcons
    );
}

void MainWindow::setupSidebar()
{
    pinnedSidebar = new PinnedSidebar(ui->centralwidget);

    // QWidget has no resized signal. Observing the sidebar itself provides
    // exact geometry updates whether its width changes through dragging,
    // collapsing, session restoration, or a containing-window resize.
    pinnedSidebar->installEventFilter(this);

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
    const int requestedSidebarWidth = pinnedSidebar->isCollapsed()
        ? kSidebarCollapsedWidth
        : expandedSidebarWidth;

    // Reserve the explicit minimum of every visible browser group before
    // allocating space to the flexible sidebar. A large restored/sidebar drag
    // width can therefore never force QMainWindow to grow to satisfy both.
    const int paneCount = qMax(paneTabWidgets.size(), 1);
    const int browserMinimumWidth =
        (paneCount * kPaneGroupMinimumWidth) +
        ((paneCount - 1) * kPaneSplitterHandleWidth);
    const int maximumSidebarWidth =
        qMax(
            total -
                browserMinimumWidth -
                kSidebarSplitterHandleWidth,
            0
        );
    const int sidebarWidth =
        qMin(requestedSidebarWidth, maximumSidebarWidth);

    sidebarSplitter->setSizes(
        {sidebarWidth, qMax(total - sidebarWidth, 1)}
    );

    alignBottomControlsToSidebar();
}

void MainWindow::alignBottomControlsToSidebar()
{
    if (
        pinnedSidebar == nullptr ||
        bottomControlsWidget == nullptr ||
        fileSearchLineEdit == nullptr
    )
    {
        return;
    }

    auto *layout =
        qobject_cast<QHBoxLayout *>(bottomControlsWidget->layout());

    if (layout == nullptr)
    {
        return;
    }

    // mapTo() compares real widget coordinates across the central widget and
    // status bar hierarchies. The resulting left offset and frame width align
    // both search-field edges with the pinned column instead of approximating
    // them from saved splitter sizes or unrelated layout margins.
    const QPoint sidebarLeft =
        pinnedSidebar->mapTo(
            bottomControlsWidget,
            QPoint(0, 0)
        );
    const int leftMargin =
        qMax(sidebarLeft.x(), 0);
    const int sidebarWidth =
        qMax(pinnedSidebar->width(), 1);

    layout->setContentsMargins(
        leftMargin,
        0,
        0,
        0
    );
    fileSearchLineEdit->setPreferredWidth(sidebarWidth);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (
        watched == pinnedSidebar &&
        (
            event->type() == QEvent::Move ||
            event->type() == QEvent::Resize ||
            event->type() == QEvent::Show
        )
    )
    {
        alignBottomControlsToSidebar();
    }

    return QMainWindow::eventFilter(watched, event);
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
    // Store a scale-independent ratio rather than QSplitter::saveState(). The
    // opaque state includes absolute pixel sizes, which can reopen a compact
    // window at a much larger width than its current screen/layout permits.
    settings.remove(QStringLiteral("session/groupSplitter"));

    qreal leftPaneRatio = 0.5;

    if (browserPaneSplitter != nullptr)
    {
        const QList<int> paneSizes =
            browserPaneSplitter->sizes();

        if (paneSizes.size() == 2)
        {
            const int paneWidthTotal =
                paneSizes.at(0) + paneSizes.at(1);

            if (paneWidthTotal > 0)
            {
                leftPaneRatio =
                    static_cast<qreal>(paneSizes.at(0)) /
                    paneWidthTotal;
            }
        }
    }

    settings.setValue(
        QStringLiteral("session/groupSplitRatio"),
        leftPaneRatio
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
            // View mode belongs to the tab's BrowserPane. Saving the enum as
            // an integer keeps QSettings data simple and allows old sessions,
            // which have no value, to default to Details during restoration.
            settings.setValue(
                QStringLiteral("viewMode"),
                static_cast<int>(pane->fileViewMode())
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
            int viewModeValue =
                settings.value(
                    QStringLiteral("viewMode"),
                    static_cast<int>(
                        BrowserPane::FileViewMode::Details
                    )
                ).toInt();

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

            // Clamp persisted data before converting it back to the enum so
            // corrupt or newer settings cannot select an unknown stack page.
            viewModeValue = qBound(
                static_cast<int>(BrowserPane::FileViewMode::Details),
                viewModeValue,
                static_cast<int>(BrowserPane::FileViewMode::BigIcons)
            );
            stateIterator->activePane->setFileViewMode(
                static_cast<BrowserPane::FileViewMode>(viewModeValue)
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

    // Restore only the relative split. Apply it after the first layout pass so
    // the calculation uses this window's actual available width rather than
    // stale absolute pixels from a previous session.
    const qreal savedSplitRatio =
        qBound(
            0.0,
            settings.value(
                QStringLiteral("session/groupSplitRatio"),
                0.5
            ).toDouble(),
            1.0
        );

    if (
        browserPaneSplitter != nullptr &&
        paneTabWidgets.size() == 2
    )
    {
        QTimer::singleShot(
            0,
            this,
            [this, savedSplitRatio]()
            {
                const int availableWidth =
                    qMax(
                        browserPaneSplitter->width() -
                            kPaneSplitterHandleWidth,
                        kPaneGroupMinimumWidth * 2
                    );
                const int maximumLeftWidth =
                    availableWidth - kPaneGroupMinimumWidth;
                const int leftWidth =
                    qBound(
                        kPaneGroupMinimumWidth,
                        qRound(availableWidth * savedSplitRatio),
                        maximumLeftWidth
                    );

                browserPaneSplitter->setSizes(
                    {leftWidth, availableWidth - leftWidth}
                );
            }
        );
    }

    updatePaneChrome();

    // Opening a restored two-pane workspace always starts interaction on the
    // left, regardless of which group happened to be active at shutdown. Run
    // the focus transfer on the next event-loop turn because the constructor's
    // widgets have not completed their first layout and focus pass yet.
    focusLeftPane();
    QTimer::singleShot(
        0,
        this,
        [this]()
        {
            focusLeftPane();
        }
    );

    return true;
}

// The UI object is allocated manually because it is generated at build time.
MainWindow::~MainWindow() {
    delete ui;
}
