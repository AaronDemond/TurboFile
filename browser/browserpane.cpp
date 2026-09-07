#include "browserpane.h"
#include "browserfilelistview.h"
#include "browserfiletreeview.h"
#include "browserlineedit.h"
#include "directorysizesortproxymodel.h"

#include <QAbstractItemView>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QModelIndex>
#include <QPushButton>
#include <QItemSelectionModel>
#include <QSize>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

// Builds the pane's chrome and tree. Navigation happens later through
// navigateTo() / restoreSession() once MainWindow has stored this pane
// in TabState.
BrowserPane::BrowserPane(
    DirectorySizeSortProxyModel *fileModel,
    QWidget *parent
)
    : QWidget(parent)
    , fileModel(fileModel)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(4);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);

    backButton = new QPushButton(QStringLiteral("<"), this);
    forwardButton = new QPushButton(QStringLiteral(">"), this);
    upButton = new QPushButton(QStringLiteral("Up"), this);
    // Path and search fields use the same concrete control class so their
    // native palette, font, frame, and state styling cannot diverge.
    pathLineEdit = new BrowserLineEdit(this);
    splitButton = new QPushButton(QStringLiteral("Split"), this);
    closeButton = new QPushButton(QStringLiteral("X"), this);

    backButton->setFixedSize(32, 28);
    forwardButton->setFixedSize(32, 28);
    upButton->setFixedSize(50, 28);
    splitButton->setFixedSize(50, 28);
    closeButton->setFixedSize(28, 28);

    // All toolbar buttons intentionally receive no stylesheet. Qt's active
    // platform style owns their normal, hover, pressed, focused, and disabled
    // appearance so every control matches the native widget library.

    splitButton->setToolTip(QStringLiteral("Split into two panes"));
    closeButton->setToolTip(QStringLiteral("Close this pane"));
    // A tab always keeps one pane. Close is shown only after a split.
    closeButton->hide();

    // The stack keeps both presentations alive. Maintaining one instance of
    // each avoids rebuilding models, selection, and scroll widgets whenever
    // the user changes view mode.
    fileViewStack = new QStackedWidget(this);
    treeView = new BrowserFileTreeView(fileViewStack);
    iconView = new BrowserFileListView(fileViewStack);

    // QListView renders only one model column. Column 0 supplies the file name
    // and QFileSystemModel decoration icon needed by the grid.
    iconView->setModel(fileModel);
    iconView->setModelColumn(0);
    iconView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    iconView->setContextMenuPolicy(Qt::CustomContextMenu);

    fileViewStack->addWidget(treeView);
    fileViewStack->addWidget(iconView);
    fileViewStack->setCurrentWidget(treeView);

    toolbar->addWidget(backButton);
    toolbar->addWidget(forwardButton);
    toolbar->addWidget(upButton);
    toolbar->addWidget(pathLineEdit);
    toolbar->addWidget(splitButton);
    toolbar->addWidget(closeButton);

    rootLayout->addLayout(toolbar);
    rootLayout->addWidget(fileViewStack);

    // Buttons and the path bar belong to this pane, so switching either tab
    // group never causes one BrowserPane to consult another tab's history.
    connect(backButton, &QPushButton::clicked, this, &BrowserPane::goBack);
    connect(forwardButton, &QPushButton::clicked, this, &BrowserPane::goForward);
    connect(upButton, &QPushButton::clicked, this, &BrowserPane::goUp);
    connect(pathLineEdit, &QLineEdit::returnPressed, this, &BrowserPane::navigateFromPathBar);
    connect(splitButton, &QPushButton::clicked, this, &BrowserPane::splitRequested);
    connect(closeButton, &QPushButton::clicked, this, &BrowserPane::closeRequested);

    // pressed fires for left and right clicks before the context menu,
    // so file actions and pin navigation target the pane the user used.
    connect(
        treeView,
        &QAbstractItemView::pressed,
        this,
        &BrowserPane::activated
    );
    connect(
        treeView,
        &BrowserFileTreeView::filesDropped,
        this,
        &BrowserPane::handleFilesDropped
    );
    connect(
        iconView,
        &QAbstractItemView::pressed,
        this,
        &BrowserPane::activated
    );
    connect(
        iconView,
        &BrowserFileListView::filesDropped,
        this,
        &BrowserPane::handleFilesDropped
    );

    // Focus on the path bar should also mark this pane active, otherwise
    // Enter would navigate the last clicked pane instead of this one.
    pathLineEdit->installEventFilter(this);
    backButton->installEventFilter(this);
    forwardButton->installEventFilter(this);
    upButton->installEventFilter(this);
    treeView->installEventFilter(this);
    iconView->installEventFilter(this);

    // QFileSystemModel discovers directory children asynchronously. Reapply a
    // non-empty filter whenever this pane's current root receives new rows so
    // late arrivals cannot briefly remain visible despite the search text.
    connect(
        fileModel,
        &QAbstractItemModel::rowsInserted,
        this,
        [this](const QModelIndex &parent, int, int)
        {
            if (parent != treeView->rootIndex())
            {
                return;
            }

            // QFileSystemModel supplies directory rows asynchronously. Keep
            // filtering and the visible status count synchronized as each
            // batch of direct children arrives.
            if (!currentSearchText.isEmpty())
            {
                applySearchFilter();
            }

            updateRegularFileCount();
        }
    );

    // Removing a direct child can change the count without inserting a
    // replacement row, so recompute after the model completes the removal.
    connect(
        fileModel,
        &QAbstractItemModel::rowsRemoved,
        this,
        [this](const QModelIndex &parent, int, int)
        {
            if (parent == treeView->rootIndex())
            {
                updateRegularFileCount();
            }
        }
    );

    // Sorting changes row positions. Row-hidden state is view-local and keyed
    // by row, so recompute it after a model layout change to keep each hidden
    // flag attached to the correct filesystem item.
    connect(
        fileModel,
        &QAbstractItemModel::layoutChanged,
        this,
        [this]()
        {
            if (!currentSearchText.isEmpty())
            {
                applySearchFilter();
            }
        }
    );

    // A rename can make one direct child begin or stop matching without
    // inserting or removing a row. Only Name-column changes need a refilter.
    connect(
        fileModel,
        &QAbstractItemModel::dataChanged,
        this,
        [this](
            const QModelIndex &topLeft,
            const QModelIndex &bottomRight,
            const QList<int> &
        )
        {
            if (
                topLeft.parent() == treeView->rootIndex() &&
                topLeft.column() <= 0 &&
                bottomRight.column() >= 0
            )
            {
                if (!currentSearchText.isEmpty())
                {
                    applySearchFilter();
                }

                // A filesystem item can be replaced at the same name or have
                // its metadata refreshed, so re-check regular-file status.
                updateRegularFileCount();
            }
        }
    );

    // A full source-model reset invalidates every root's existing child set.
    // The count first returns to zero and later grows through rowsInserted as
    // QFileSystemModel repopulates this pane's current directory.
    connect(
        fileModel,
        &QAbstractItemModel::modelReset,
        this,
        &BrowserPane::updateRegularFileCount
    );

    updateNavigationButtons();
}

