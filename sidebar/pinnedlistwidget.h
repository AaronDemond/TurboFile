#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class PinnedItemWidget;
class QMimeData;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QPropertyAnimation;
class QVBoxLayout;

// Ordered collection of pins: drop target, reorder, persistence, animation.
// pinnedPaths is the single source of truth for order. pinColors maps
// canonical path to a non-default folder tint.
class PinnedListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PinnedListWidget(QWidget *parent = nullptr);

    // index -1 appends. An already-pinned path at a real index is a reorder.
    void addPinnedDirectory(
        const QString &path,
        int index = -1
    );

    void removePinnedDirectory(const QString &path);

    bool containsPath(const QString &path) const;

signals:
    void directoryActivated(const QString &path);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    // Distinguishes tree/file-manager directory drops from pin-row reorders.
    enum class DropKind
    {
        None,
        ExternalDirectory,
        InternalPin
    };

    DropKind classifyDrop(
        const QMimeData *mime,
        QString *outPath
    ) const;

    int insertionIndexForY(int y) const;
    void showDropPlaceholder(int index);
    void hideDropPlaceholder();
    void hideDropPlaceholderImmediate();

    void insertPinWidget(const QString &cleanPath, int index);
    void reorderPin(int from, int to);
    void animateRemoval(PinnedItemWidget *item);
    // Invalid color removes the stored tint (Default).
    void setStoredIconColor(const QString &path, const QColor &color);

    void savePins();
    void loadPins();

    QVBoxLayout *pinsLayout = nullptr;
    // Authoritative ordered list of canonical directory paths.
    QStringList pinnedPaths;
    // Widgets parallel to pinnedPaths. Do not infer order from layout children.
    QList<PinnedItemWidget *> pinWidgets;
    // Only non-default tints. Missing key means the theme folder icon.
    QHash<QString, QColor> pinColors;

    // Temporary gap shown while dragging. Not a pin and not in pinnedPaths.
    QWidget *dropPlaceholder = nullptr;
    // Reused so dragMoveEvent does not allocate a new animation every pixel.
    QPropertyAnimation *placeholderAnimation = nullptr;
    int currentDropIndex = -1;
};
