#pragma once

#include <QList>
#include <QModelIndex>
#include <QTreeView>
#include <QUrl>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;

// QTreeView used by one BrowserPane.
//
// The stock view plus QFileSystemModel would move or copy through Qt's
// default drop path. TurboFile always copies via MainWindow::copyRecursively
// so inter-pane drops never silently move, and so the size cache is
// invalidated the same way clipboard paste is. This subclass therefore
// accepts URL drags and emits them instead of calling QTreeView::dropEvent.
class BrowserFileTreeView : public QTreeView
{
    Q_OBJECT

public:
    explicit BrowserFileTreeView(QWidget *parent = nullptr);

signals:
    // urls are the dragged local files. hoverIndex is the row under the
    // cursor, which may be invalid when the drop is on empty space.
    // The pane maps that index to a destination directory.
    void filesDropped(
        const QList<QUrl> &urls,
        const QModelIndex &hoverIndex
    );

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    // True only for mime data that carries local filesystem URLs.
    // Pin-reorder MIME has no URLs, so those drags are ignored here.
    bool canAcceptDrop(const QMimeData *mime) const;
};