QString BrowserPane::currentPath() const
{
    // The tree root is the directory this pane is browsing. filePath()
    // maps the proxy index back to a real filesystem path.
    return fileModel->filePath(treeView->rootIndex());
}

QTreeView *BrowserPane::fileTreeView() const
{
    return treeView;
}

QAbstractItemView *BrowserPane::activeFileView() const
{
    // Both stack pages derive from QAbstractItemView, so callers can perform
    // selection and context-menu operations without depending on presentation.
    return qobject_cast<QAbstractItemView *>(
        fileViewStack->currentWidget()
    );
}

QList<QAbstractItemView *> BrowserPane::fileViews() const
{
    return {treeView, iconView};
}

BrowserPane::FileViewMode BrowserPane::fileViewMode() const
{
    return currentFileViewMode;
}

QString BrowserPane::searchText() const
{
    return currentSearchText;
}

int BrowserPane::regularFileCount() const
{
    return currentRegularFileCount;
}

void BrowserPane::setFileViewMode(FileViewMode mode)
{
    currentFileViewMode = mode;

    if (mode == FileViewMode::Details)
    {
        fileViewStack->setCurrentWidget(treeView);
    }
    else
    {
        // Small and Big share the same list. Changing geometry before showing
        // it lets QListView recalculate wrapping for the current pane width.
        const bool useBigIcons =
            mode == FileViewMode::BigIcons;

        iconView->setIconSize(
            useBigIcons ? QSize(64, 64) : QSize(24, 24)
        );
        iconView->setGridSize(
            useBigIcons ? QSize(140, 104) : QSize(120, 56)
        );
        iconView->setSpacing(useBigIcons ? 8 : 4);
        fileViewStack->setCurrentWidget(iconView);
    }

    if (QAbstractItemView *view = activeFileView())
    {
        view->setFocus(Qt::OtherFocusReason);
    }

    emit fileViewModeChanged(mode);
}

