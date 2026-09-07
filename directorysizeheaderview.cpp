#include "directorysizeheaderview.h"
#include "directorysizesortproxymodel.h"

#include <QMouseEvent>
#include <QTreeView>

DirectorySizeHeaderView::DirectorySizeHeaderView(
    QTreeView *fileTreeView,
    DirectorySizeSortProxyModel *fileModel
)
    : QHeaderView(Qt::Horizontal, fileTreeView)
    , fileTreeView(fileTreeView)
    , fileModel(fileModel)
{
    // Remember the most recent stable non-Size sort independently for each
    // tab because every tree owns its own header and root index.
    connect(
        this,
        &QHeaderView::sectionClicked,
        this,
        [this](int column)
        {
            // Size is never stored as a fallback because it can become invalid
            // after a filesystem mutation and must then leave the active sort.
            if (column == 1)
            {
                return;
            }

            // QHeaderView updates its indicator before emitting sectionClicked,
            // so the current order is the one the user just selected.
            lastSortColumn = column;
            lastSortOrder = sortIndicatorOrder();
        }
    );

    // The shared model may invalidate totals because of an app operation or an
    // external filesystem event. Each tab independently checks whether it is
    // currently using those values before restoring its own fallback ordering.
    connect(
        fileModel,
        &DirectorySizeSortProxyModel::directorySizesInvalidated,
        this,
        [this]()
        {
            if (sortIndicatorSection() == 1)
            {
                restoreLastStableSort();
            }
        }
    );
}

void DirectorySizeHeaderView::mousePressEvent(QMouseEvent *event)
{
    // Translate the mouse coordinate into a logical model column. Visual order
    // may differ if users drag header sections, so a pixel position alone is
    // not a stable way to recognize the Size column.
    int column =
        logicalIndexAt(event->position().toPoint());

    // Only a primary-button press on Size needs gating. Resizing, context-menu
    // input, and every other sortable column retain QHeaderView behavior.
    if (
        event->button() == Qt::LeftButton &&
        column == 1 &&
        !fileModel->directorySizesReady(
            fileTreeView->rootIndex()
        )
    )
    {
        // Request off-screen sibling directories as well as visible ones.
        // Consuming the press prevents QHeaderView from changing its sort
        // indicator or asking the proxy to sort incomplete values.
        fileModel->requestDirectorySizes(
            fileTreeView->rootIndex()
        );
        event->accept();
        return;
    }

    // Ready Size columns and all non-Size columns continue through Qt's normal
    // press/release sequence, which updates the indicator and sorts the proxy.
    QHeaderView::mousePressEvent(event);
}

void DirectorySizeHeaderView::restoreLastStableSort()
{
    // Update both pieces of state: setSortIndicator() repairs the visual arrow,
    // while sortByColumn() restores the proxy's actual row ordering.
    setSortIndicator(
        lastSortColumn,
        lastSortOrder
    );

    fileTreeView->sortByColumn(
        lastSortColumn,
        lastSortOrder
    );
}