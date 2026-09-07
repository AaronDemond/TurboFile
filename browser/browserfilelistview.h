#pragma once

#include <QListView>
#include <QList>
#include <QUrl>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;

// Grid-oriented companion to BrowserFileTreeView.
//
// The list view displays only the filesystem model's Name column in IconMode.
// It accepts the same local-file drops as the details tree and delegates the
// actual copy operation to MainWindow instead of allowing QFileSystemModel to
// perform an implicit move.
class BrowserFileListView : public QListView
{
    Q_OBJECT

public:
    explicit BrowserFileListView(QWidget *parent = nullptr);

signals:
    // hoverIndex identifies the file or directory under the drop cursor. An
    // invalid index means the files were dropped on empty grid space.
    void filesDropped(
        const QList<QUrl> &urls,
        const QModelIndex &hoverIndex
    );

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    // Restrict drops to local filesystem URLs. Sidebar-reorder MIME and remote
    // URLs are deliberately rejected before reaching the shared copy path.
    bool canAcceptDrop(const QMimeData *mimeData) const;
};