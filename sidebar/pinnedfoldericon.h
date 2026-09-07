#pragma once

#include <QColor>
#include <QPixmap>

// Returns the theme folder icon, optionally tinted for a pin color.
// An invalid tint leaves the stock QFileIconProvider folder pixmap unchanged.
QPixmap pinFolderIcon(const QColor &tint, int size);
