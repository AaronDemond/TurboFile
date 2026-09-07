#include "directorysizesortproxymodel.h"
#include "directorysizefilesystemmodel.h"

#include <QFileInfo>

DirectorySizeSortProxyModel::DirectorySizeSortProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
    , filesystemModel(new DirectorySizeFileSystemModel(this))
{
    // Compare names without case distinctions and interpret digit runs as
    // numbers, producing a natural order such as item2 before item10.
    nameCollator.setCaseSensitivity(Qt::CaseInsensitive);
    nameCollator.setNumericMode(true);

    // The proxy owns its source model through QObject parenting and exposes the
    // sorted indexes consumed by every QTreeView in MainWindow.
    setSourceModel(filesystemModel);

    // Reevaluate the active ordering whenever source rows or completed size
    // values change. Size sorting is activated only after readiness is checked.
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
    // QFileSystemModel creates a source index for the path. Views use proxy
    // indexes exclusively, so convert the returned root before exposing it.
    return mapFromSource(
        filesystemModel->setRootPath(path)
    );
}

QModelIndex DirectorySizeSortProxyModel::index(
    const QString &path,
    int column
) const
{
    // Path navigation starts at the filesystem source and then crosses the
    // proxy boundary so rootIndex(), selection, and signals share one model.
    return mapFromSource(
        filesystemModel->index(path, column)
    );
}

QString DirectorySizeSortProxyModel::filePath(
    const QModelIndex &index
) const
{
    // Tree views provide proxy indexes. File operations need the underlying
    // filesystem path, so map back before calling QFileSystemModel.
    return filesystemModel->filePath(
        mapToSource(index)
    );
}

bool DirectorySizeSortProxyModel::isDir(
    const QModelIndex &index
) const
{
    // Directory checks follow the same proxy-to-source mapping used by path
    // lookup, keeping existing MainWindow callers unaware of the proxy layer.
    return filesystemModel->isDir(
        mapToSource(index)
    );
}

bool DirectorySizeSortProxyModel::isRegularFile(
    const QModelIndex &index
) const
{
    // QFileInfo::isFile() follows a symbolic link to its file target. Check
    // isSymbolicLink() first so links are excluded from the bottom-row count
    // even when their target is an otherwise ordinary file.
    QFileInfo info = filesystemModel->fileInfo(
        mapToSource(index)
    );

    return
        info.isFile() &&
        !info.isSymbolicLink();
}

void DirectorySizeSortProxyModel::invalidatePaths(
    const QStringList &paths
)
{
    // Cache ownership remains in the source model. This forwarding method keeps
    // mutation code coupled only to the shared model exposed by MainWindow.
    filesystemModel->invalidatePaths(paths);
}

bool DirectorySizeSortProxyModel::directorySizesReady(
    const QModelIndex &parent
) const
{
    // The parent received from QTreeView belongs to the proxy. Map it once,
    // then inspect its direct children through the source model.
    QModelIndex sourceParent = mapToSource(parent);
    int childCount = filesystemModel->rowCount(sourceParent);

    // Size sorting compares only siblings displayed under this root. Nested
    // descendants contribute to each recursive total but are not separate rows
    // in the current comparison, so only direct child directories gate sorting.
    for (int row = 0; row < childCount; row++)
    {
        QModelIndex childIndex =
            filesystemModel->index(row, 0, sourceParent);

        if (
            filesystemModel->isDir(childIndex) &&
            !filesystemModel->directorySizeReady(childIndex)
        )
        {
            // One missing or active calculation is enough to reject the click;
            // otherwise rows could reorder repeatedly as totals arrive.
            return false;
        }
    }

    // Regular files already have synchronous sizes, and every directory has
    // reached either Ready or the terminal Unavailable state.
    return true;
}

void DirectorySizeSortProxyModel::requestDirectorySizes(
    const QModelIndex &parent
)
{
    // Use the source parent because requestDirectorySize() accepts source-model
    // indexes and owns the cache/scheduler behind that model.
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
    // QSortFilterProxyModel passes source-model indexes into lessThan(). They
    // must be used directly here; mapToSource() would incorrectly map them a
    // second time and erase the filesystem metadata needed for comparison.
    QFileInfo leftInfo = filesystemModel->fileInfo(left);
    QFileInfo rightInfo = filesystemModel->fileInfo(right);

    // Establish the directory/file grouping before applying any column-specific
    // comparison. This guarantees that no file can enter the directory group.
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
        // Size comparisons use different sources: cached recursive totals for
        // directories and inexpensive QFileInfo logical sizes for files.
        if (leftIsDirectory)
        {
            // Zero is a valid directory size, so separate availability booleans
            // distinguish missing totals from completed empty directories.
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
                    // Compare raw bytes rather than locale-formatted display
                    // strings, which would produce lexicographic size ordering.
                    return leftSize < rightSize;
                }
            }

            // Unavailable and equal-size directories use their names for a
            // deterministic order within the directory group.
            return compareNames(left, right) < 0;
        }

        if (leftInfo.size() != rightInfo.size())
        {
            // QFileInfo supplies regular-file logical bytes synchronously.
            return leftInfo.size() < rightInfo.size();
        }

        // Equal-size files use names to make their order deterministic.
        return compareNames(left, right) < 0;
    }

    if (left.column() == 0)
    {
        // The Name column uses the configured natural, case-insensitive order.
        return compareNames(left, right) < 0;
    }

    if (left.column() == 3)
    {
        // Compare actual timestamps for Date Modified rather than their
        // localized display strings, then use names to resolve equal dates.
        if (leftInfo.lastModified() != rightInfo.lastModified())
        {
            return leftInfo.lastModified() < rightInfo.lastModified();
        }

        return compareNames(left, right) < 0;
    }

    // Type and any future columns retain Qt's default role-based comparison,
    // after the directory-first partition has already been enforced.
    return QSortFilterProxyModel::lessThan(left, right);
}

int DirectorySizeSortProxyModel::compareNames(
    const QModelIndex &left,
    const QModelIndex &right
) const
{
    // Both indexes are source-model indexes supplied by lessThan(). fileName()
    // extracts only the leaf labels before QCollator performs natural ordering.
    return nameCollator.compare(
        filesystemModel->fileName(left),
        filesystemModel->fileName(right)
    );
}