#include "mainwindow.h"
#include "directorysizesortproxymodel.h"

#include <QTreeView>
#include <QItemSelectionModel>

#include <QMenu>
#include <QAction>

#include <QFile>
#include <QFileInfo>
#include <QDir>

#include <QDesktopServices>
#include <QUrl>

#include <QApplication>
#include <QClipboard>
#include <QMimeData>

#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>


// -------------------------------------------------
// CONTEXT MENU
// -------------------------------------------------

void MainWindow::showFileContextMenu(
    QWidget *page,
    const QPoint &position
)
{
    TabState &state =
        tabStates[page];

    QTreeView *fileTreeView =
        state.fileTreeView;

    QModelIndex clickedIndex =
        fileTreeView->indexAt(position);

    QMenu menu(fileTreeView);

    if (clickedIndex.isValid())
    {
        QItemSelectionModel *selectionModel =
            fileTreeView->selectionModel();

        QModelIndex rowIndex =
            clickedIndex.siblingAtColumn(0);

        bool alreadySelected =
            selectionModel->isRowSelected(
                rowIndex.row(),
                rowIndex.parent()
            );

        // Preserve an existing multi-selection when it contains the
        // right-clicked row. Otherwise, select only the clicked row.
        if (!alreadySelected)
        {
            selectionModel->select(
                rowIndex,
                QItemSelectionModel::ClearAndSelect |
                    QItemSelectionModel::Rows
            );

            fileTreeView->setCurrentIndex(rowIndex);
        }

        QStringList selectedPaths =
            selectedFilePaths(fileTreeView);

        QAction *openAction =
            menu.addAction("Open");

        QAction *copyAction =
            menu.addAction("Copy");

        QAction *renameAction =
            menu.addAction("Rename");

        renameAction->setEnabled(
            selectedPaths.size() == 1
        );

        menu.addSeparator();

        QAction *deleteAction =
            menu.addAction("Delete");

        menu.addSeparator();

        QAction *pasteAction =
            menu.addAction("Paste");

        // Only enable Paste if the clipboard
        // currently contains filesystem URLs.
        pasteAction->setEnabled(
            QApplication::clipboard()
                ->mimeData()
                ->hasUrls()
        );

        QAction *selectedAction =
            menu.exec(
                fileTreeView
                    ->viewport()
                    ->mapToGlobal(position)
            );

        if (selectedAction == openAction)
        {
            openSelectedItems(page);
        }
        else if (selectedAction == copyAction)
        {
            copySelectedItemsToClipboard(page);
        }
        else if (selectedAction == renameAction)
        {
            renameSelectedItem(page);
        }
        else if (selectedAction == deleteAction)
        {
            deleteSelectedItems(page);
        }
        else if (selectedAction == pasteAction)
        {
            pasteClipboardItems(page);
        }

        return;
    }

    // -------------------------------------------------
    // EMPTY SPACE
    // -------------------------------------------------

    // If the user right-clicked empty space, there
    // isn't a file to Open/Copy/Rename/Delete.
    //
    // Paste still makes sense, though.
    QAction *pasteAction =
        menu.addAction("Paste");

    pasteAction->setEnabled(
        QApplication::clipboard()
            ->mimeData()
            ->hasUrls()
    );

    QAction *selectedAction =
        menu.exec(
            fileTreeView
                ->viewport()
                ->mapToGlobal(position)
        );

    if (selectedAction == pasteAction)
    {
        pasteClipboardItems(page);
    }
}

// -------------------------------------------------
// OPEN
// -------------------------------------------------

void MainWindow::openItem(
    QWidget *page,
    const QModelIndex &index
)
{
    // Turn the QModelIndex into a real filesystem path.
    QString path =
        fileModel->filePath(index);

    // Directories open inside TurboFile.
    if (fileModel->isDir(index))
    {
        navigateTo(page, path);
        return;
    }

    // Regular files are handed to the desktop environment.
    //
    // For example:
    //
    // .png → image viewer
    // .pdf → PDF viewer
    // .txt → configured text editor
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(path)
    );
}

void MainWindow::openSelectedItems(
    QWidget *page
)
{
    TabState &state =
        tabStates[page];

    QStringList paths =
        selectedFilePaths(state.fileTreeView);

    if (paths.isEmpty())
    {
        return;
    }

    if (paths.size() == 1)
    {
        const QString &path =
            paths.first();

        QFileInfo info(path);

        if (info.isDir())
        {
            navigateTo(page, path);
        }
        else
        {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(path)
            );
        }

        return;
    }

    for (const QString &path : paths)
    {
        QFileInfo info(path);

        if (!info.exists())
        {
            continue;
        }

        if (info.isDir())
        {
            createTab(path);
        }
        else
        {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(path)
            );
        }
    }
}

