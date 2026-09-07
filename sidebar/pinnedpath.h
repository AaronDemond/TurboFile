#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>

inline QString canonicalPinPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

inline bool isHomePinPath(const QString &path)
{
    return canonicalPinPath(path) ==
        QDir::cleanPath(QDir::homePath());
}

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

inline bool isAvailableDirectory(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() && info.isDir();
}
