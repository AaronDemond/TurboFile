#include "browserpane.h"
#include "browserfiletreeview.h"
#include "directorysizesortproxymodel.h"

#include <QAbstractItemView>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QModelIndex>
#include <QPushButton>
#include <QSizePolicy>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Same chrome as the original tab navigation buttons so a split pane
// still reads as part of TurboFile rather than a second widget style.
const char kPaneButtonStyle[] =
    "QPushButton { background-color: orange; color: black; border: 1px solid black; }"
    "QPushButton:hover { background-color: green; }"
    "QPushButton:pressed { background-color: red }"
    "QPushButton:disabled {background-color: grey; color: black; border: 1px solid black}";

} // namespace

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
    pathLineEdit = new QLineEdit(this);
    splitButton = new QPushButton(QStringLiteral("Split"), this);
    closeButton = new QPushButton(QStringLiteral("X"), this);

    backButton->setFixedSize(32, 28);
    forwardButton->setFixedSize(32, 28);
    upButton->setFixedSize(50, 28);
    splitButton->setFixedSize(50, 28);
    closeButton->setFixedSize(28, 28);

    backButton->setStyleSheet(QLatin1String(kPaneButtonStyle));
    forwardButton->setStyleSheet(QLatin1String(kPaneButtonStyle));
    upButton->setStyleSheet(QLatin1String(kPaneButtonStyle));
    splitButton->setStyleSheet(QLatin1String(kPaneButtonStyle));
    closeButton->setStyleSheet(QLatin1String(kPaneButtonStyle));

    splitButton->setToolTip(QStringLiteral("Split into two panes"));
    closeButton->setToolTip(QStringLiteral("Close this pane"));
    // A tab always keeps one pane. Close is shown only after a split.
    closeButton->hide();

    treeView = new BrowserFileTreeView(this);

    toolbar->addWidget(backButton);
    toolbar->addWidget(forwardButton);
    toolbar->addWidget(upButton);
    toolbar->addWidget(pathLineEdit);
    toolbar->addWidget(splitButton);
    toolbar->addWidget(closeButton);

    rootLayout->addLayout(toolbar);
    rootLayout->addWidget(treeView);

    // Buttons and the path bar belong to this pane, so their slots never
    // consult another pane's history. That is the whole point of the split.
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

    // Focus on the path bar should also mark this pane active, otherwise
    // Enter would navigate the last clicked pane instead of this one.
    pathLineEdit->installEventFilter(this);
    backButton->installEventFilter(this);
    forwardButton->installEventFilter(this);
    upButton->installEventFilter(this);

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

    treeView->setRootIndex(newIndex);
    pathLineEdit->setText(cleanPath);
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
