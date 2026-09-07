#include "mainwindow.h"
#include "directorysizesortproxymodel.h"

#include <QDir>
#include <QFileInfo>

#include <QLineEdit>
#include <QTreeView>
#include <QPushButton>
#include <QWidget>

#include <QVBoxLayout>
#include <QHBoxLayout>

#include <QTabWidget>

#include <QShortcut>
#include <QKeySequence>

// Navigate to the path manually entered in the path bar.
void MainWindow::navigateFromPathBar(QWidget *page){

    // get the current state of this particular tab
    TabState &state = tabStates[page];

    // read the edit in the path bar
    QString enteredPath = state.pathLineEdit->text().trimmed();


    //generate currentPath
    QModelIndex currentIndex = state.fileTreeView->rootIndex();
    QString currentPath = fileModel->filePath(currentIndex);

    // handle empty path
    if (enteredPath.isEmpty()) {
        state.pathLineEdit->setText(currentPath);
        return;
    }

    // handle '~' for home directory
    if (enteredPath == "~") {
        enteredPath = QDir::homePath();
    } else if (enteredPath.startsWith("~/")) {
        enteredPath = QDir::homePath() + enteredPath.mid(1);
    }

    // handle realative pathnames
    if (QDir::isRelativePath(enteredPath)) {
        QDir currentDirectory(currentPath);
        enteredPath = currentDirectory.absoluteFilePath(enteredPath);
    }

    // validate path exists
    QFileInfo pathInfo(enteredPath);
    if (!pathInfo.exists() || !pathInfo.isDir()) {
        state.pathLineEdit->setText(currentPath);
        return;
    }

    // finally navigate to path
    navigateTo(page, pathInfo.absoluteFilePath());
}


// Open a directory from a clicked tree item, but ignore non-directory entries.
void MainWindow::openDirectory(QWidget *page, const QModelIndex &index)
{
    if (!fileModel->isDir(index))
    {
        return;
    }

    QString newPath = fileModel->filePath(index);
    navigateTo(page, newPath);
}

// Move the current tab upward one directory level.
void MainWindow::goUp(QWidget *page)
{
    TabState &state = tabStates[page];
    QModelIndex currentIndex = state.fileTreeView->rootIndex();
    QModelIndex parentIndex = fileModel->parent(currentIndex);

    if (!parentIndex.isValid())
    {
        return;
    }

    QString parentPath = fileModel->filePath(parentIndex);
    navigateTo(page, parentPath);
}

// Move backward through the current tab's browsing history.
void MainWindow::goBack(QWidget *page)
{
    TabState &state = tabStates[page];

    if (state.historyIndex <= 0)
    {
        return;
    }

    state.historyIndex--;

    QString previousPath = state.history.at(state.historyIndex);
    navigateTo(page, previousPath, false);
}

// Move forward through the current tab's browsing history.
void MainWindow::goForward(QWidget *page)
{
    TabState &state = tabStates[page];

    if (state.historyIndex >= state.history.size() - 1)
    {
        return;
    }

    state.historyIndex++;

    QString nextPath = state.history.at(state.historyIndex);
    navigateTo(page, nextPath, false);
}

// Change the currently viewed directory for a tab and optionally record it in history.
void MainWindow::navigateTo(
    QWidget *page,
    const QString &path,
    bool addToHistory
)
{
    TabState &state = tabStates[page];
    QModelIndex newIndex = fileModel->index(path);

    if (!newIndex.isValid())
    {
        return;
    }

    // Only modify the history when this is a new navigation action. Back and
    // Forward pass false because they move an existing history index instead.
    if (addToHistory)
    {
        // Remove every entry after the current position. These entries are the
        // old Forward history and are no longer reachable after branching to a
        // different directory.
        //
        // For example, after A -> B -> C followed by Back, the current index
        // points to B. Navigating from B to D must remove C before adding D.
        while (state.history.size() > state.historyIndex + 1)
        {
            state.history.removeLast();
        }

        // Add the destination when history is empty or when it differs from
        // the most recent entry. This prevents consecutive duplicate paths.
        if (
            state.history.isEmpty() ||
            state.history.last() != path
        )
        {
            // Append the new destination, then move the current-history marker
            // to its position at the end of the list.
            state.history.append(path);
            state.historyIndex = state.history.size() - 1;
        }
    }

    state.fileTreeView->setRootIndex(newIndex);
    state.pathLineEdit->setText(path);
    updateTabTitle(page, path);
    updateNavigationButtons(page);
}

// Enable or disable the navigation buttons based on the current history position.
void MainWindow::updateNavigationButtons(QWidget *page)
{
    TabState &state = tabStates[page];

    bool canGoBack = state.historyIndex > 0;
    bool canGoForward =
        state.historyIndex >= 0 &&
        state.historyIndex < state.history.size() - 1;

    state.backButton->setEnabled(canGoBack);
    state.forwardButton->setEnabled(canGoForward);
}