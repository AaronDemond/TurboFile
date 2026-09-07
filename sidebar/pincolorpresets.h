#pragma once

// Color submenu helpers for pin rows. Header-only so the item widget can
// build the menu without a separate translation unit.

#include <QAction>
#include <QColor>
#include <QIcon>
#include <QList>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QVariant>

// One named swatch in the pin Color submenu.
// The id is stable if we ever key off names; persistence stores hex, not ids,
// so custom colors still round-trip.
struct PinColorPreset
{
    QString id;
    QString label;
    QColor color;
};

// Built-in palette shown under Color. Keep this list small and fixed.
inline QList<PinColorPreset> pinColorPresets()
{
    return {
        {QStringLiteral("red"), QStringLiteral("Red"), QColor(QStringLiteral("#E53935"))},
        {QStringLiteral("orange"), QStringLiteral("Orange"), QColor(QStringLiteral("#FB8C00"))},
        {QStringLiteral("yellow"), QStringLiteral("Yellow"), QColor(QStringLiteral("#FDD835"))},
        {QStringLiteral("green"), QStringLiteral("Green"), QColor(QStringLiteral("#43A047"))},
        {QStringLiteral("teal"), QStringLiteral("Teal"), QColor(QStringLiteral("#00897B"))},
        {QStringLiteral("blue"), QStringLiteral("Blue"), QColor(QStringLiteral("#1E88E5"))},
        {QStringLiteral("purple"), QStringLiteral("Purple"), QColor(QStringLiteral("#8E24AA"))},
        {QStringLiteral("gray"), QStringLiteral("Gray"), QColor(QStringLiteral("#757575"))}
    };
}

// Small square used as a QAction icon so the user can see the color
// without opening Custom.
inline QPixmap pinColorSwatch(const QColor &color)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setPen(QColor(0, 0, 0, 80));
    painter.setBrush(color);
    painter.drawRect(0, 0, 11, 11);
    return pixmap;
}

// Compare RGB only so equivalent hex strings check the same menu item.
inline bool pinColorsMatch(const QColor &left, const QColor &right)
{
    return left.isValid() &&
        right.isValid() &&
        left.rgb() == right.rgb();
}

// Append a Color submenu to the pin context menu.
// Each action stores a "pinColorRole" of default, preset, or custom so
// PinnedItemWidget can apply the choice without knowing preset contents.
inline QMenu *addPinColorMenu(QMenu *parent, const QColor &current)
{
    QMenu *menu = parent->addMenu(QObject::tr("Color"));

    // Invalid color means the stock theme folder icon.
    QAction *defaultAction = menu->addAction(QObject::tr("Default"));
    defaultAction->setCheckable(true);
    defaultAction->setChecked(!current.isValid());
    defaultAction->setData(QVariant());
    defaultAction->setProperty("pinColorRole", QStringLiteral("default"));

    menu->addSeparator();

    // Check the matching preset, if any, so the current color is visible.
    bool matchedPreset = false;
    const QList<PinColorPreset> presets = pinColorPresets();
    for (const PinColorPreset &preset : presets)
    {
        QAction *action = menu->addAction(preset.label);
        action->setIcon(QIcon(pinColorSwatch(preset.color)));
        action->setCheckable(true);
        const bool matched = pinColorsMatch(current, preset.color);
        action->setChecked(matched);
        matchedPreset = matchedPreset || matched;
        action->setData(preset.color);
        action->setProperty("pinColorRole", QStringLiteral("preset"));
    }

    menu->addSeparator();

    // Custom is checked only when the stored color is not one of the presets.
    QAction *customAction = menu->addAction(QObject::tr("Custom..."));
    customAction->setCheckable(true);
    customAction->setChecked(current.isValid() && !matchedPreset);
    customAction->setProperty("pinColorRole", QStringLiteral("custom"));

    return menu;
}
