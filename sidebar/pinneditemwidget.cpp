#include "pinneditemwidget.h"
#include "pinnedconstants.h"
#include "pinnedpath.h"

#include <QApplication>
#include <QDrag>
#include <QEnterEvent>
#include <QFileIconProvider>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QStyle>
#include <QUrl>

PinnedItemWidget::PinnedItemWidget(
    const QString &path,
    QWidget *parent
)
    : QFrame(parent)
    , m_path(canonicalPinPath(path))
{
    setFrameShape(QFrame::NoFrame);
    setMinimumHeight(kPinRowHeight);
    setMaximumHeight(kPinRowHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAutoFillBackground(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setToolTip(m_path);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(6);

    iconLabel = new QLabel(this);
    iconLabel->setFixedSize(16, 16);
    iconLabel->setScaledContents(true);

    nameLabel = new QLabel(this);
    nameLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    nameLabel->setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Preferred
    );

    layout->addWidget(iconLabel);
    layout->addWidget(nameLabel, 1);

    connect(
        this,
        &QWidget::customContextMenuRequested,
        this,
        [this](const QPoint &position)
        {
            showUnpinMenu(mapToGlobal(position));
        }
    );

    applyAppearance();
    setHovered(false);
}

const QString &PinnedItemWidget::path() const
{
    return m_path;
}

void PinnedItemWidget::refreshAvailability()
{
    applyAppearance();
}

void PinnedItemWidget::applyAppearance()
{
    available = isAvailableDirectory(m_path);
    nameLabel->setText(pinDisplayName(m_path));

    if (available)
    {
        QFileIconProvider icons;
        iconLabel->setPixmap(
            icons.icon(QFileIconProvider::Folder).pixmap(16, 16)
        );
        nameLabel->setEnabled(true);
        setToolTip(m_path);
        return;
    }

    iconLabel->setPixmap(
        style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(16, 16)
    );
    nameLabel->setEnabled(false);
    setToolTip(m_path);
}

void PinnedItemWidget::setHovered(bool hovered)
{
    QPalette pal = palette();
    if (hovered)
    {
        pal.setColor(QPalette::Window, pal.color(QPalette::Midlight));
    }
    else
    {
        pal.setColor(QPalette::Window, pal.color(QPalette::Base));
    }
    setPalette(pal);
}

void PinnedItemWidget::showUnpinMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    QAction *unpinAction = menu.addAction(tr("Unpin"));
    if (menu.exec(globalPos) == unpinAction)
    {
        emit unpinRequested(m_path);
    }
}

void PinnedItemWidget::startDrag()
{
    auto *mime = new QMimeData;
    mime->setData(kPinnedDirectoryMime, m_path.toUtf8());

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(grab());
    drag->exec(Qt::MoveAction);
}

void PinnedItemWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        dragStartPosition = event->pos();
        pressed = true;
    }

    QFrame::mousePressEvent(event);
}

void PinnedItemWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!pressed || !(event->buttons() & Qt::LeftButton))
    {
        QFrame::mouseMoveEvent(event);
        return;
    }

    if ((event->pos() - dragStartPosition).manhattanLength() <
        QApplication::startDragDistance())
    {
        QFrame::mouseMoveEvent(event);
        return;
    }

    pressed = false;
    startDrag();
}

void PinnedItemWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && pressed)
    {
        pressed = false;

        if (isAvailableDirectory(m_path))
        {
            applyAppearance();
            emit activated(m_path);
        }
        else
        {
            emit unpinRequested(m_path);
        }
    }

    QFrame::mouseReleaseEvent(event);
}

void PinnedItemWidget::enterEvent(QEnterEvent *event)
{
    setHovered(true);
    QFrame::enterEvent(event);
}

void PinnedItemWidget::leaveEvent(QEvent *event)
{
    setHovered(false);
    QFrame::leaveEvent(event);
}
