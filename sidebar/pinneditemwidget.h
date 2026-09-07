#pragma once

#include <QFrame>
#include <QPoint>
#include <QString>

class QLabel;
class QMouseEvent;
class QEnterEvent;
class QEvent;

class PinnedItemWidget : public QFrame
{
    Q_OBJECT

public:
    explicit PinnedItemWidget(
        const QString &path,
        QWidget *parent = nullptr
    );

    const QString &path() const;

    void refreshAvailability();

signals:
    void activated(const QString &path);
    void unpinRequested(const QString &path);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void applyAppearance();
    void showUnpinMenu(const QPoint &globalPos);
    void startDrag();
    void setHovered(bool hovered);

    QString m_path;
    QLabel *iconLabel = nullptr;
    QLabel *nameLabel = nullptr;
    QPoint dragStartPosition;
    bool pressed = false;
    bool available = true;
};
