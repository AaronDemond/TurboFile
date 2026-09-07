#include "mainwindow.h"

#include <QFileSystemModel>
#include <QTreeView>

#include <QMenu>
#include <QAction>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>

#include <QDesktopServices>
#include <QUrl>

#include <QApplication>
#include <QClipboard>
#include <QMimeData>

#include <QInputDialog>
#include <QMessageBox>
#include <qabstractitemmodel.h>
#include <qaction.h>
#include <qapplication.h>
#include <qdesktopservices.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qlist.h>
#include <qmessagebox.h>
#include <qstringview.h>
#include <qurl.h>
#include <qwidget.h>


// -------------------------------------------------
// CONTEXT MENU
// -------------------------------------------------

void MainWindow::showFileContextMenu(
    QWidget *page,
    const QPoint &position
)
{
    // Get the state belonging to this browser tab.
    TabState &state =
        tabStates[page];

    // Determine which filesystem item is underneath
    // the mouse cursor.
    QModelIndex index =
        state.fileTreeView->indexAt(position);

    // Create the menu.
    QMenu menu(state.fileTreeView);

    // If the user clicked an actual file/directory,
    // show item-specific actions.
    if (index.isValid())
    {
        // Make the right-clicked item the current selection.
        state.fileTreeView->setCurrentIndex(index);

        QAction *openAction =
            menu.addAction("Open");

        QAction *copyAction =
            menu.addAction("Copy");

        QAction *renameAction =
            menu.addAction("Rename");

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
                state.fileTreeView
                    ->viewport()
                    ->mapToGlobal(position)
            );

        if (selectedAction == openAction)
        {
            openItem(page, index);
        }
        else if (selectedAction == copyAction)
        {
            copyItemToClipboard(index);
        }
        else if (selectedAction == renameAction)
        {
            renameItem(index);
        }
        else if (selectedAction == deleteAction)
        {
            deleteItem(index);
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
            state.fileTreeView
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

// -------------------------------------------------
// COPY
// -------------------------------------------------

void MainWindow::copyItemToClipboard(
    const QModelIndex &index
)
{
    // Get the actual filesystem path of the selected item.
    QString path =
        fileModel->filePath(index);

    // Convert the normal path:
    QUrl fileUrl =
        QUrl::fromLocalFile(path);

    // QMimeData describes what kind of content
    // we're putting onto the clipboard.
    // We allocate it with new because QClipboard
    // takes ownership of it.
    auto *mimeData =
        new QMimeData;

    // Put our file URL in a QList because the clipboard
    // URL format supports multiple files.
    QList<QUrl> urls;

    urls.append(fileUrl);

    // This creates the standard text/uri-list
    // clipboard representation used by desktop apps.
    mimeData->setUrls(urls);

    // Also provide the normal path as plain text.
    mimeData->setText(path);


    // -------------------------------------------------
    // GNOME FILE-MANAGER COMPATIBILITY
    // -------------------------------------------------

    // GNOME/Nautilus also recognizes this MIME type
    // for file Copy/Paste operations.
    // file:///some/file
    QByteArray gnomeClipboardData =
        "copy\n" +
        fileUrl.toEncoded();

    mimeData->setData(
        "x-special/gnome-copied-files",
        gnomeClipboardData
    );


    // -------------------------------------------------
    // KDE FILE-MANAGER COMPATIBILITY
    // -------------------------------------------------

    // KDE applications can use this to distinguish
    // Copy from Cut.
    //
    // 0 means Copy.
    mimeData->setData(
        "application/x-kde-cutselection",
        QByteArray("0")
    );


    // Put everything onto the system clipboard.
    //
    // QClipboard takes ownership of mimeData,
    // so we must NOT delete it ourselves.
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
        
        // perform the copy
        if (!copyRecursively(sourcePath, destinationPath)){
            QMessageBox::warning(
                this,
                "Paste Failed",
                QString("Could not copy:\n%1").arg((sourcePath)));   
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

void MainWindow::renameItem(
    const QModelIndex &index
)
{
    QString oldPath =
        fileModel->filePath(index);

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
    }
}

// -------------------------------------------------
// DELETE
// -------------------------------------------------

void MainWindow::deleteItem(
    const QModelIndex &index
)
{
    QString path =
        fileModel->filePath(index);

    QFileInfo info(path);


    QMessageBox::StandardButton answer =
        QMessageBox::question(
            this,
            "Move to Trash",
            QString(
                "Move \"%1\" to the Trash?"
            ).arg(info.fileName()),
            QMessageBox::Yes |
                QMessageBox::No,
            QMessageBox::No
        );


    // User cancelled.
    if (answer != QMessageBox::Yes)
    {
        return;
    }


    // Use the desktop's Trash instead of permanently
    // destroying the file.
    if (!QFile::moveToTrash(path))
    {
        QMessageBox::warning(
            this,
            "Delete Failed",
            "The item could not be moved to the Trash."
        );
    }
}