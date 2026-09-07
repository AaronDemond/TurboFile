#include "browserlineedit.h"

#include <QApplication>
#include <QtGlobal>

BrowserLineEdit::BrowserLineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    // BrowserPane and QStatusBar can inherit different parent palettes and
    // fonts from some desktop themes. Assigning the application-level values
    // here gives every BrowserLineEdit the same native QLineEdit appearance
    // while still following application-wide light/dark theme changes present
    // when the control is constructed.
    setPalette(QApplication::palette());
    setFont(QApplication::font());
}

void BrowserLineEdit::setPreferredWidth(int width)
{
    const int normalizedWidth = qMax(width, 0);

    if (preferredWidth == normalizedWidth)
    {
        return;
    }

    preferredWidth = normalizedWidth;

    // sizeHint() communicates the desired aligned width to the layout. Do not
    // set a fixed or maximum width here: hard width properties can participate
    // in parent layout negotiation and make QMainWindow jump wider when the
    // sidebar changes size.
    updateGeometry();
}

QSize BrowserLineEdit::sizeHint() const
{
    QSize preferredSize = QLineEdit::sizeHint();

    if (preferredWidth >= 0)
    {
        preferredSize.setWidth(preferredWidth);
    }

    return preferredSize;
}

QSize BrowserLineEdit::minimumSizeHint() const
{
    QSize minimumSize = QLineEdit::minimumSizeHint();

    // QLineEdit normally contributes a text-friendly minimum width to parent
    // layouts. Clearing only that horizontal hint lets narrow panes and the
    // status bar compress while retaining the native control height.
    minimumSize.setWidth(0);
    return minimumSize;
}