// -------------------------------------------------
// COPY
// -------------------------------------------------

void MainWindow::copySelectedItemsToClipboard(
    QWidget *page
)
{
    TabState &state =
        tabStates[page];

    QStringList paths =
        selectedFilePaths(state.fileTreeView);

    if (paths.isEmpty())
    {
        return;
    }

    auto *mimeData =
        new QMimeData;

    QList<QUrl> urls;

    for (const QString &path : paths)
    {
        urls.append(
            QUrl::fromLocalFile(path)
        );
    }

    mimeData->setUrls(urls);

    mimeData->setText(
        paths.join('\n')
    );

    QByteArray gnomeClipboardData =
        "copy";

    for (const QUrl &url : urls)
    {
        gnomeClipboardData += "\n";
        gnomeClipboardData += url.toEncoded();
    }

    mimeData->setData(
        "x-special/gnome-copied-files",
        gnomeClipboardData
    );

    mimeData->setData(
        "application/x-kde-cutselection",
        QByteArray("0")
    );

    QApplication::clipboard()
        ->setMimeData(mimeData);
}

void MainWindow::pasteClipboardItems(
    QWidget *page
) {
    // get the tab's current state
    TabState &state = tabStates[page];

    // get current clipboard contents
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();

    // only know filesystem urls
    if (!mimeData->hasUrls()) {
        return;
    }

    // Determine the direcory currently being displayed
    QModelIndex currentIndex = state.fileTreeView->rootIndex();
    QString destinationDirectory = fileModel->filePath(currentIndex);

    // The clipboard may contain one or many URLS
    const QList<QUrl> urls = mimeData->urls();

    for (const QUrl &url : urls) {
        
        // Ignore non-local urls
        if (!url.isLocalFile()){
            continue;
        }

        QString sourcePath = url.toLocalFile();
        QFileInfo sourceInfo(sourcePath);

        // make sure source exists
        if (!sourceInfo.exists()){
            continue;
        }

        // if already exists
        QString destinationPath = 
            makeUniqueCopyPath(sourcePath, destinationDirectory);

        // protect against copying a dir into itself
        if (sourceInfo.isDir()){
            QString sourceAbsolute = QDir::cleanPath(sourceInfo.absoluteFilePath());
            QString destinationAbsolute = QDir::cleanPath(QFileInfo(destinationPath).absoluteFilePath());

            if (destinationAbsolute == sourceAbsolute || destinationAbsolute.startsWith(sourceAbsolute + "/")){
                QMessageBox::warning(this, "Paste Failed", "A directory cannot be copied inside itself");
                continue;
            }

        }
        
        // Perform the copy and invalidate directory totals only when the
        // destination was created successfully.
        if (!copyRecursively(sourcePath, destinationPath)){
            QMessageBox::warning(
                this,
                "Paste Failed",
                QString("Could not copy:\n%1").arg((sourcePath)));   
        } else {
            fileModel->invalidatePaths({destinationPath});
        }
    }


}

bool MainWindow::copyRecursively(const QString &sourcePath, const QString &destinationPath) {
    QFileInfo sourceInfo(sourcePath);

    // source dissapeared
    if (!sourceInfo.exists()){
        return false;
    }

    // reg file
    if (sourceInfo.isFile()){
        return QFile::copy(
            sourcePath,
            destinationPath
        );
    }

    // directory
    if (sourceInfo.isDir()){

        // creat destination dir
        if(!QDir().mkpath(destinationPath)) {
            return false;
        }

        QDir sourceDirectory(sourcePath);

        // retreive all except . and ..
        QFileInfoList entries =  sourceDirectory.entryInfoList(
            QDir::NoDotAndDotDot |
            QDir::AllEntries |
            QDir::Hidden |
            QDir::System
        );

        //recursively copy the child
        for (const QFileInfo &entry : entries) {
            QString childSource = entry.absoluteFilePath();
            QString childDestination = QDir(destinationPath).filePath(entry.fileName());

            // recursively copy the child
            if (!copyRecursively(childSource, childDestination)) {
                return false;
            }
        }
    }

    return true;
}

