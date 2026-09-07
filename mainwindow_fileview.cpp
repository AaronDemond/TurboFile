#include "mainwindow.h"
#include "directorysizeheaderview.h"
#include "directorysizesortproxymodel.h"
#include <qabstractitemmodel.h>
#include <qcontainerfwd.h>
#include <qitemselectionmodel.h>
#include <qnamespace.h>
#include <qtreeview.h>
#include <qheaderview.h>
#include <qabstractitemview.h>
#include <qitemselectionmodel.h>
#include <qitemselectionmodel.h>

// Attach one tab's tree to the shared sorting proxy and configure the view
// behavior that depends on lazy directory-size calculation.
void MainWindow::configureFileTreeView (
    QTreeView *fileTreeView
) {
    // Every tab shares one proxy, source model, cache, and worker pool. The
    // QTreeView stores proxy indexes, while the proxy maps filesystem calls.
    fileTreeView->setModel(fileModel);

    // This header consumes premature Size clicks before QTreeView can change
    // the active sort and requests every directory needed by that sort.
    auto *header =
        new DirectorySizeHeaderView(
            fileTreeView,
            fileModel
        );

    // QTreeView takes ownership of the replacement header because the tree is
    // passed as its QObject parent during construction.
    fileTreeView->setHeader(header);

    // set the default width of of the file columns
    // 0 = Name, 1 = Size, 2 = Type, 3 = Date Modified
    fileTreeView->setColumnWidth(0, 255);
    fileTreeView->setColumnWidth(1, 100);
    fileTreeView->setColumnWidth(2, 250);
    fileTreeView->setColumnWidth(3, 170);

    // Enable normal sorting for every column. DirectorySizeHeaderView consumes
    // a Size click before it reaches this mechanism when totals are incomplete.
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    fileTreeView->setSortingEnabled(true);
    fileTreeView->sortByColumn(0, Qt::AscendingOrder);

    // configure selection mode to desktop
    fileTreeView->setSelectionMode(
        QAbstractItemView::ExtendedSelection
    );

    fileTreeView->setSelectionBehavior(
        QAbstractItemView::SelectRows
    );

    // DragDrop so a pane can receive copies from the other pane while still
    // acting as a drag source for pinning. BrowserFileTreeView intercepts
    // dropEvent and never lets QFileSystemModel move files.
    fileTreeView->setDragEnabled(true);
    fileTreeView->setAcceptDrops(true);
    fileTreeView->setDragDropMode(QAbstractItemView::DragDrop);
    fileTreeView->setDefaultDropAction(Qt::CopyAction);
}

// Convert the selected proxy rows into concrete filesystem paths for actions.
QStringList MainWindow::selectedFilePaths(QTreeView *fileTreeView) const {
    // The selection model belongs to the tree. selectedRows(0) returns one
    // Name-column index per selected item rather than one index per column.
    QItemSelectionModel *selectionModel = fileTreeView->selectionModel();
    QModelIndexList selectedIndexes = selectionModel->selectedRows(0);
    QStringList paths;

    // fileModel maps each proxy index back through the source filesystem model
    // before returning its absolute path.
    for (const QModelIndex &index : selectedIndexes) {
        paths.append(fileModel->filePath(index));
    }

    return paths;
}