#include "mainwindow.h"
#include "directorysizeheaderview.h"
#include "directorysizesortproxymodel.h"
#include <QAbstractItemView>
#include <qabstractitemmodel.h>
#include <qcontainerfwd.h>
#include <qitemselectionmodel.h>
#include <qnamespace.h>
#include <qtreeview.h>
#include <qheaderview.h>
#include <qabstractitemview.h>
#include <qitemselectionmodel.h>
#include <qitemselectionmodel.h>
#include <QColor>
#include <QPalette>

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

    // Keep the Name column at a useful 300-pixel starting width while still
    // allowing the user to resize it manually. Size, Type, and Date Modified
    // split all remaining horizontal space equally, so each pane adapts when
    // the window or the pane splitter is resized instead of retaining stale
    // hard-coded widths.
    header->setSectionResizeMode(0, QHeaderView::Interactive);
    header->resizeSection(0, 300);

    for (int column = 1; column < fileModel->columnCount(); column++)
    {
        header->setSectionResizeMode(
            column,
            QHeaderView::Stretch
        );
    }

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

    // Start from the active theme's Base color, then move its lightness far
    // enough in the opposite direction to make neighboring rows unmistakable.
    // Only AlternateBase changes: text, selections, focus, and disabled colors
    // continue to come directly from the desktop palette.
    QPalette treePalette = fileTreeView->palette();
    QColor baseColor = treePalette.color(QPalette::Base);
    QColor alternateColor =
        baseColor.lightness() < 128
            ? baseColor.lighter(140)
            : baseColor.darker(112);

    treePalette.setColor(
        QPalette::AlternateBase,
        alternateColor
    );
    fileTreeView->setPalette(treePalette);

    // QTreeView now alternates between Base and the higher-contrast
    // AlternateBase calculated above, making each wide row easier to follow.
    fileTreeView->setAlternatingRowColors(true);

    // DragDrop so a pane can receive copies from the other pane while still
    // acting as a drag source for pinning. BrowserFileTreeView intercepts
    // dropEvent and never lets QFileSystemModel move files.
    fileTreeView->setDragEnabled(true);
    fileTreeView->setAcceptDrops(true);
    fileTreeView->setDragDropMode(QAbstractItemView::DragDrop);
    fileTreeView->setDefaultDropAction(Qt::CopyAction);
}

// Convert the selected proxy rows into concrete filesystem paths for actions.
QStringList MainWindow::selectedFilePaths(QAbstractItemView *fileView) const {
    // The selection model belongs to the active view. selectedRows(0) returns one
    // Name-column index per selected item rather than one index per column.
    QItemSelectionModel *selectionModel = fileView->selectionModel();
    QModelIndexList selectedIndexes = selectionModel->selectedRows(0);
    QStringList paths;

    // fileModel maps each proxy index back through the source filesystem model
    // before returning its absolute path.
    for (const QModelIndex &index : selectedIndexes) {
        paths.append(fileModel->filePath(index));
    }

    return paths;
}