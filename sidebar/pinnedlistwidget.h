#pragma once

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

class PinnedListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PinnedListWidget(QWidget *parent = nullptr);

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

    void savePins();
    void loadPins();

    QVBoxLayout *pinsLayout = nullptr;
    QStringList pinnedPaths;
    QList<PinnedItemWidget *> pinWidgets;

    QWidget *dropPlaceholder = nullptr;
    QPropertyAnimation *placeholderAnimation = nullptr;
    int currentDropIndex = -1;
};