void BrowserPane::synchronizeFileViewSelection()
{
    // setModel() creates the details tree's selection model. Reusing that
    // exact object means selecting in either presentation immediately updates
    // the hidden one, so actions and mode switches retain the same selection.
    if (treeView->selectionModel() != nullptr)
    {
        iconView->setSelectionModel(
            treeView->selectionModel()
        );
    }
}

void BrowserPane::setSearchText(const QString &text)
{
    if (currentSearchText == text)
    {
        return;
    }

    // Preserve the user's spelling for the bottom search field. Matching is
    // case-insensitive inside applySearchFilter(), so no normalized copy is
    // needed and switching panes can restore exactly what the user entered.
    currentSearchText = text;
    applySearchFilter();
}

void BrowserPane::applySearchFilter()
{
    const QModelIndex rootIndex =
        treeView->rootIndex();
    const int rowCount =
        fileModel->rowCount(rootIndex);
    QItemSelectionModel *selectionModel =
        treeView->selectionModel();

    // Both views show the same source rows under the same root. QTreeView's
    // API includes the parent index, while QListView interprets each row under
    // its configured root index.
    for (int row = 0; row < rowCount; ++row)
    {
        const QModelIndex nameIndex =
            fileModel->index(row, 0, rootIndex);
        const QString fileName =
            fileModel->data(
                nameIndex,
                Qt::DisplayRole
            ).toString();
        const bool matches =
            currentSearchText.isEmpty() ||
            fileName.contains(
                currentSearchText,
                Qt::CaseInsensitive
            );

        treeView->setRowHidden(
            row,
            rootIndex,
            !matches
        );
        iconView->setRowHidden(row, !matches);

        // Hidden selections are dangerous because a later context-menu action
        // could otherwise operate on an item the user can no longer see. The
        // two views share this selection model, so one deselect updates both.
        if (!matches && selectionModel != nullptr)
        {
            selectionModel->select(
                nameIndex,
                QItemSelectionModel::Deselect |
                    QItemSelectionModel::Rows
            );
        }
    }
}

void BrowserPane::updateRegularFileCount()
{
    const QModelIndex rootIndex =
        treeView->rootIndex();
    const int rowCount =
        fileModel->rowCount(rootIndex);
    int fileCount = 0;

    // Count only direct children because the status text describes the open
    // directory, not its complete recursive subtree. The proxy maps each row
    // to QFileSystemModel metadata and excludes both directories and links.
    for (int row = 0; row < rowCount; ++row)
    {
        const QModelIndex index =
            fileModel->index(row, 0, rootIndex);

        if (fileModel->isRegularFile(index))
        {
            ++fileCount;
        }
    }

    if (currentRegularFileCount == fileCount)
    {
        return;
    }

    currentRegularFileCount = fileCount;
    emit regularFileCountChanged(currentRegularFileCount);
}

QStringList BrowserPane::history() const
{
    return m_history;
}

int BrowserPane::historyIndex() const
{
    return m_historyIndex;
}

void BrowserPane::setSplitButtonVisible(bool visible)
{
    splitButton->setVisible(visible);
}

void BrowserPane::setCloseButtonVisible(bool visible)
{
    closeButton->setVisible(visible);
}

bool BrowserPane::eventFilter(QObject *watched, QEvent *event)
{
    // Mouse press or keyboard focus on chrome means the user is working
    // in this pane. MainWindow then routes pin clicks and file actions here.
    if (
        event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::FocusIn
    )
    {
        emit activated();
    }

    return QWidget::eventFilter(watched, event);
}

