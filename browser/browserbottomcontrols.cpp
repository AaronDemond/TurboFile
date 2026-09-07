#include "browserbottomcontrols.h"

#include <QSizePolicy>

BrowserBottomControls::BrowserBottomControls(QWidget *parent)
    : QWidget(parent)
{
    // Expanding tells QStatusBar to keep assigning this row its available
    // width. The previous Ignored policy allowed a relayout after pane closure
    // to collapse the complete control row to zero pixels.
    setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Fixed
    );
}

QSize BrowserBottomControls::minimumSizeHint() const
{
    QSize minimumSize = QWidget::minimumSizeHint();

    // Child buttons retain their own useful size hints during normal layouts,
    // but their combined width must not become MainWindow's hard minimum.
    minimumSize.setWidth(0);
    return minimumSize;
}