QString MainWindow::makeUniqueCopyPath(const QString &sourcePath, const QString &destinationDirectory) {
    
    QFileInfo sourceInfo(sourcePath);
    QString originialName = sourceInfo.fileName();
    QDir destinationDir(destinationDirectory);

    // first try original filename
    QString candidate = destinationDir.filePath(originialName);

    if (!QFileInfo::exists(candidate)){
        return candidate;
    }

    QString baseName;
    QString extension;

    // dir name
    if (sourceInfo.isDir()){
        baseName = originialName;
        extension = "";
    } else {

        // file name
        baseName = sourceInfo.completeBaseName();
        QString suffix = sourceInfo.completeSuffix();

        if (!suffix.isEmpty()){
            extension = "." + suffix;
        }
    }

    QString copiedName = baseName + " (copy)" + extension;
    candidate = destinationDir.filePath(copiedName);

    if(!QFileInfo::exists(candidate)){
        return candidate;
    }

    int copyNumber = 2;

    while(true){
        copiedName = baseName + QString(" (copy %1)").arg(copyNumber) + extension;
        candidate = destinationDir.filePath(copiedName);

        if (!QFileInfo::exists(candidate)){
            return candidate;
        }
        copyNumber++;
    }


}

// -------------------------------------------------
// RENAME
// -------------------------------------------------

void MainWindow::renameSelectedItem(
    QWidget *page
)
{
    TabState &state =
        tabStates[page];

    QStringList paths =
        selectedFilePaths(state.fileTreeView);

    if (paths.size() != 1)
    {
        return;
    }

    const QString &oldPath =
        paths.first();

    QFileInfo info(oldPath);

    QString oldName =
        info.fileName();

    bool accepted = false;

    QString newName =
        QInputDialog::getText(
            this,
            "Rename",
            "New name:",
            QLineEdit::Normal,
            oldName,
            &accepted
        ).trimmed();


    // User pressed Cancel.
    if (!accepted)
    {
        return;
    }

    // Empty names are invalid.
    if (newName.isEmpty())
    {
        return;
    }

    // Don't allow "/" inside a filename.
    //
    // Linux uses "/" as the directory separator.
    if (newName.contains('/'))
    {
        QMessageBox::warning(
            this,
            "Invalid Name",
            "A file name cannot contain '/'."
        );

        return;
    }

    // Nothing changed.
    if (newName == oldName)
    {
        return;
    }


    // Work inside the item's parent directory.
    QDir parentDirectory =
        info.dir();

    // Check whether the requested destination
    // already exists.
    QString newPath =
        parentDirectory.filePath(
            newName
        );

    if (QFileInfo::exists(newPath))
    {
        QMessageBox::warning(
            this,
            "Rename Failed",
            "An item with that name already exists."
        );

        return;
    }


    // QDir::rename() works for both files
    // and directories within the directory.
    if (!parentDirectory.rename(
            oldName,
            newName
        ))
    {
        QMessageBox::warning(
            this,
            "Rename Failed",
            "The item could not be renamed."
        );

        return;
    }

    // Both names are supplied so stale descendants under the old directory
    // key and parent totals are cleared together.
    fileModel->invalidatePaths(
        {
            oldPath,
            newPath
        }
    );
}

// -------------------------------------------------
// DELETE
// -------------------------------------------------

void MainWindow::deleteSelectedItems(
    QWidget *page
)
{
    TabState &state =
        tabStates[page];

    QStringList paths =
        selectedFilePaths(state.fileTreeView);

    if (paths.isEmpty())
    {
        return;
    }

    QString message;

    if (paths.size() == 1)
    {
        QFileInfo info(paths.first());

        message =
            QString(
                "Move \"%1\" to the Trash?"
            ).arg(info.fileName());
    }
    else
    {
        message =
            QString(
                "Move %1 selected items to the Trash?"
            ).arg(paths.size());
    }


    QMessageBox::StandardButton answer =
        QMessageBox::question(
            this,
            "Move to Trash",
            message,
            QMessageBox::Yes |
                QMessageBox::No,
            QMessageBox::No
        );


    // User cancelled.
    if (answer != QMessageBox::Yes)
    {
        return;
    }


    QStringList failedPaths;
    QStringList deletedPaths;

    for (const QString &path : paths)
    {
        if (!QFile::moveToTrash(path))
        {
            failedPaths.append(path);
        }
        else
        {
            deletedPaths.append(path);
        }
    }

    // Invalidate all successful deletions as one cache update. Failed paths
    // still exist, so their cached values remain valid.
    fileModel->invalidatePaths(deletedPaths);

    if (!failedPaths.isEmpty())
    {
        QMessageBox::warning(
            this,
            "Delete Failed",
            QString(
                "%1 item(s) could not be moved to the Trash."
            ).arg(failedPaths.size())
        );
    }
}