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
    void mousePressEvent(QMouseEvent *event) override;

private:
    // Return from Size sorting when an external filesystem change makes one
    // or more directory totals pending again.
    void restoreLastStableSort();

    QTreeView *fileTreeView;
    DirectorySizeSortProxyModel *fileModel;
    int lastSortColumn = 0;
    Qt::SortOrder lastSortOrder = Qt::AscendingOrder;
};

#endif // DIRECTORYSIZEHEADERVIEW_H