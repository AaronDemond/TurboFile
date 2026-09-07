#pragma once

#include <QFrame>
#include <QString>

class PinnedListWidget;

class PinnedSidebar : public QFrame
{
    Q_OBJECT

public:
    explicit PinnedSidebar(QWidget *parent = nullptr);

    QSize sizeHint() const override;

    void pinDirectory(const QString &path);
    bool isPinned(const QString &path) const;

signals:
    void directoryActivated(const QString &path);

private:
    PinnedListWidget *pinnedList = nullptr;
};
