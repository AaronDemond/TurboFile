#pragma once

#include <QLineEdit>

// Shared text-entry control used by both the per-pane path field and the
// window-wide search field. Constructing both controls from this class keeps
// their native frame, font, palette, and interaction states identical even
// though they live under different parent widgets.
class BrowserLineEdit : public QLineEdit
{
public:
    explicit BrowserLineEdit(QWidget *parent = nullptr);

    // Request a normal layout width without turning it into a hard minimum.
    // The status-bar search uses this to align with the sidebar when space is
    // available while remaining shrinkable with the rest of the window.
    void setPreferredWidth(int width);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    // A negative value keeps QLineEdit's native preferred width.
    int preferredWidth = -1;
};
