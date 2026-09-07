#include "mainwindow.h"
#include "directorysizesortproxymodel.h"
#include "sidebar/pinnedsidebar.h"
#include "shellscriptdialog.h"

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
#include <QProcess>
#include <QStandardPaths>


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

        // Pin is offered only for a single unpinned directory. Files and
        // multi-selections stay out of the sidebar.
        QAction *pinAction = nullptr;
        if (selectedPaths.size() == 1)
        {
            const QString &selectedPath = selectedPaths.first();
            QFileInfo selectedInfo(selectedPath);
            if (selectedInfo.isDir() &&
                !pinnedSidebar->isPinned(selectedPath))
            {
                pinAction = menu.addAction("Pin");
            }
        }

        menu.addSeparator();

        // Creation targets the directory displayed by the tab rather than
        // the right-clicked item. A submenu keeps the two creation choices
        // grouped without making the main context menu unnecessarily long.
        QMenu *createNewMenu =
            menu.addMenu("Create New");

        QAction *newFileAction =
            createNewMenu->addAction("File");

        QAction *newFolderAction =
            createNewMenu->addAction("Folder");

        QAction *openTerminalAction =
            menu.addAction("Open Terminal Here");

        QAction *runShellScriptAction =
            menu.addAction("Run Shell script here");

        menu.addSeparator();

        QAction *copyAction =
            menu.addAction("Copy");

        // Duplicate operates on the complete preserved selection and creates
        // each copy beside its source with the existing unique-name helper.
        QAction *duplicateAction =
            menu.addAction("Duplicate Here");

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
        else if (pinAction != nullptr && selectedAction == pinAction)
        {
            pinnedSidebar->pinDirectory(selectedPaths.first());
        }
        else if (selectedAction == newFileAction)
        {
            createNewFile(page);
        }
        else if (selectedAction == newFolderAction)
        {
            createNewFolder(page);
        }
        else if (selectedAction == openTerminalAction)
        {
            openTerminalHere(page);
        }
        else if (selectedAction == runShellScriptAction)
        {
            runShellScriptHere(page);
        }
        else if (selectedAction == copyAction)
        {
            copySelectedItemsToClipboard(page);
        }
        else if (selectedAction == duplicateAction)
        {
            duplicateSelectedItems(page);
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

    // Empty space has no item-specific actions, but creating, opening a
    // terminal, and pasting all target the tab's displayed directory.
    QMenu *createNewMenu =
        menu.addMenu("Create New");

    QAction *newFileAction =
        createNewMenu->addAction("File");

    QAction *newFolderAction =
        createNewMenu->addAction("Folder");

    QAction *openTerminalAction =
        menu.addAction("Open Terminal Here");

    QAction *runShellScriptAction =
        menu.addAction("Run Shell script here");

    menu.addSeparator();

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

    if (selectedAction == newFileAction)
    {
        createNewFile(page);
    }
    else if (selectedAction == newFolderAction)
    {
        createNewFolder(page);
    }
    else if (selectedAction == openTerminalAction)
    {
        openTerminalHere(page);
    }
    else if (selectedAction == runShellScriptAction)
    {
        runShellScriptHere(page);
    }
    else if (selectedAction == pasteAction)
    {
        pasteClipboardItems(page);
    }
}

// -------------------------------------------------
// CREATE / TERMINAL / DUPLICATE
// -------------------------------------------------

QString MainWindow::currentDirectoryPath(QWidget *page) const
{
    // The page pointer is the stable key for per-tab state even when users
    // reorder tabs and their numeric QTabWidget indexes change.
    auto stateIterator =
        tabStates.constFind(page);

    if (stateIterator == tabStates.constEnd())
    {
        return QString();
    }

    // The tree's root index represents the directory currently displayed in
    // this tab. filePath() maps the proxy index back to a real path.
    QModelIndex rootIndex =
        stateIterator->fileTreeView->rootIndex();

    return fileModel->filePath(rootIndex);
}

