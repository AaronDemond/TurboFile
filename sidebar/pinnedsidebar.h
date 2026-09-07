#pragma once

#include <QFrame>
#include <QString>

class PinnedListWidget;
class QLabel;
class QScrollArea;
class QToolButton;
class QWidget;

// Outer chrome for the window-level pinned-directory panel.
// Shows the title, collapse/expand controls, and a scroll area wrapping
// PinnedListWidget. MainWindow owns width persistence via the splitter.
class PinnedSidebar : public QFrame
{
    Q_OBJECT

public:
    explicit PinnedSidebar(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Used by the file-view context menu to pin a selected directory.
    void pinDirectory(const QString &path);
    bool isPinned(const QString &path) const;

    bool isCollapsed() const;
    void setCollapsed(bool collapsed);

signals:
    // Forwarded from the pin list when a row is clicked.
    void directoryActivated(const QString &path);
    void collapsedChanged(bool collapsed);

private:
    void applyCollapsedState();

    PinnedListWidget *pinnedList = nullptr;
    QWidget *headerWidget = nullptr;
    QLabel *titleLabel = nullptr;
    QToolButton *collapseButton = nullptr;
    QWidget *collapsedStrip = nullptr;
    QToolButton *expandButton = nullptr;
    QScrollArea *scrollArea = nullptr;
    bool collapsed = false;
};
