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

    // Allow users to reorder tabs and close all but the last remaining tab.
    ui->tabWidget->setTabsClosable(true);
    ui->tabWidget->setMovable(true);

    // Create the permanent trailing plus tab before adding browser tabs.
    setupNewTabButton();

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

    // Delete a requested tab, while keeping one tab available at all times.
    connect(ui->tabWidget, &QTabWidget::tabCloseRequested, this, [this](int index) {
        // The count includes the plus placeholder, so two tabs means there
        // is only one real browser tab left and it must remain open.
        if (ui->tabWidget->count() <= 2) {
            return;
        }

        // Remove the page from the widget first, then safely destroy it.
        QWidget *page = ui->tabWidget->widget(index);

        // The plus placeholder is a control, not a closable browser tab.
        if (page == newTabPlaceholder) {
            return;
        }

        tabStates.remove(page);
        ui->tabWidget->removeTab(index);
        page->deleteLater();
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
    sidebarSplitter->addWidget(pinnedSidebar);
    sidebarSplitter->addWidget(ui->tabWidget);
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

    auto *rootLayout =
        qobject_cast<QHBoxLayout *>(ui->centralwidget->layout());
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

    // Clicking a pin navigates the current tab's active pane only.
    connect(
        pinnedSidebar,
        &PinnedSidebar::directoryActivated,
        this,
        [this](const QString &path)
        {
            QWidget *page = ui->tabWidget->currentWidget();
            if (page != nullptr && page != newTabPlaceholder)
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

// Walk tabs in visual order so restore recreates the same left-to-right
// sequence. The plus placeholder is a control, not a saved browser tab.
void MainWindow::saveSession() const
{
    QSettings settings(
        QStringLiteral("TurboFile"),
        QStringLiteral("TurboFile")
    );

    settings.remove(QStringLiteral("session/tabs"));

    int written = 0;
    settings.beginWriteArray(QStringLiteral("session/tabs"));

    const int tabCount = ui->tabWidget->count();
    int savedCurrent = 0;

    for (int i = 0; i < tabCount; ++i)
    {
        QWidget *page = ui->tabWidget->widget(i);
        if (page == nullptr || page == newTabPlaceholder)
        {
            continue;
        }

        auto it = tabStates.constFind(page);
        if (it == tabStates.constEnd() || it->panes.isEmpty())
        {
            continue;
        }

        if (page == ui->tabWidget->currentWidget())
        {
            savedCurrent = written;
        }

        settings.setArrayIndex(written);
        settings.setValue(
            QStringLiteral("splitter"),
            it->paneSplitter != nullptr
                ? it->paneSplitter->saveState()
                : QByteArray()
        );

        settings.beginWriteArray(QStringLiteral("panes"));
        for (int p = 0; p < it->panes.size(); ++p)
        {
            BrowserPane *pane = it->panes.at(p);
            settings.setArrayIndex(p);
            settings.setValue(QStringLiteral("path"), pane->currentPath());
            settings.setValue(QStringLiteral("history"), pane->history());
            settings.setValue(QStringLiteral("historyIndex"), pane->historyIndex());
        }
        settings.endArray();

        ++written;
    }

    settings.endArray();
    settings.setValue(QStringLiteral("session/currentTab"), savedCurrent);
}

bool MainWindow::restoreSession()
{
    QSettings settings(
        QStringLiteral("TurboFile"),
        QStringLiteral("TurboFile")
    );

    const int tabCount =
        settings.beginReadArray(QStringLiteral("session/tabs"));

    if (tabCount <= 0)
    {
        settings.endArray();
        return false;
    }

    QList<QWidget *> restoredPages;

    for (int i = 0; i < tabCount; ++i)
    {
        settings.setArrayIndex(i);

        const QByteArray splitterState =
            settings.value(QStringLiteral("splitter")).toByteArray();

        const int paneCount =
            settings.beginReadArray(QStringLiteral("panes"));

        struct SavedPane
        {
            QString path;
            QStringList history;
            int historyIndex = -1;
        };
        QList<SavedPane> savedPanes;

        for (int p = 0; p < paneCount && p < kMaxPanesPerTab; ++p)
        {
            settings.setArrayIndex(p);
            SavedPane saved;
            saved.path = settings.value(QStringLiteral("path")).toString();
            saved.history =
                settings.value(QStringLiteral("history")).toStringList();
            saved.historyIndex =
                settings.value(QStringLiteral("historyIndex"), -1).toInt();
            if (saved.path.isEmpty())
            {
                saved.path = QDir::homePath();
            }
            savedPanes.append(saved);
        }
        settings.endArray();

        if (savedPanes.isEmpty())
        {
            continue;
        }

        createTab(savedPanes.first().path);
        QWidget *page = ui->tabWidget->currentWidget();
        if (page == nullptr || page == newTabPlaceholder)
        {
            continue;
        }

        TabState &state = tabStates[page];
        state.panes.first()->restoreSession(
            savedPanes.first().path,
            savedPanes.first().history,
            savedPanes.first().historyIndex
        );

        if (savedPanes.size() > 1)
        {
            splitPane(page);
            state.panes.last()->restoreSession(
                savedPanes.at(1).path,
                savedPanes.at(1).history,
                savedPanes.at(1).historyIndex
            );
        }

        if (!splitterState.isEmpty() && state.paneSplitter != nullptr)
        {
            state.paneSplitter->restoreState(splitterState);
        }

        restoredPages.append(page);
    }

    settings.endArray();

    if (restoredPages.isEmpty())
    {
        return false;
    }

    const int current =
        settings.value(QStringLiteral("session/currentTab"), 0).toInt();
    if (current >= 0 && current < restoredPages.size())
    {
        ui->tabWidget->setCurrentWidget(restoredPages.at(current));
    }

    return true;
}

// The UI object is allocated manually because it is generated at build time.
MainWindow::~MainWindow() {
    delete ui;
}