void MainWindow::createNewFile(QWidget *page)
{
    QString directoryPath =
        currentDirectoryPath(page);

    if (directoryPath.isEmpty())
    {
        return;
    }

    bool accepted = false;

    // Ask only for one filename because the destination directory is already
    // determined by the tab where the context menu was opened.
    QString fileName =
        QInputDialog::getText(
            this,
            "Create New File",
            "File name:",
            QLineEdit::Normal,
            QString(),
            &accepted
        ).trimmed();

    if (!accepted || fileName.isEmpty())
    {
        return;
    }

    // A single context-menu operation creates one item in the current folder,
    // so path separators and the special dot directory names are invalid.
    if (
        fileName.contains('/') ||
        fileName == "." ||
        fileName == ".."
    )
    {
        QMessageBox::warning(
            this,
            "Invalid File Name",
            "Enter a single file name without '/'."
        );
        return;
    }

    QString filePath =
        QDir(directoryPath).filePath(fileName);
    QFile file(filePath);

    // NewOnly makes existence checking and creation one atomic operation. It
    // prevents an existing item from being overwritten between two calls.
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        QMessageBox::warning(
            this,
            "Create File Failed",
            QString(
                "Could not create:\n%1"
            ).arg(filePath)
        );
        return;
    }

    file.close();

    // The new zero-byte file changes every cached ancestor directory total,
    // even though its own byte contribution is currently zero.
    fileModel->invalidatePaths({filePath});
}

void MainWindow::createNewFolder(QWidget *page)
{
    QString directoryPath =
        currentDirectoryPath(page);

    if (directoryPath.isEmpty())
    {
        return;
    }

    bool accepted = false;

    QString folderName =
        QInputDialog::getText(
            this,
            "Create New Folder",
            "Folder name:",
            QLineEdit::Normal,
            QString(),
            &accepted
        ).trimmed();

    if (!accepted || folderName.isEmpty())
    {
        return;
    }

    if (
        folderName.contains('/') ||
        folderName == "." ||
        folderName == ".."
    )
    {
        QMessageBox::warning(
            this,
            "Invalid Folder Name",
            "Enter a single folder name without '/'."
        );
        return;
    }

    QDir directory(directoryPath);
    QString folderPath =
        directory.filePath(folderName);

    // mkdir() creates exactly one child directory and fails rather than
    // silently accepting an existing item with the same name.
    if (!directory.mkdir(folderName))
    {
        QMessageBox::warning(
            this,
            "Create Folder Failed",
            QString(
                "Could not create:\n%1"
            ).arg(folderPath)
        );
        return;
    }

    fileModel->invalidatePaths({folderPath});
}

void MainWindow::openTerminalHere(QWidget *page)
{
    QString directoryPath =
        currentDirectoryPath(page);

    // A directory can disappear after the tab navigates to it but before the
    // context-menu action runs. Refuse to launch a terminal that may silently
    // fall back to an unrelated working directory.
    if (
        directoryPath.isEmpty() ||
        !QDir(directoryPath).exists()
    )
    {
        QMessageBox::warning(
            this,
            "Open Terminal Failed",
            "The current directory no longer exists."
        );
        return;
    }

    // Try the desktop's generic launcher first, followed by common terminal
    // applications. startDetached() receives the directory separately so the
    // shell starts there without path quoting or command-string parsing.
    const QStringList terminalCandidates =
        {
            "x-terminal-emulator",
            "gnome-terminal",
            "konsole",
            "xfce4-terminal",
            "mate-terminal",
            "lxterminal",
            "xterm"
        };

    for (const QString &candidate : terminalCandidates)
    {
        QString executable =
            QStandardPaths::findExecutable(candidate);

        if (executable.isEmpty())
        {
            continue;
        }

        if (
            QProcess::startDetached(
                executable,
                QStringList(),
                directoryPath
            )
        )
        {
            return;
        }
    }

    QMessageBox::warning(
        this,
        "Open Terminal Failed",
        "No supported terminal application could be started."
    );
}

