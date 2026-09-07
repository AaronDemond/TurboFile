#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QWidget>

class BrowserFileTreeView;
class BrowserFileListView;
class BrowserLineEdit;
class DirectorySizeSortProxyModel;
class QAbstractItemView;
class QModelIndex;
class QPushButton;
class QEvent;
class QStackedWidget;
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
    // Details uses the existing column view. SmallIcons and BigIcons share one
    // grid widget and differ only in icon and cell geometry.
    enum class FileViewMode
    {
        Details,
        SmallIcons,
        BigIcons
    };
    Q_ENUM(FileViewMode)

    // fileModel is borrowed. MainWindow owns the shared proxy for the
    // whole window so every pane sees one cache and one worker pool.
    explicit BrowserPane(
        DirectorySizeSortProxyModel *fileModel,
        QWidget *parent = nullptr
    );

    QString currentPath() const;
    QTreeView *fileTreeView() const;
    QAbstractItemView *activeFileView() const;
    QList<QAbstractItemView *> fileViews() const;
    FileViewMode fileViewMode() const;
    QString searchText() const;
    int regularFileCount() const;
    QStringList history() const;
    int historyIndex() const;

    // Switch the visible file presentation without changing the current
    // directory, model, or selected filesystem rows.
    void setFileViewMode(FileViewMode mode);

    // Both views share the tree's QItemSelectionModel after MainWindow has
    // attached and configured the details model.
    void synchronizeFileViewSelection();

    // Filter direct children of this pane's current directory by file name.
    // The text belongs to the pane so split panes can filter independently.
    void setSearchText(const QString &text);

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
    void fileViewModeChanged(FileViewMode mode);

    // The direct regular-file count changed because this pane navigated or
    // its current directory gained, lost, or changed filesystem rows.
    void regularFileCountChanged(int count);

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

    // Apply the current case-insensitive name filter to both presentations.
    // Rows are hidden per view rather than removed from the shared proxy, so
    // other tabs and the neighboring split pane remain unchanged.
    void applySearchFilter();

    // Count direct regular-file children under the current root. Directories
    // and symbolic links are intentionally excluded from the status total.
    void updateRegularFileCount();

    void handleFilesDropped(
        const QList<QUrl> &urls,
        const QModelIndex &hoverIndex
    );

    // Borrowed shared proxy. Not owned by this pane.
    DirectorySizeSortProxyModel *fileModel = nullptr;

    BrowserFileTreeView *treeView = nullptr;
    BrowserFileListView *iconView = nullptr;
    QStackedWidget *fileViewStack = nullptr;
    FileViewMode currentFileViewMode = FileViewMode::Details;
    QString currentSearchText;
    int currentRegularFileCount = 0;
    BrowserLineEdit *pathLineEdit = nullptr;
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
