#pragma once

#include <QWidget>

// Window-wide status-row container for Search, view-mode buttons, and the file
// count. It expands into the space QStatusBar offers but deliberately does not
// turn the combined child size hints into a hard minimum for MainWindow.
class BrowserBottomControls : public QWidget
{
public:
    explicit BrowserBottomControls(QWidget *parent = nullptr);

    // Keep the layout's natural height while allowing the horizontal row to
    // compress as far as the surrounding window and pane minimums require.
    QSize minimumSizeHint() const override;
};
