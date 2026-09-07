#include "directorysizefilesystemmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QLocale>
#include <QMetaObject>
#include <QRunnable>
#include <QStringList>

#include <limits>

DirectorySizeFileSystemModel::DirectorySizeFileSystemModel(QObject *parent)
    : QFileSystemModel(parent)
{
    // Recursive directory scans are disk-bound. Two workers allow progress on
    // separate directories without overwhelming the storage device.
    workerPool.setMaxThreadCount(2);
    connectFilesystemInvalidationSignals();
}

DirectorySizeFileSystemModel::~DirectorySizeFileSystemModel()
{
    // Workers only hold value data and cancellation tokens. Cancelling first
    // lets the at-most-two active scans stop at their next filesystem entry.
    shuttingDown = true;

    for (CacheEntry &entry : sizeCache)
    {
        if (entry.cancellation)
        {
            entry.cancellation->store(true);
        }
    }

    queuedTasks.clear();
    workerPool.clear();
    workerPool.waitForDone();
}

QVariant DirectorySizeFileSystemModel::data(
    const QModelIndex &index,
    int role
) const
{
    // Only directory display text in the Size column needs custom behavior.
    if (
        !index.isValid() ||
        index.column() != 1 ||
        role != Qt::DisplayRole
    )
    {
        return QFileSystemModel::data(index, role);
    }

    QFileInfo info = fileInfo(index);

    // Symbolic links are deliberately not traversed because they can create
    // cycles or count the same files through multiple paths.
    if (info.isSymbolicLink() && info.isDir())
    {
        return "--";
    }

    if (!info.isDir())
    {
        return QFileSystemModel::data(index, role);
    }

    QString path = normalizedPath(info.absoluteFilePath());
    auto cacheIterator = sizeCache.constFind(path);

    if (cacheIterator == sizeCache.constEnd())
    {
        // data() is const and may be called repeatedly while painting. Queue
        // the state-changing request on the model's UI thread and deduplicate
        // it when the queued call runs.
        auto *model =
            const_cast<DirectorySizeFileSystemModel *>(this);

        QMetaObject::invokeMethod(
            model,
            [model, path]()
            {
                model->ensureDirectorySizeRequested(path);
            },
            Qt::QueuedConnection
        );

        return "...";
    }

    const CacheEntry &entry = cacheIterator.value();

    if (entry.state == CacheState::Pending)
    {
        return "...";
    }

    if (entry.state == CacheState::Unavailable)
    {
        return "--";
    }

    QString sizeText = formattedSize(entry.bytes);

    // A leading tilde distinguishes a best-effort result from a complete
    // total when a nested directory disappeared or was unreadable.
    if (entry.partial)
    {
        sizeText.prepend('~');
    }

    return sizeText;
}

bool DirectorySizeFileSystemModel::directorySizeReady(
    const QModelIndex &index
) const
{
    if (!index.isValid())
    {
        return true;
    }

    QFileInfo info = fileInfo(index);

    if (!info.isDir() || info.isSymbolicLink())
    {
        return true;
    }

    QString path = normalizedPath(info.absoluteFilePath());
    auto cacheIterator = sizeCache.constFind(path);

    return
        cacheIterator != sizeCache.constEnd() &&
        cacheIterator->state != CacheState::Pending;
}

bool DirectorySizeFileSystemModel::directorySize(
    const QModelIndex &index,
    qint64 *bytes
) const
{
    if (!index.isValid() || bytes == nullptr)
    {
        return false;
    }

    QString path = normalizedPath(filePath(index));
    auto cacheIterator = sizeCache.constFind(path);

    if (
        cacheIterator == sizeCache.constEnd() ||
        cacheIterator->state != CacheState::Ready
    )
    {
        return false;
    }

    *bytes = cacheIterator->bytes;
    return true;
}

void DirectorySizeFileSystemModel::requestDirectorySize(
    const QModelIndex &index
)
{
    if (!index.isValid())
    {
        return;
    }

    QFileInfo info = fileInfo(index);

    if (!info.isDir() || info.isSymbolicLink())
    {
        return;
    }

    ensureDirectorySizeRequested(
        normalizedPath(info.absoluteFilePath())
    );
}

