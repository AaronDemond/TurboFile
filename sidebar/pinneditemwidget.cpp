#include "pinneditemwidget.h"
#include "pincolorpresets.h"
#include "pinnedconstants.h"
#include "pinnedfoldericon.h"
#include "pinnedpath.h"

#include <QApplication>
#include <QColorDialog>
#include <QDrag>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QStyle>

// Visual pin row. Reports clicks, unpin, color, and internal drags.
// Persistence and list order stay in PinnedListWidget.

// Build a compact pin row: folder icon + directory name.
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
    // Full path is always available on hover even when the label is short.
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

    // Right-click is handled here so the item can offer Unpin and Color
    // without the list widget knowing about menus.
    connect(
        this,
        &QWidget::customContextMenuRequested,
        this,
        [this](const QPoint &position)
        {
            showContextMenu(mapToGlobal(position));
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

// Does not emit iconColorChanged. Callers that persist must emit themselves,
// or go through applyColorAction.
void PinnedItemWidget::setIconColor(const QColor &color)
{
    m_iconColor = color.isValid() ? color : QColor();
    applyAppearance();
}

QColor PinnedItemWidget::iconColor() const
{
    return m_iconColor;
}

void PinnedItemWidget::applyAppearance()
{
    available = isAvailableDirectory(m_path);
    nameLabel->setText(pinDisplayName(m_path));
    setToolTip(m_path);

    // Missing directories keep the warning icon. The stored tint is unchanged
    // and will apply again if the path becomes available.
    if (!available)
    {
        iconLabel->setPixmap(
            style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(16, 16)
        );
        nameLabel->setEnabled(false);
        return;
    }

    iconLabel->setPixmap(pinFolderIcon(m_iconColor, 16));
    nameLabel->setEnabled(true);
}

// Hover fill uses palette roles so the row follows the desktop theme.
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

void PinnedItemWidget::showContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    QAction *unpinAction = menu.addAction(tr("Unpin"));
    menu.addSeparator();
    addPinColorMenu(&menu, m_iconColor);

    QAction *chosen = menu.exec(globalPos);
    if (chosen == nullptr)
    {
        return;
    }

    if (chosen == unpinAction)
    {
        emit unpinRequested(m_path);
        return;
    }

    applyColorAction(chosen);
}

// Interpret a Color submenu action and persist through iconColorChanged.
void PinnedItemWidget::applyColorAction(QAction *action)
{
    const QString role =
        action->property("pinColorRole").toString();
    if (role.isEmpty())
    {
        return;
    }

    QColor chosen = m_iconColor;
    if (role == QLatin1String("default"))
    {
        chosen = QColor();
    }
    else if (role == QLatin1String("preset"))
    {
        chosen = action->data().value<QColor>();
    }
    else if (role == QLatin1String("custom"))
    {
        // Cancel leaves the previous color. An invalid dialog result is not
        // treated as Default.
        const QColor initial =
            m_iconColor.isValid() ? m_iconColor : Qt::blue;
        const QColor picked = QColorDialog::getColor(
            initial,
            this,
            tr("Folder color")
        );
        if (!picked.isValid())
        {
            return;
        }
        chosen = picked;
    }
    else
    {
        return;
    }

    setIconColor(chosen);
    emit iconColorChanged(m_path, m_iconColor);
}

// Internal reorder drag. The list widget prefers this MIME type over URLs.
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

// Start a drag only after the cursor has moved the platform drag distance,
// so a normal click still activates the pin.
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

        // Re-check on click so a remounted drive navigates instead of
        // remaining dimmed. A still-missing pin is removed silently.
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
