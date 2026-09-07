#include "browserfilelistview.h"

#include <QAbstractItemView>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

// Configure the icon grid as both a drag source and a copy-only drop target.
BrowserFileListView::BrowserFileListView(QWidget *parent)
    : QListView(parent)
{
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setWrapping(true);
    setWordWrap(true);
    setUniformItemSizes(false);

    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setDropIndicatorShown(true);
}

bool BrowserFileListView::canAcceptDrop(
    const QMimeData *mimeData
) const
{
    if (mimeData == nullptr || !mimeData->hasUrls())
    {
        return false;
    }

    const QList<QUrl> urls = mimeData->urls();

    if (urls.isEmpty())
    {
        return false;
    }

    // Every URL must resolve to a local path because the existing recursive
    // copy helper does not download or otherwise resolve remote resources.
    for (const QUrl &url : urls)
    {
        if (!url.isLocalFile())
        {
            return false;
        }
    }

    return true;
}

void BrowserFileListView::dragEnterEvent(QDragEnterEvent *event)
{
    if (canAcceptDrop(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }

    event->ignore();
}

void BrowserFileListView::dragMoveEvent(QDragMoveEvent *event)
{
    if (canAcceptDrop(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }

    event->ignore();
}

void BrowserFileListView::dropEvent(QDropEvent *event)
{
    if (!canAcceptDrop(event->mimeData()))
    {
        event->ignore();
        return;
    }

    // indexAt() receives viewport coordinates in Qt 6. MainWindow later
    // decides whether this index names a destination directory or whether the
    // pane's current root should receive the copied files.
    const QModelIndex hoverIndex =
        indexAt(event->position().toPoint());

    emit filesDropped(
        event->mimeData()->urls(),
        hoverIndex
    );

    // Do not call QListView::dropEvent(). Its default model path could move
    // items; TurboFile centralizes copying and cache invalidation instead.
    event->setDropAction(Qt::CopyAction);
    event->accept();
}