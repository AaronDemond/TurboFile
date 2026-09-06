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


// Create the controls and behavior for a single directory-navigation tab.
void MainWindow::createTab(const QString &path)
{
    // The page owns its layouts and child widgets through Qt parent ownership.
    auto *page = new QWidget();
    auto *mainLayout = new QVBoxLayout(page);
    auto *topLayout = new QHBoxLayout();

    // The top row exposes parent-directory navigation and the current path.
    auto *backButton = new QPushButton("<", page);
    auto *forwardButton = new QPushButton(">", page);
    auto *upButton = new QPushButton("Up", page);
    auto *pathLineEdit = new QLineEdit(page);
    auto *fileTreeView = new QTreeView(page);

    // Place the controls above the tree that displays directory entries.
    topLayout->addWidget(backButton);
    topLayout->addWidget(forwardButton);
    topLayout->addWidget(upButton);
    topLayout->addWidget(pathLineEdit);
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(fileTreeView);

    // Reuse the window-wide filesystem model for this tab's tree view.
    fileTreeView->setModel(fileModel);



    // Store the tab's state for navigation purposes.
    TabState state;
    state.fileTreeView = fileTreeView;
    state.pathLineEdit = pathLineEdit;
    state.backButton = backButton;
    state.forwardButton = forwardButton;
    tabStates[page] = state;



    // Add the finished page, label it from its directory, and select it.
    int tabIndex = ui->tabWidget->addTab(page, "");
    navigateTo(page, path);
    ui->tabWidget->setCurrentIndex(tabIndex);
    updateTabTitle(page, path);


    connect(
        fileTreeView,
        &QTreeView::doubleClicked,
        page,
        [this, page](const QModelIndex &index)
        {
            // Only navigate into directories.
            if (fileModel->isDir(index))
            {
                // Turn the QModelIndex into a real path.
                QString newPath =
                    fileModel->filePath(index);

                // navigateTo() now handles:
                //
                // - changing the tree
                // - changing the path field
                // - updating tab title
                // - adding history
                // - updating navigation buttons
                navigateTo(page, newPath);
            }
        }
    );
    // The Up button moves the view to its current root's parent directory.
    connect(
        upButton,
        &QPushButton::clicked,
        page,
        [this, page]()
        {
            TabState &state =
                tabStates[page];

            QModelIndex currentIndex =
                state.fileTreeView->rootIndex();

            QModelIndex parentIndex =
                fileModel->parent(currentIndex);

            if (parentIndex.isValid())
            {
                QString parentPath =
                    fileModel->filePath(parentIndex);

                navigateTo(page, parentPath);
            }
        }
    );

    connect(
        backButton,
        &QPushButton::clicked,
        page,
        [this, page]()
        {
            TabState &state =
                tabStates[page];

            // Make sure there is an older history entry.
            if (state.historyIndex > 0)
            {
                // Move backward through history.
                state.historyIndex--;

                // Get the path at our new history position.
                QString previousPath =
                    state.history.at(state.historyIndex);

                // Navigate without adding another history entry.
                navigateTo(
                    page,
                    previousPath,
                    false
                );
            }
        }
    );

    connect(
        forwardButton,
        &QPushButton::clicked,
        page,
        [this, page]()
        {
            TabState &state =
                tabStates[page];

            // Make sure something exists ahead of us.
            if (
                state.historyIndex <
                state.history.size() - 1
            )
            {
                // Move forward through history.
                state.historyIndex++;

                // Get the path at the new history position.
                QString nextPath =
                    state.history.at(state.historyIndex);

                // Navigate without creating another history entry.
                navigateTo(
                    page,
                    nextPath,
                    false
                );
            }
        }
    );
}


// Derive a concise label from the directory name, retaining root paths as-is.
void MainWindow::updateTabTitle(QWidget *page, const QString &path) {
    // Locate the tab that contains this dynamically-created page.
    int tabIndex = ui->tabWidget->indexOf(page);
    QString tabName = QFileInfo(path).fileName();

    // QFileInfo returns an empty name for paths such as "/".
    if (tabName.isEmpty()) {
        tabName = path;
    }

    // Apply the label to the matching tab.
    ui->tabWidget->setTabText(tabIndex, tabName);
}