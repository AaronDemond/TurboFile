#include "mainwindow.h"
#include "ui_mainwindow.h"

// Model and filesystem types used to provide directory contents and tab labels.
#include <QFileSystemModel>
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
    , fileModel(new QFileSystemModel(this))
{
    // Create the widgets declared in mainwindow.ui.
    ui->setupUi(this);

    // Load the model from the user's home directory upward.
    fileModel->setRootPath(QDir::homePath());

    // Allow users to reorder tabs and close all but the last remaining tab.
    ui->tabWidget->setTabsClosable(true);
    ui->tabWidget->setMovable(true);

    // Every window starts with one tab rooted at the home directory.
    createTab(QDir::homePath());

    // Ctrl+T creates another home-directory browser tab.
    auto *newTabShortcut = new QShortcut(QKeySequence("Ctrl+T"), this);

    connect(newTabShortcut, &QShortcut::activated, this, [this]() {
        createTab(QDir::homePath());
    });

    // Delete a requested tab, while keeping one tab available at all times.
    connect(ui->tabWidget, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (ui->tabWidget->count() == 1) {
            return;
        }

        // Remove the page from the widget first, then safely destroy it.
        QWidget *page = ui->tabWidget->widget(index);
        tabStates.remove(page);
        ui->tabWidget->removeTab(index);
        page->deleteLater();
    });
}




// The UI object is allocated manually because it is generated at build time.
MainWindow::~MainWindow() {
    delete ui;
}
