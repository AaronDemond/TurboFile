#include "pinnedsidebar.h"
#include "pinnedconstants.h"
#include "pinnedlistwidget.h"

#include <QFont>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

PinnedSidebar::PinnedSidebar(QWidget *parent)
    : QFrame(parent)
{
    setMinimumWidth(kSidebarMinWidth);
    setMaximumWidth(kSidebarMaxWidth);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setFrameShape(QFrame::StyledPanel);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *title = new QLabel(tr("Pinned"), this);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Expanding
    );

    pinnedList = new PinnedListWidget(scrollArea);
    scrollArea->setWidget(pinnedList);

    layout->addWidget(title);
    layout->addWidget(scrollArea, 1);

    connect(
        pinnedList,
        &PinnedListWidget::directoryActivated,
        this,
        &PinnedSidebar::directoryActivated
    );
}

QSize PinnedSidebar::sizeHint() const
{
    return QSize(kSidebarHintWidth, QFrame::sizeHint().height());
}

void PinnedSidebar::pinDirectory(const QString &path)
{
    pinnedList->addPinnedDirectory(path);
}

bool PinnedSidebar::isPinned(const QString &path) const
{
    return pinnedList->containsPath(path);
}
