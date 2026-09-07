#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>

// Path helpers shared by pin rows and the pin list.
// Every stored pin path should go through canonicalPinPath so duplicates
// and QSettings keys compare equal regardless of trailing slashes.

// Resolve to an absolute, cleaned filesystem path.
inline QString canonicalPinPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// True when the pin points at the current user's home directory.
inline bool isHomePinPath(const QString &path)
{
    return canonicalPinPath(path) ==
        QDir::cleanPath(QDir::homePath());
}

// Label shown in the sidebar. Home is displayed as "Home" rather than
// the account directory name. "/" has an empty fileName() on Unix.
inline QString pinDisplayName(const QString &path)
{
    if (isHomePinPath(path))
    {
        return QStringLiteral("Home");
    }

    const QString name = QFileInfo(path).fileName();
    if (name.isEmpty())
    {
        return QStringLiteral("/");
    }

    return name;
}

// A pin stays in the list even if this returns false (unmounted drive).
inline bool isAvailableDirectory(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() && info.isDir();
}