void DirectorySizeFileSystemModel::invalidatePaths(
    const QStringList &paths
)
{
    QStringList normalizedPaths;

    for (const QString &path : paths)
    {
        QString normalized = normalizedPath(path);

        if (!normalized.isEmpty())
        {
            normalizedPaths.append(normalized);
        }
    }

    if (normalizedPaths.isEmpty())
    {
        return;
    }

    // A view that is currently sorted by Size must return to its previous
    // stable sort before pending values enter that comparison.
    emit directorySizesInvalidated();

    QStringList invalidatedCachePaths;

    // A changed path affects its own cached subtree and every cached ancestor
    // whose recursive total includes it.
    for (auto iterator = sizeCache.begin(); iterator != sizeCache.end();)
    {
        bool shouldInvalidate = false;

        for (const QString &changedPath : normalizedPaths)
        {
            if (pathsOverlap(iterator.key(), changedPath))
            {
                shouldInvalidate = true;
                break;
            }
        }

        if (!shouldInvalidate)
        {
            ++iterator;
            continue;
        }

        if (iterator->cancellation)
        {
            iterator->cancellation->store(true);
        }

        invalidatedCachePaths.append(iterator.key());
        iterator = sizeCache.erase(iterator);
    }

    // Repainting invalidated visible rows returns them to "..." and lazily
    // schedules replacement work. Non-visible paths remain uncached.
    for (const QString &path : invalidatedCachePaths)
    {
        refreshSizeCell(path);
    }
}

void DirectorySizeFileSystemModel::ensureDirectorySizeRequested(
    const QString &path
)
{
    if (shuttingDown || sizeCache.contains(path))
    {
        return;
    }

    CacheEntry entry;
    entry.generation = nextGeneration++;
    entry.cancellation =
        std::make_shared<std::atomic_bool>(false);

    sizeCache.insert(path, entry);

    SizeTask task;
    task.path = path;
    task.generation = entry.generation;
    task.cancellation = entry.cancellation;
    queuedTasks.enqueue(task);

    startQueuedTasks();
}

void DirectorySizeFileSystemModel::startQueuedTasks()
{
    while (
        !shuttingDown &&
        activeTaskCount < workerPool.maxThreadCount() &&
        !queuedTasks.isEmpty()
    )
    {
        SizeTask task = queuedTasks.dequeue();
        auto cacheIterator = sizeCache.constFind(task.path);

        // Invalidated queued tasks remain harmless in the FIFO until reached.
        if (
            cacheIterator == sizeCache.constEnd() ||
            cacheIterator->generation != task.generation ||
            task.cancellation->load()
        )
        {
            continue;
        }

        activeTaskCount++;

        workerPool.start(
            QRunnable::create(
                [this, task]()
                {
                    SizeResult result =
                        calculateDirectorySize(
                            task.path,
                            task.cancellation
                        );

                    // The model remains alive while its destructor waits for
                    // active workers. Qt drops this queued call if destruction
                    // completes before the event loop processes it.
                    QMetaObject::invokeMethod(
                        this,
                        [this, task, result]()
                        {
                            finishTask(task, result);
                        },
                        Qt::QueuedConnection
                    );
                }
            )
        );
    }
}

void DirectorySizeFileSystemModel::finishTask(
    const SizeTask &task,
    const SizeResult &result
)
{
    activeTaskCount--;

    auto cacheIterator = sizeCache.find(task.path);

    // An invalidation may remove and replace this path while the old worker
    // is finishing. The generation check prevents stale data from winning.
    if (
        cacheIterator != sizeCache.end() &&
        cacheIterator->generation == task.generation &&
        !result.cancelled
    )
    {
        cacheIterator->bytes = result.bytes;
        cacheIterator->partial = result.partial;
        cacheIterator->state =
            result.unavailable
                ? CacheState::Unavailable
                : CacheState::Ready;
        cacheIterator->cancellation.reset();

        refreshSizeCell(task.path);
    }

    startQueuedTasks();
}

void DirectorySizeFileSystemModel::refreshSizeCell(const QString &path)
{
    QModelIndex nameIndex = index(path);

    if (!nameIndex.isValid())
    {
        return;
    }

    QModelIndex sizeIndex = nameIndex.siblingAtColumn(1);

    // Prevent the model's own repaint signal from being mistaken for an
    // external filesystem content change by the invalidation connection.
    emittingSizeChange = true;
    emit dataChanged(
        sizeIndex,
        sizeIndex,
        {Qt::DisplayRole}
    );
    emittingSizeChange = false;
}

