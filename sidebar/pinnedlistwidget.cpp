#include "pinnedlistwidget.h"
#include "pinnedconstants.h"
#include "pinneditemwidget.h"
#include "pinnedpath.h"

#include <QCursor>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEasingCurve>
#include <QMimeData>
#include <QPropertyAnimation>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const QString kSettingsOrganization = QStringLiteral("TurboFile");
const QString kSettingsApplication = QStringLiteral("TurboFile");
const QString kPinnedDirectoriesKey =
    QStringLiteral("sidebar/pinnedDirectories");

} // namespace

PinnedListWidget::PinnedListWidget(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);

    pinsLayout = new QVBoxLayout(this);
    pinsLayout->setContentsMargins(0, 0, 0, 0);
    pinsLayout->setSpacing(0);
    pinsLayout->addStretch();

    dropPlaceholder = new QWidget(this);
    dropPlaceholder->setAttribute(Qt::WA_TransparentForMouseEvents);
    dropPlaceholder->setMinimumHeight(0);
    dropPlaceholder->setMaximumHeight(0);
    dropPlaceholder->hide();
    dropPlaceholder->setStyleSheet(
        QStringLiteral(
            "background-color: palette(highlight);"
            "margin: 2px 4px;"
        )
    );

    placeholderAnimation =
        new QPropertyAnimation(dropPlaceholder, "maximumHeight", this);
    placeholderAnimation->setDuration(kPinAnimationMs);
    placeholderAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(
        placeholderAnimation,
        &QPropertyAnimation::valueChanged,
        this,
        [this](const QVariant &value)
        {
            dropPlaceholder->setMinimumHeight(value.toInt());
        }
    );

    connect(
        placeholderAnimation,
        &QPropertyAnimation::finished,
        this,
        [this]()
        {
            if (dropPlaceholder->maximumHeight() == 0)
            {
                pinsLayout->removeWidget(dropPlaceholder);
                dropPlaceholder->hide();
            }
        }
    );

    loadPins();
}

void PinnedListWidget::addPinnedDirectory(
    const QString &path,
    int index
)
{
    const QString clean = canonicalPinPath(path);
    if (clean.isEmpty())
    {
        return;
    }

    const int existing = pinnedPaths.indexOf(clean);
    if (existing >= 0)
    {
        if (index < 0)
        {
            return;
        }

        reorderPin(existing, index);
        return;
    }

    int insertAt = index;
    if (insertAt < 0 || insertAt > pinnedPaths.size())
    {
        insertAt = pinnedPaths.size();
    }

    insertPinWidget(clean, insertAt);
    savePins();
}

void PinnedListWidget::removePinnedDirectory(const QString &path)
{
    const QString clean = canonicalPinPath(path);
    const int index = pinnedPaths.indexOf(clean);
    if (index < 0)
    {
        return;
    }

    pinnedPaths.removeAt(index);
    PinnedItemWidget *item = pinWidgets.takeAt(index);
    savePins();
    animateRemoval(item);
}

bool PinnedListWidget::containsPath(const QString &path) const
{
    return pinnedPaths.contains(canonicalPinPath(path));
}

PinnedListWidget::DropKind PinnedListWidget::classifyDrop(
    const QMimeData *mime,
    QString *outPath
) const
{
    if (mime == nullptr)
    {
        return DropKind::None;
    }

    if (mime->hasFormat(kPinnedDirectoryMime))
    {
        const QString path = canonicalPinPath(
            QString::fromUtf8(mime->data(kPinnedDirectoryMime))
        );
        if (path.isEmpty())
        {
            return DropKind::None;
        }

        if (outPath != nullptr)
        {
            *outPath = path;
        }
        return DropKind::InternalPin;
    }

    if (!mime->hasUrls())
    {
        return DropKind::None;
    }

    const QList<QUrl> urls = mime->urls();
    if (urls.size() != 1)
    {
        return DropKind::None;
    }

    const QUrl &url = urls.first();
    if (!url.isLocalFile())
    {
        return DropKind::None;
    }

    const QString localPath = url.toLocalFile();
    if (!isAvailableDirectory(localPath))
    {
        return DropKind::None;
    }

    if (outPath != nullptr)
    {
        *outPath = canonicalPinPath(localPath);
    }
    return DropKind::ExternalDirectory;
}

int PinnedListWidget::insertionIndexForY(int y) const
{
    int index = 0;
    for (const PinnedItemWidget *item : pinWidgets)
    {
        const QRect geometry = item->geometry();
        const int midpoint = geometry.top() + geometry.height() / 2;
        if (y < midpoint)
        {
            return index;
        }
        ++index;
    }

    return pinWidgets.size();
}

void PinnedListWidget::showDropPlaceholder(int index)
{
    const int currentLayoutIndex = pinsLayout->indexOf(dropPlaceholder);
    if (currentLayoutIndex != index)
    {
        if (currentLayoutIndex >= 0)
        {
            pinsLayout->removeWidget(dropPlaceholder);
        }
        pinsLayout->insertWidget(index, dropPlaceholder);
    }

    dropPlaceholder->show();

    if (dropPlaceholder->maximumHeight() >= kPlaceholderEndHeight)
    {
        return;
    }

    placeholderAnimation->stop();
    placeholderAnimation->setStartValue(dropPlaceholder->maximumHeight());
    placeholderAnimation->setEndValue(kPlaceholderEndHeight);
    placeholderAnimation->start();
}

