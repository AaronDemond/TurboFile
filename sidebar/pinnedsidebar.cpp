#include "pinnedsidebar.h"
#include "pinnedconstants.h"
#include "pinnedlistwidget.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

// Outer sidebar frame. Forwards activation and exposes collapse to MainWindow.

// Expanded layout: title + collapse button, then the scrolling pin list.
// Collapsed layout: a thin strip with an expand chevron.
PinnedSidebar::PinnedSidebar(QWidget *parent)
    : QFrame(parent)
{
    // Horizontal Ignored lets QSplitter own the pane width. Preferred plus
    // sizeHint(180) would fight every handle movement and make dragging lag.
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    setFrameShape(QFrame::StyledPanel);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    headerWidget = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(4);

    titleLabel = new QLabel(tr("Pinned"), headerWidget);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);

    // « hides the list and leaves only the expand strip.
    collapseButton = new QToolButton(headerWidget);
    collapseButton->setAutoRaise(true);
    collapseButton->setFocusPolicy(Qt::NoFocus);
    collapseButton->setText(QStringLiteral("«"));
    collapseButton->setToolTip(tr("Collapse sidebar"));

    headerLayout->addWidget(titleLabel, 1);
    headerLayout->addWidget(collapseButton);

    // The list lives in a scroll area so many pins do not widen the sidebar.
    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Expanding
    );

    pinnedList = new PinnedListWidget(scrollArea);
    scrollArea->setWidget(pinnedList);

    // Visible only while collapsed. MainWindow sizes the splitter to
    // kSidebarCollapsedWidth when this strip is shown.
    collapsedStrip = new QWidget(this);
    auto *stripLayout = new QVBoxLayout(collapsedStrip);
    stripLayout->setContentsMargins(0, 4, 0, 4);
    stripLayout->setSpacing(0);

    // » restores the last expanded splitter width.
    expandButton = new QToolButton(collapsedStrip);
    expandButton->setAutoRaise(true);
    expandButton->setFocusPolicy(Qt::NoFocus);
    expandButton->setText(QStringLiteral("»"));
    expandButton->setToolTip(tr("Expand sidebar"));
    expandButton->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Fixed
    );

    stripLayout->addWidget(expandButton, 0, Qt::AlignHCenter);
    stripLayout->addStretch();
    collapsedStrip->hide();

    layout->addWidget(headerWidget);
    layout->addWidget(scrollArea, 1);
    layout->addWidget(collapsedStrip, 1);

    connect(
        pinnedList,
        &PinnedListWidget::directoryActivated,
        this,
        &PinnedSidebar::directoryActivated
    );
    connect(
        collapseButton,
        &QToolButton::clicked,
        this,
        [this]()
        {
            setCollapsed(true);
        }
    );
    connect(
        expandButton,
        &QToolButton::clicked,
        this,
        [this]()
        {
            setCollapsed(false);
        }
    );
}

QSize PinnedSidebar::sizeHint() const
{
    if (collapsed)
    {
        return QSize(kSidebarCollapsedWidth, QFrame::sizeHint().height());
    }

    // First-run / fallback width only. Because the horizontal policy is
    // Ignored, QSplitter does not keep snapping back to this hint mid-drag.
    return QSize(kSidebarHintWidth, QFrame::sizeHint().height());
}

// Expanded: no minimum width so the splitter can be dragged freely.
// Collapsed: the strip must stay wide enough for the chevron.
QSize PinnedSidebar::minimumSizeHint() const
{
    if (collapsed)
    {
        return QSize(kSidebarCollapsedWidth, QFrame::minimumSizeHint().height());
    }

    return QSize(0, QFrame::minimumSizeHint().height());
}

void PinnedSidebar::pinDirectory(const QString &path)
{
    pinnedList->addPinnedDirectory(path);
}

bool PinnedSidebar::isPinned(const QString &path) const
{
    return pinnedList->containsPath(path);
}

bool PinnedSidebar::isCollapsed() const
{
    return collapsed;
}

void PinnedSidebar::setCollapsed(bool collapsedState)
{
    if (collapsed == collapsedState)
    {
        return;
    }

    collapsed = collapsedState;
    applyCollapsedState();
    emit collapsedChanged(collapsed);
}

void PinnedSidebar::applyCollapsedState()
{
    headerWidget->setVisible(!collapsed);
    scrollArea->setVisible(!collapsed);
    collapsedStrip->setVisible(collapsed);

    auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
    if (layout != nullptr)
    {
        const int margin = collapsed ? 2 : 8;
        layout->setContentsMargins(margin, margin, margin, margin);
    }

    // Collapse locks width to the strip. Expand clears min/max and returns
    // to Ignored so the splitter can resize without the frame resisting.
    if (collapsed)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setFixedWidth(kSidebarCollapsedWidth);
    }
    else
    {
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    }

    updateGeometry();
}
