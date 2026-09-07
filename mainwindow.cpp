#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "directorysizesortproxymodel.h"
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

// The UI contains this widget, which manages the browser tabs.
#include <QTabWidget>

// Keyboard shortcut support for creating tabs.
#include <QShortcut>
#include <QKeySequence>

// Initialize the generated UI and a filesystem model owned by this window.
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , fileModel(new DirectorySizeSortProxyModel(this))
{
    // Create the widgets declared in mainwindow.ui.
    ui->setupUi(this);

    pinnedSidebar = new PinnedSidebar(ui->centralwidget);
    auto *rootLayout =
        qobject_cast<QHBoxLayout *>(ui->centralwidget->layout());
    rootLayout->insertWidget(0, pinnedSidebar);
    rootLayout->setStretch(0, 0);
    rootLayout->setStretch(1, 1);

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

    // Load the model from the user's home directory upward.
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




// The UI object is allocated manually because it is generated at build time.
MainWindow::~MainWindow() {
    delete ui;
}
