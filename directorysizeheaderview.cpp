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
            if (column == 1)
            {
                return;
            }

            lastSortColumn = column;
            lastSortOrder = sortIndicatorOrder();
        }
    );

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
    int column =
        logicalIndexAt(event->position().toPoint());

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

    QHeaderView::mousePressEvent(event);
}

void DirectorySizeHeaderView::restoreLastStableSort()
{
    setSortIndicator(
        lastSortColumn,
        lastSortOrder
    );

    fileTreeView->sortByColumn(
        lastSortColumn,
        lastSortOrder
    );
}