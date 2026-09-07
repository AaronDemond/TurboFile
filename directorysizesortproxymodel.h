#pragma once
#ifndef DIRECTORYSIZESORTPROXYMODEL_H
#define DIRECTORYSIZESORTPROXYMODEL_H

#include <QCollator>
#include <QSortFilterProxyModel>
#include <QStringList>

class DirectorySizeFileSystemModel;

// Sorts the shared filesystem model while preserving the path-oriented API
// used throughout MainWindow. Every ordering keeps directories above files.
class DirectorySizeSortProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit DirectorySizeSortProxyModel(QObject *parent = nullptr);

    // Declaring the inherited overload avoids hiding index(row, column,
    // parent) when this class adds the convenience index(path, column) form.
    using QSortFilterProxyModel::index;

    // Map QFileSystemModel's path APIs through the proxy so existing callers
    // can continue working entirely with indexes supplied by the tree view.
    QModelIndex setRootPath(const QString &path);
    QModelIndex index(
        const QString &path,
        int column = 0
    ) const;
    QString filePath(const QModelIndex &index) const;
    bool isDir(const QModelIndex &index) const;

    // Identify direct regular files without treating symbolic links to files
    // as files. BrowserPane uses this for its active-directory status count.
    bool isRegularFile(const QModelIndex &index) const;

    // Forward successful filesystem mutations to the source cache.
    void invalidatePaths(const QStringList &paths);

    // Size sorting is allowed only after every direct child directory under
    // the displayed root has reached a completed or unavailable state.
    bool directorySizesReady(const QModelIndex &parent) const;
    void requestDirectorySizes(const QModelIndex &parent);

signals:
    // Views use this to leave Size sorting as soon as cached inputs go stale.
    void directorySizesInvalidated();

protected:
    // QSortFilterProxyModel passes source-model indexes to this comparator.
    // Keeping that contract explicit prevents accidental double mapping.
    bool lessThan(
        const QModelIndex &left,
        const QModelIndex &right
    ) const override;

private:
    // Compare names naturally so values such as file2 precede file10.
    int compareNames(
        const QModelIndex &left,
        const QModelIndex &right
    ) const;

    // The proxy owns the source model through QObject parenting. MainWindow
    // owns only this proxy, while every tab shares both model layers.
    DirectorySizeFileSystemModel *filesystemModel;

    // QCollator provides locale-aware, case-insensitive natural name order
    // and is reused for primary Name sorts and deterministic tie breaking.
    QCollator nameCollator;
};

#endif // DIRECTORYSIZESORTPROXYMODEL_H