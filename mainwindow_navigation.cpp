#include "mainwindow.h"
#include "browser/browserpane.h"
#include "directorysizesortproxymodel.h"

#include <QDir>
#include <QFileInfo>
#include <QModelIndex>
#include <QWidget>
#include <QTabWidget>

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

// Pin clicks and file actions always target the tab's active pane so a
// split view can browse two directories without fighting over one root.
void MainWindow::navigateTo(
    QWidget *page,
    const QString &path,
    bool addToHistory
)
{
    auto it = tabStates.find(page);
    if (it == tabStates.end() || it->activePane == nullptr)
    {
        return;
    }

    it->activePane->navigateTo(path, addToHistory);
}
