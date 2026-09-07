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

void MainWindow::configureFileTreeView (
    QTreeView *fileTreeView
) {
    fileTreeView->setModel(fileModel);

    // This header consumes premature Size clicks before QTreeView can change
    // the active sort and requests every directory needed by that sort.
    auto *header =
        new DirectorySizeHeaderView(
            fileTreeView,
            fileModel
        );

    fileTreeView->setHeader(header);

    // set the default width of of the file columns
    // 0 = Name, 1 = Size, 2 = Type, 3 = Date Modified
    fileTreeView->setColumnWidth(0, 255);
    fileTreeView->setColumnWidth(1, 100);
    fileTreeView->setColumnWidth(2, 250);
    fileTreeView->setColumnWidth(3, 170);

    // configure column sorting
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

    fileTreeView->setDragEnabled(true);
    fileTreeView->setDragDropMode(QAbstractItemView::DragOnly);
    fileTreeView->setDefaultDropAction(Qt::CopyAction);
}

// get selected paths from the QTreeView
QStringList MainWindow::selectedFilePaths(QTreeView *fileTreeView) const {
    QItemSelectionModel *selectionModel = fileTreeView->selectionModel();
    QModelIndexList selectedIndexes = selectionModel->selectedRows(0);
    QStringList paths;
    for (const QModelIndex &index : selectedIndexes) {
        paths.append(fileModel->filePath(index));
    }

    return paths;
}