// Drop on a directory copies into that directory. Drop on a file or empty
// space copies into this pane's current root. The actual copy lives in
// MainWindow so it can reuse paste's recursion and cache invalidation.
void BrowserPane::handleFilesDropped(
    const QList<QUrl> &urls,
    const QModelIndex &hoverIndex
)
{
    QString destination = currentPath();

    if (hoverIndex.isValid() && fileModel->isDir(hoverIndex))
    {
        destination = fileModel->filePath(hoverIndex);
    }

    if (destination.isEmpty())
    {
        return;
    }

    emit filesDropped(urls, destination);
}

void BrowserPane::navigateFromPathBar()
{
    QString enteredPath = pathLineEdit->text().trimmed();
    const QString current = currentPath();

    if (enteredPath.isEmpty())
    {
        pathLineEdit->setText(current);
        return;
    }

    // '~' and '~/...' expand to the user's home, matching the old tab bar.
    if (enteredPath == QLatin1String("~"))
    {
        enteredPath = QDir::homePath();
    }
    else if (enteredPath.startsWith(QLatin1String("~/")))
    {
        enteredPath = QDir::homePath() + enteredPath.mid(1);
    }

    if (QDir::isRelativePath(enteredPath))
    {
        enteredPath = QDir(current).absoluteFilePath(enteredPath);
    }

    const QFileInfo pathInfo(enteredPath);
    if (!pathInfo.exists() || !pathInfo.isDir())
    {
        pathLineEdit->setText(current);
        return;
    }

    navigateTo(pathInfo.absoluteFilePath());
}

void BrowserPane::goUp()
{
    const QModelIndex currentIndex = treeView->rootIndex();
    const QModelIndex parentIndex = fileModel->parent(currentIndex);

    // Filesystem root has no parent index. Stay put rather than navigating
    // to an empty path.
    if (!parentIndex.isValid())
    {
        return;
    }

    navigateTo(fileModel->filePath(parentIndex));
}

void BrowserPane::goBack()
{
    if (m_historyIndex <= 0)
    {
        return;
    }

    --m_historyIndex;
    navigateTo(m_history.at(m_historyIndex), false);
}

void BrowserPane::goForward()
{
    if (m_historyIndex >= m_history.size() - 1)
    {
        return;
    }

    ++m_historyIndex;
    navigateTo(m_history.at(m_historyIndex), false);
}

void BrowserPane::navigateTo(const QString &path, bool addToHistory)
{
    const QString cleanPath = QDir::cleanPath(path);
    const QModelIndex newIndex = fileModel->index(cleanPath);

    if (!newIndex.isValid())
    {
        return;
    }

    if (addToHistory)
    {
        // Drop the old Forward branch. After A -> B -> C, Back to B, then
        // navigate to D, C must not remain reachable via Forward.
        while (m_history.size() > m_historyIndex + 1)
        {
            m_history.removeLast();
        }

        if (m_history.isEmpty() || m_history.last() != cleanPath)
        {
            m_history.append(cleanPath);
            m_historyIndex = m_history.size() - 1;
        }
    }

    // Both views must always represent the same directory. Their shared
    // selection model then refers to indexes under an identical root.
    treeView->setRootIndex(newIndex);
    iconView->setRootIndex(newIndex);
    pathLineEdit->setText(cleanPath);

    // A pane-local search remains active when navigating. Apply it to the
    // newly selected root immediately; rows loaded later are handled by the
    // rowsInserted connection established in the constructor.
    applySearchFilter();
    updateRegularFileCount();

    updateNavigationButtons();
    emit pathChanged(cleanPath);
}

void BrowserPane::restoreSession(
    const QString &path,
    const QStringList &history,
    int historyIndex
)
{
    // Apply the saved list first so navigateTo(..., false) does not invent
    // a one-entry history that would replace the user's stack.
    m_history = history;
    m_historyIndex = historyIndex;

    QString target = path;
    if (target.isEmpty() || !QFileInfo(target).isDir())
    {
        target = QDir::homePath();
    }

    navigateTo(target, m_history.isEmpty());

    // navigateTo may have clamped or rewritten history. If the saved
    // stack was valid, keep the saved index within bounds.
    if (!history.isEmpty())
    {
        m_history = history;
        m_historyIndex = qBound(0, historyIndex, m_history.size() - 1);
        updateNavigationButtons();
    }
}

void BrowserPane::updateNavigationButtons()
{
    backButton->setEnabled(m_historyIndex > 0);
    forwardButton->setEnabled(
        m_historyIndex >= 0 &&
        m_historyIndex < m_history.size() - 1
    );
}