void PinnedListWidget::hideDropPlaceholder()
{
    currentDropIndex = -1;

    if (!dropPlaceholder->isVisible() &&
        dropPlaceholder->maximumHeight() == 0)
    {
        return;
    }

    placeholderAnimation->stop();
    placeholderAnimation->setStartValue(dropPlaceholder->maximumHeight());
    placeholderAnimation->setEndValue(0);
    placeholderAnimation->start();
}

void PinnedListWidget::hideDropPlaceholderImmediate()
{
    placeholderAnimation->stop();
    currentDropIndex = -1;
    dropPlaceholder->setMinimumHeight(0);
    dropPlaceholder->setMaximumHeight(0);
    pinsLayout->removeWidget(dropPlaceholder);
    dropPlaceholder->hide();
}

void PinnedListWidget::insertPinWidget(
    const QString &cleanPath,
    int index
)
{
    auto *item = new PinnedItemWidget(cleanPath, this);

    connect(
        item,
        &PinnedItemWidget::activated,
        this,
        &PinnedListWidget::directoryActivated
    );
    connect(
        item,
        &PinnedItemWidget::unpinRequested,
        this,
        &PinnedListWidget::removePinnedDirectory
    );

    pinnedPaths.insert(index, cleanPath);
    pinWidgets.insert(index, item);

    int layoutIndex = index;
    const int placeholderIndex = pinsLayout->indexOf(dropPlaceholder);
    if (placeholderIndex >= 0 && placeholderIndex <= index)
    {
        ++layoutIndex;
    }
    pinsLayout->insertWidget(layoutIndex, item);
}

void PinnedListWidget::reorderPin(int from, int to)
{
    if (from < 0 || from >= pinnedPaths.size())
    {
        return;
    }

    int destination = to;
    if (destination > from)
    {
        --destination;
    }
    if (destination < 0)
    {
        destination = 0;
    }
    if (destination >= pinnedPaths.size())
    {
        destination = pinnedPaths.size() - 1;
    }
    if (destination == from)
    {
        return;
    }

    pinnedPaths.move(from, destination);
    PinnedItemWidget *item = pinWidgets.takeAt(from);
    pinWidgets.insert(destination, item);

    pinsLayout->removeWidget(item);
    int layoutIndex = destination;
    const int placeholderIndex = pinsLayout->indexOf(dropPlaceholder);
    if (placeholderIndex >= 0 && placeholderIndex <= destination)
    {
        ++layoutIndex;
    }
    pinsLayout->insertWidget(layoutIndex, item);
    savePins();
}

void PinnedListWidget::animateRemoval(PinnedItemWidget *item)
{
    item->setMinimumHeight(0);

    auto *animation =
        new QPropertyAnimation(item, "maximumHeight", item);
    animation->setDuration(kPinAnimationMs);
    animation->setStartValue(item->height());
    animation->setEndValue(0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(
        animation,
        &QPropertyAnimation::finished,
        item,
        &QObject::deleteLater
    );
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void PinnedListWidget::savePins()
{
    QSettings settings(kSettingsOrganization, kSettingsApplication);
    settings.setValue(kPinnedDirectoriesKey, pinnedPaths);
}

void PinnedListWidget::loadPins()
{
    QSettings settings(kSettingsOrganization, kSettingsApplication);
    const QStringList saved =
        settings.value(kPinnedDirectoriesKey).toStringList();

    for (const QString &path : saved)
    {
        const QString clean = canonicalPinPath(path);
        if (clean.isEmpty() || pinnedPaths.contains(clean))
        {
            continue;
        }

        insertPinWidget(clean, pinnedPaths.size());
    }
}

void PinnedListWidget::dragEnterEvent(QDragEnterEvent *event)
{
    QString path;
    if (classifyDrop(event->mimeData(), &path) == DropKind::None)
    {
        event->ignore();
        return;
    }

    event->acceptProposedAction();

    const int index =
        insertionIndexForY(event->position().toPoint().y());
    currentDropIndex = index;
    showDropPlaceholder(index);
}

void PinnedListWidget::dragMoveEvent(QDragMoveEvent *event)
{
    QString path;
    if (classifyDrop(event->mimeData(), &path) == DropKind::None)
    {
        event->ignore();
        return;
    }

    event->acceptProposedAction();

    const int newIndex =
        insertionIndexForY(event->position().toPoint().y());
    if (newIndex == currentDropIndex)
    {
        return;
    }

    currentDropIndex = newIndex;
    showDropPlaceholder(newIndex);
}

void PinnedListWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    QWidget::dragLeaveEvent(event);

    QTimer::singleShot(
        0,
        this,
        [this]()
        {
            if (!rect().contains(mapFromGlobal(QCursor::pos())))
            {
                hideDropPlaceholder();
            }
        }
    );
}

void PinnedListWidget::dropEvent(QDropEvent *event)
{
    QString path;
    if (classifyDrop(event->mimeData(), &path) == DropKind::None)
    {
        hideDropPlaceholderImmediate();
        event->ignore();
        return;
    }

    int insertAt = currentDropIndex;
    if (insertAt < 0)
    {
        insertAt = insertionIndexForY(event->position().toPoint().y());
    }

    hideDropPlaceholderImmediate();
    addPinnedDirectory(path, insertAt);
    event->acceptProposedAction();
}
