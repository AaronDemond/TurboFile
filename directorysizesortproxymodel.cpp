#include "directorysizesortproxymodel.h"
#include "directorysizefilesystemmodel.h"

#include <QFileInfo>

DirectorySizeSortProxyModel::DirectorySizeSortProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
    , filesystemModel(new DirectorySizeFileSystemModel(this))
{
    nameCollator.setCaseSensitivity(Qt::CaseInsensitive);
    nameCollator.setNumericMode(true);

    setSourceModel(filesystemModel);
    setDynamicSortFilter(true);

    // Forward invalidation before the source emits repaint signals. A view
    // currently sorted by Size can then return to its last stable ordering.
    connect(
        filesystemModel,
        &DirectorySizeFileSystemModel::directorySizesInvalidated,
        this,
        &DirectorySizeSortProxyModel::directorySizesInvalidated
    );
}

QModelIndex DirectorySizeSortProxyModel::setRootPath(const QString &path)
{
    return mapFromSource(
        filesystemModel->setRootPath(path)
    );
}

QModelIndex DirectorySizeSortProxyModel::index(
    const QString &path,
    int column
) const
{
    return mapFromSource(
        filesystemModel->index(path, column)
    );
}

QString DirectorySizeSortProxyModel::filePath(
    const QModelIndex &index
) const
{
    return filesystemModel->filePath(
        mapToSource(index)
    );
}

bool DirectorySizeSortProxyModel::isDir(
    const QModelIndex &index
) const
{
    return filesystemModel->isDir(
        mapToSource(index)
    );
}

void DirectorySizeSortProxyModel::invalidatePaths(
    const QStringList &paths
)
{
    filesystemModel->invalidatePaths(paths);
}

bool DirectorySizeSortProxyModel::directorySizesReady(
    const QModelIndex &parent
) const
{
    QModelIndex sourceParent = mapToSource(parent);
    int childCount = filesystemModel->rowCount(sourceParent);

    for (int row = 0; row < childCount; row++)
    {
        QModelIndex childIndex =
            filesystemModel->index(row, 0, sourceParent);

        if (
            filesystemModel->isDir(childIndex) &&
            !filesystemModel->directorySizeReady(childIndex)
        )
        {
            return false;
        }
    }

    return true;
}

void DirectorySizeSortProxyModel::requestDirectorySizes(
    const QModelIndex &parent
)
{
    QModelIndex sourceParent = mapToSource(parent);
    int childCount = filesystemModel->rowCount(sourceParent);

    // Request every direct child directory, including rows outside the
    // viewport that have not caused data() to run yet.
    for (int row = 0; row < childCount; row++)
    {
        QModelIndex childIndex =
            filesystemModel->index(row, 0, sourceParent);

        filesystemModel->requestDirectorySize(childIndex);
    }
}

bool DirectorySizeSortProxyModel::lessThan(
    const QModelIndex &left,
    const QModelIndex &right
) const
{
    QFileInfo leftInfo = filesystemModel->fileInfo(left);
    QFileInfo rightInfo = filesystemModel->fileInfo(right);
    bool leftIsDirectory = leftInfo.isDir();
    bool rightIsDirectory = rightInfo.isDir();

    if (leftIsDirectory != rightIsDirectory)
    {
        // QSortFilterProxyModel reverses the comparison arguments for a
        // descending sort. Account for that reversal to keep directories in
        // the first group for both directions.
        return
            sortOrder() == Qt::AscendingOrder
                ? leftIsDirectory
                : !leftIsDirectory;
    }

    if (left.column() == 1)
    {
        if (leftIsDirectory)
        {
            qint64 leftSize = 0;
            qint64 rightSize = 0;
            bool leftSizeAvailable =
                filesystemModel->directorySize(left, &leftSize);
            bool rightSizeAvailable =
                filesystemModel->directorySize(right, &rightSize);

            if (leftSizeAvailable && rightSizeAvailable)
            {
                if (leftSize != rightSize)
                {
                    return leftSize < rightSize;
                }
            }

            // Unavailable and equal-size directories use their names for a
            // deterministic order within the directory group.
            return compareNames(left, right) < 0;
        }

        if (leftInfo.size() != rightInfo.size())
        {
            return leftInfo.size() < rightInfo.size();
        }

        return compareNames(left, right) < 0;
    }

    if (left.column() == 0)
    {
        return compareNames(left, right) < 0;
    }

    if (left.column() == 3)
    {
        if (leftInfo.lastModified() != rightInfo.lastModified())
        {
            return leftInfo.lastModified() < rightInfo.lastModified();
        }

        return compareNames(left, right) < 0;
    }

    return QSortFilterProxyModel::lessThan(left, right);
}

int DirectorySizeSortProxyModel::compareNames(
    const QModelIndex &left,
    const QModelIndex &right
) const
{
    return nameCollator.compare(
        filesystemModel->fileName(left),
        filesystemModel->fileName(right)
    );
}