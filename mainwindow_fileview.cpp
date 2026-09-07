#include "mainwindow.h"
#include <qitemselectionmodel.h>
#include <qnamespace.h>
#include <qtreeview.h>
#include <qheaderview.h>
#include <qfilesystemmodel.h>
#include <qabstractitemview.h>
#include <qitemselectionmodel.h>

void MainWindow::configureFileTreeView (
    QTreeView *fileTreeView
) {
    fileTreeView->setModel(fileModel);

    // set the default width of of the file columns
    // 0 = Name, 1 = Size, 2 = Type, 3 = Date Modified
    fileTreeView->setColumnWidth(0, 355);
    fileTreeView->setColumnWidth(2, 100);
    fileTreeView->setColumnWidth(3, 100);
    fileTreeView->setColumnWidth(3, 170);

    QHeaderView *header = fileTreeView->header();

    // configure column sorting
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    fileTreeView->setSortingEnabled(true);
    fileTreeView->sortByColumn(0, Qt::AscendingOrder);

    // configure selection mode to desktop
    fileTreeView->setSelectionMode(
        QAbstractItemView::ExtendedSelection
    );
}