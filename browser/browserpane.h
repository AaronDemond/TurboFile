#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QWidget>

class BrowserFileTreeView;
class DirectorySizeSortProxyModel;
class QLineEdit;
class QModelIndex;
class QPushButton;
class QEvent;
class QTreeView;

// One independent file explorer inside one tab of a pane group.
//
// Owns its navigation bar, tree, and browsing history. A QTabWidget group may
// host several BrowserPane pages, and the workspace may place two groups side
// by side. All panes share the window's filesystem model but never share
// history or root indexes. The pane reports activation, split/close requests,
// path changes, and file drops; MainWindow owns structural changes.
class BrowserPane : public QWidget
{
    Q_OBJECT

public:
    // fileModel is borrowed. MainWindow owns the shared proxy for the
    // whole window so every pane sees one cache and one worker pool.
    explicit BrowserPane(
        DirectorySizeSortProxyModel *fileModel,
        QWidget *parent = nullptr
    );

    QString currentPath() const;
    QTreeView *fileTreeView() const;
    QStringList history() const;
    int historyIndex() const;

    // Change this pane's root directory. addToHistory is false for Back
    // and Forward so those moves reuse existing history entries.
    void navigateTo(
        const QString &path,
        bool addToHistory = true
    );

    // Reload a saved pane without appending a duplicate history entry.
    void restoreSession(
        const QString &path,
        const QStringList &history,
        int historyIndex
    );

    void setSplitButtonVisible(bool visible);
    void setCloseButtonVisible(bool visible);

signals:
    // The user interacted with this pane; MainWindow should mark its tab group
    // active so Ctrl+T, pins, and actions remain on the correct side.
    void activated();
    void splitRequested();
    void closeRequested();
    void pathChanged(const QString &path);
    // Destination is already a local directory path, not a model index.
    void filesDropped(
        const QList<QUrl> &urls,
        const QString &destinationDirectory
    );

public slots:
    void goUp();
    void goBack();
    void goForward();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void navigateFromPathBar();
    void updateNavigationButtons();
    void handleFilesDropped(
        const QList<QUrl> &urls,
        const QModelIndex &hoverIndex
    );

    // Borrowed shared proxy. Not owned by this pane.
    DirectorySizeSortProxyModel *fileModel = nullptr;

    BrowserFileTreeView *treeView = nullptr;
    QLineEdit *pathLineEdit = nullptr;
    QPushButton *backButton = nullptr;
    QPushButton *forwardButton = nullptr;
    QPushButton *upButton = nullptr;
    QPushButton *splitButton = nullptr;
    QPushButton *closeButton = nullptr;

    // Ordered visit list for this pane only. historyIndex is the current
    // position; -1 means nothing has been navigated yet.
    QStringList m_history;
    int m_historyIndex = -1;
};
