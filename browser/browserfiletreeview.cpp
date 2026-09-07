#include "browserfiletreeview.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

// Parent is the BrowserPane. Qt object ownership destroys the view with
// the pane; nothing else should delete it.
BrowserFileTreeView::BrowserFileTreeView(QWidget *parent)
    : QTreeView(parent)
{
    // DragDrop lets this view be both a pin/file drag source and a
    // drop target for the other pane. Default is copy, never move.
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setDropIndicatorShown(true);
}

// Local file:// URLs only. Anything else (including the sidebar pin MIME)
// is refused so the cursor shows the forbidden glyph.
bool BrowserFileTreeView::canAcceptDrop(const QMimeData *mime) const
{
    if (mime == nullptr || !mime->hasUrls())
    {
        return false;
    }

    const QList<QUrl> urls = mime->urls();
    if (urls.isEmpty())
    {
        return false;
    }

    for (const QUrl &url : urls)
    {
        if (!url.isLocalFile())
        {
            return false;
        }
    }

    return true;
}

void BrowserFileTreeView::dragEnterEvent(QDragEnterEvent *event)
{
    if (canAcceptDrop(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }

    event->ignore();
}

void BrowserFileTreeView::dragMoveEvent(QDragMoveEvent *event)
{
    if (canAcceptDrop(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }

    event->ignore();
}

// Do not call QTreeView::dropEvent. That would let QFileSystemModel
// perform its own drop (often a move). TurboFile copies through
// copyRecursively so both panes and the size cache stay consistent.
void BrowserFileTreeView::dropEvent(QDropEvent *event)
{
    if (!canAcceptDrop(event->mimeData()))
    {
        event->ignore();
        return;
    }

    // position() is the Qt 6 cursor location in viewport coordinates,
    // which is what indexAt() expects.
    const QModelIndex hoverIndex =
        indexAt(event->position().toPoint());

    emit filesDropped(event->mimeData()->urls(), hoverIndex);

    event->setDropAction(Qt::CopyAction);
    event->accept();
}
