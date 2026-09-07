#pragma once
#ifndef DIRECTORYSIZEHEADERVIEW_H
#define DIRECTORYSIZEHEADERVIEW_H

#include <QHeaderView>

class DirectorySizeSortProxyModel;
class QMouseEvent;
class QTreeView;

// Prevents an incomplete asynchronous Size column from becoming the active
// sort while retaining normal header behavior for every other column.
class DirectorySizeHeaderView : public QHeaderView
{
public:
    DirectorySizeHeaderView(
        QTreeView *fileTreeView,
        DirectorySizeSortProxyModel *fileModel
    );

protected:
    // Intercept the press rather than reacting to sectionClicked afterward.
    // This prevents Qt from activating an incomplete Size sort even briefly.
    void mousePressEvent(QMouseEvent *event) override;

private:
    // Return from Size sorting when an external filesystem change makes one
    // or more directory or mounted-drive totals pending again.
    void restoreLastStableSort();

    // Both pointers are borrowed. The tree owns this header, and MainWindow
    // keeps the shared proxy alive longer than every per-tab tree and header.
    QTreeView *fileTreeView;
    DirectorySizeSortProxyModel *fileModel;

    // Each tab remembers its own last usable ordering. The defaults match the
    // initial Name-ascending sort configured when the tree is created.
    int lastSortColumn = 0;
    Qt::SortOrder lastSortOrder = Qt::AscendingOrder;
};

#endif // DIRECTORYSIZEHEADERVIEW_H