#pragma once

#include <QColor>
#include <QFrame>
#include <QPoint>
#include <QString>

class QAction;
class QLabel;
class QMouseEvent;
class QEnterEvent;
class QEvent;

// One row in the pinned-directory list.
// Owns a single canonical path, draws the icon/name, and reports user
// actions. It never writes QSettings or mutates the ordered pin list.
class PinnedItemWidget : public QFrame
{
    Q_OBJECT

public:
    explicit PinnedItemWidget(
        const QString &path,
        QWidget *parent = nullptr
    );

    // Canonical path stored for this pin. Safe to return by const reference
    // because m_path lives as long as the widget.
    const QString &path() const;

    void refreshAvailability();
    void setIconColor(const QColor &color);
    QColor iconColor() const;

signals:
    // Left-click on an available directory.
    void activated(const QString &path);
    // Unpin menu, or a click on a missing directory.
    void unpinRequested(const QString &path);
    // Color submenu changed the tint. Invalid color means Default.
    void iconColorChanged(const QString &path, const QColor &color);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void applyAppearance();
    void showContextMenu(const QPoint &globalPos);
    void applyColorAction(QAction *action);
    void startDrag();
    void setHovered(bool hovered);

    // Canonical filesystem path this row represents.
    QString m_path;
    // Invalid means the untinted theme folder icon.
    QColor m_iconColor;
    QLabel *iconLabel = nullptr;
    QLabel *nameLabel = nullptr;
    QPoint dragStartPosition;
    bool pressed = false;
    bool available = true;
};
