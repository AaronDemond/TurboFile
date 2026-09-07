#include "pinnedfoldericon.h"

#include <QFileIconProvider>
#include <QImage>
#include <QRgb>

// Build a folder glyph for a pin row. The shape always comes from the
// desktop theme so the icon still looks like a folder after tinting.
QPixmap pinFolderIcon(const QColor &tint, int size)
{
    QFileIconProvider icons;
    const QPixmap base =
        icons.icon(QFileIconProvider::Folder).pixmap(size, size);

    // Default color (invalid QColor) or a missing pixmap: no processing.
    if (!tint.isValid() || base.isNull())
    {
        return base;
    }

    QImage image = base.toImage().convertToFormat(QImage::Format_ARGB32);
    const int tintR = tint.red();
    const int tintG = tint.green();
    const int tintB = tint.blue();

    // Multiply the chosen color by each pixel's luminance so highlights
    // and shadows in the original folder artwork are preserved.
    // Alpha is left unchanged so the folder outline stays intact.
    for (int y = 0; y < image.height(); ++y)
    {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const QRgb pixel = line[x];
            const int alpha = qAlpha(pixel);
            if (alpha == 0)
            {
                continue;
            }

            const int luminance = qGray(pixel);
            line[x] = qRgba(
                (tintR * luminance) / 255,
                (tintG * luminance) / 255,
                (tintB * luminance) / 255,
                alpha
            );
        }
    }

    return QPixmap::fromImage(image);
}
