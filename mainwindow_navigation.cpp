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


void MainWindow::navigateTo(
    QWidget *page,
    const QString &path,
    bool addToHistory
)
{
    // Get the state belonging to this particular tab.
    TabState &state = tabStates[page];

    // Ask the filesystem model for the QModelIndex
    // representing this path.
    QModelIndex newIndex =
        fileModel->index(path);

    // Make sure the path actually produced a valid model index.
    if (!newIndex.isValid())
    {
        return;
    }

    // -------------------------------------------------
    // UPDATE HISTORY
    // -------------------------------------------------

    if (addToHistory)
    {
        // If we went Back earlier and then navigate somewhere
        // new, the old Forward history must be removed.
        //
        // Example:
        //
        // A -> B -> C
        //
        // Back:
        //
        // A -> B -> C
        //      ^
        //
        // Then navigate to D:
        //
        // A -> B -> D
        //
        // C should disappear.
        while (state.history.size() > state.historyIndex + 1)
        {
            state.history.removeLast();
        }

        // Avoid adding the exact same path twice in a row.
        if (
            state.history.isEmpty() ||
            state.history.last() != path
        )
        {
            state.history.append(path);

            state.historyIndex =
                state.history.size() - 1;
        }
    }

    // -------------------------------------------------
    // ACTUALLY DISPLAY THE DIRECTORY
    // -------------------------------------------------

    state.fileTreeView->setRootIndex(newIndex);

    state.pathLineEdit->setText(path);

    // Update the visible tab title.
    updateTabTitle(page, path);

    // Enable/disable Back and Forward appropriately.
    updateNavigationButtons(page);
}


void MainWindow::updateNavigationButtons(QWidget *page)
{
    // Get this tab's state.
    TabState &state = tabStates[page];

    // Back is possible whenever we're beyond
    // the first history entry.
    bool canGoBack =
        state.historyIndex > 0;

    // Forward is possible whenever something exists
    // after our current history entry.
    bool canGoForward =
        state.historyIndex >= 0 &&
        state.historyIndex < state.history.size() - 1;

    state.backButton->setEnabled(canGoBack);
    state.forwardButton->setEnabled(canGoForward);
}