void DirectorySizeFileSystemModel::connectFilesystemInvalidationSignals()
{
    connect(
        this,
        &QFileSystemModel::rowsInserted,
        this,
        [this](const QModelIndex &parent, int, int)
        {
            invalidatePaths({filePath(parent)});
        }
    );

    connect(
        this,
        &QFileSystemModel::rowsRemoved,
        this,
        [this](const QModelIndex &parent, int, int)
        {
            invalidatePaths({filePath(parent)});
        }
    );

    connect(
        this,
        &QFileSystemModel::rowsMoved,
        this,
        [this](
            const QModelIndex &sourceParent,
            int,
            int,
            const QModelIndex &destinationParent,
            int
        )
        {
            invalidatePaths(
                {
                    filePath(sourceParent),
                    filePath(destinationParent)
                }
            );
        }
    );

    connect(
        this,
        &QFileSystemModel::fileRenamed,
        this,
        [this](
            const QString &path,
            const QString &oldName,
            const QString &newName
        )
        {
            QDir directory(path);
            invalidatePaths(
                {
                    directory.filePath(oldName),
                    directory.filePath(newName)
                }
            );
        }
    );

    connect(
        this,
        &QFileSystemModel::dataChanged,
        this,
        [this](
            const QModelIndex &topLeft,
            const QModelIndex &bottomRight,
            const QList<int> &
        )
        {
            if (emittingSizeChange)
            {
                return;
            }

            QStringList changedPaths;

            for (
                int row = topLeft.row();
                row <= bottomRight.row();
                row++
            )
            {
                changedPaths.append(
                    filePath(topLeft.sibling(row, 0))
                );
            }

            invalidatePaths(changedPaths);
        }
    );

    connect(
        this,
        &QFileSystemModel::modelReset,
        this,
        [this]()
        {
            QStringList cachedPaths = sizeCache.keys();
            invalidatePaths(cachedPaths);
        }
    );
}

QString DirectorySizeFileSystemModel::normalizedPath(const QString &path)
{
    if (path.isEmpty())
    {
        return QString();
    }

    return QDir::cleanPath(
        QFileInfo(path).absoluteFilePath()
    );
}

DirectorySizeFileSystemModel::SizeResult
DirectorySizeFileSystemModel::calculateDirectorySize(
    const QString &path,
    const std::shared_ptr<std::atomic_bool> &cancellation
)
{
    SizeResult result;
    QFileInfo rootInfo(path);

    if (
        !rootInfo.exists() ||
        !rootInfo.isDir() ||
        rootInfo.isSymbolicLink() ||
        !rootInfo.isReadable()
    )
    {
        result.unavailable = true;
        return result;
    }

    QStringList pendingDirectories = {path};

    while (!pendingDirectories.isEmpty())
    {
        if (cancellation->load())
        {
            result.cancelled = true;
            return result;
        }

        QString directoryPath = pendingDirectories.takeLast();
        QFileInfo directoryInfo(directoryPath);

        if (
            !directoryInfo.exists() ||
            !directoryInfo.isDir() ||
            !directoryInfo.isReadable()
        )
        {
            result.partial = true;
            continue;
        }

        QDir directory(directoryPath);
        QFileInfoList entries = directory.entryInfoList(
            QDir::AllEntries |
                QDir::Hidden |
                QDir::System |
                QDir::NoDotAndDotDot |
                QDir::NoSymLinks
        );

        for (const QFileInfo &entry : entries)
        {
            if (cancellation->load())
            {
                result.cancelled = true;
                return result;
            }

            if (entry.isDir())
            {
                pendingDirectories.append(entry.absoluteFilePath());
                continue;
            }

            if (!entry.isFile())
            {
                continue;
            }

            qint64 fileSize = entry.size();
            qint64 maximumSize = std::numeric_limits<qint64>::max();

            if (result.bytes > maximumSize - fileSize)
            {
                result.bytes = maximumSize;
            }
            else
            {
                result.bytes += fileSize;
            }
        }
    }

    return result;
}

QString DirectorySizeFileSystemModel::formattedSize(qint64 bytes)
{
    return QLocale().formattedDataSize(
        bytes,
        1,
        QLocale::DataSizeTraditionalFormat
    );
}

bool DirectorySizeFileSystemModel::pathsOverlap(
    const QString &firstPath,
    const QString &secondPath
)
{
    if (firstPath == secondPath)
    {
        return true;
    }

    QString firstPrefix =
        firstPath == "/" ? firstPath : firstPath + '/';
    QString secondPrefix =
        secondPath == "/" ? secondPath : secondPath + '/';

    return
        firstPath.startsWith(secondPrefix) ||
        secondPath.startsWith(firstPrefix);
}