void MainWindow::runShellScriptHere(QWidget *page)
{
    QString directoryPath =
        currentDirectoryPath(page);

    // Resolve and validate the tab root before opening the editor. This keeps
    // the displayed working directory truthful if the folder was removed
    // after the tab originally navigated there.
    if (
        directoryPath.isEmpty() ||
        !QDir(directoryPath).exists()
    )
    {
        QMessageBox::warning(
            this,
            "Run Shell Script Failed",
            "The current directory no longer exists."
        );
        return;
    }

    // The dialog is stack-owned by this function and executes its own modal
    // event loop. QProcess remains asynchronous, so editing, output updates,
    // and Stop continue responding while Bash is running.
    ShellScriptDialog dialog(directoryPath, this);

    connect(
        &dialog,
        &ShellScriptDialog::scriptFinished,
        this,
        [this, directoryPath]()
        {
            // Shell code can modify any nested path without going through
            // TurboFile's normal action methods. Invalidating the working
            // directory clears its cached descendants and ancestor totals.
            fileModel->invalidatePaths({directoryPath});
        }
    );

    dialog.exec();
}

void MainWindow::duplicateSelectedItems(QWidget *page)
{
    auto stateIterator =
        tabStates.constFind(page);

    if (stateIterator == tabStates.constEnd())
    {
        return;
    }

    QStringList sourcePaths =
        selectedFilePaths(stateIterator->fileTreeView);

    if (sourcePaths.isEmpty())
    {
        return;
    }

    QStringList failedPaths;
    QStringList createdPaths;

    // Every source is copied into its own parent directory. This remains
    // correct even if a future view permits selections spanning directories.
    for (const QString &sourcePath : sourcePaths)
    {
        QFileInfo sourceInfo(sourcePath);

        if (!sourceInfo.exists())
        {
            failedPaths.append(sourcePath);
            continue;
        }

        QString destinationPath =
            makeUniqueCopyPath(
                sourcePath,
                sourceInfo.absolutePath()
            );

        if (!copyRecursively(sourcePath, destinationPath))
        {
            // Recursive copying can fail after creating part of a directory.
            // Remove only that newly generated destination so the failed
            // action does not leave an incomplete duplicate in the file view.
            QFileInfo destinationInfo(destinationPath);

            if (
                destinationInfo.isSymbolicLink() ||
                destinationInfo.isFile()
            )
            {
                QFile::remove(destinationPath);
            }
            else if (destinationInfo.isDir())
            {
                QDir(destinationPath).removeRecursively();
            }

            failedPaths.append(sourcePath);
            continue;
        }

        createdPaths.append(destinationPath);
    }

    // Batch invalidation avoids repeatedly cancelling and rescheduling the
    // same parent directory while several selected items are duplicated.
    fileModel->invalidatePaths(createdPaths);

    if (!failedPaths.isEmpty())
    {
        QString failureMessage;

        if (failedPaths.size() == 1)
        {
            failureMessage =
                QString(
                    "Could not duplicate:\n%1"
                ).arg(failedPaths.first());
        }
        else
        {
            failureMessage =
                QString(
                    "%1 items could not be duplicated.\n\nFirst failure:\n%2"
                ).arg(
                    QString::number(failedPaths.size()),
                    failedPaths.first()
                );
        }

        QMessageBox::warning(
            this,
            "Duplicate Failed",
            failureMessage
        );
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
            // The destination and all cached ancestors may now have different
            // totals. The model also clears cached descendants when needed.
            fileModel->invalidatePaths({destinationPath});
        }
    }


}

bool MainWindow::copyRecursively(const QString &sourcePath, const QString &destinationPath) {
    QFileInfo sourceInfo(sourcePath);

    // Handle symbolic links before asking whether their targets exist or are
    // directories. Copying a link as a directory could recurse through a
    // Windows junction or Linux symlink cycle; QFile::link preserves a link
    // to the same resolved target instead.
    if (sourceInfo.isSymbolicLink()){
        return QFile::link(
            sourceInfo.symLinkTarget(),
            destinationPath
        );
    }

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