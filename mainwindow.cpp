#include "mainwindow.h"
#include "ui_mainwindow.h"
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
#include <QCloseEvent>
#include <QSplitter>
#include <QSettings>
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

    // Every window starts with one tab rooted at the home directory.
    createTab(QDir::homePath());

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

    // Clicking a pin navigates the current browser tab only.
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
    QMainWindow::closeEvent(event);
}

// The UI object is allocated manually because it is generated at build time.
MainWindow::~MainWindow() {
    delete ui;
}
