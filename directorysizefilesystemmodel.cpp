#include "directorysizefilesystemmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QLocale>
#include <QMetaObject>
#include <QRunnable>
#include <QStringList>
#include <QStorageInfo>

#include <limits>

DirectorySizeFileSystemModel::DirectorySizeFileSystemModel(QObject *parent)
    : QFileSystemModel(parent)
{
    // Recursive directory scans are disk-bound. Two workers allow progress on
    // separate directories without overwhelming the storage device.
    workerPool.setMaxThreadCount(2);

    // QFileSystemModel watches loaded directories for external changes. Its
    // signals are translated into cache invalidations in the model/UI thread.
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

    // Prevent queued work from starting, remove runnables that have not begun,
    // and wait only for the at-most-two active workers to observe cancellation.
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

    // QFileSystemModel resolves the source index to filesystem metadata. This
    // remains on the UI thread and does not perform recursive traversal.
    QFileInfo info = fileInfo(index);

    // Symbolic links are deliberately not traversed because they can create
    // cycles or count the same files through multiple paths.
    if (info.isSymbolicLink() && info.isDir())
    {
        return "--";
    }

    if (!info.isDir())
    {
        // Regular files already have a cheap numeric size supplied by Qt, so
        // preserve the framework's display formatting for those rows.
        return QFileSystemModel::data(index, role);
    }

    // Absolute, cleaned paths let multiple tabs looking at the same directory
    // share one cache entry and one background calculation.
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

    // The const reference avoids copying the shared cancellation token while
    // this UI-thread lookup selects the text for the current cache state.
    const CacheEntry &entry = cacheIterator.value();

    if (entry.state == CacheState::Pending)
    {
        // Pending covers both FIFO-queued and actively running calculations.
        return "...";
    }

    if (entry.state == CacheState::Unavailable)
    {
        // A completed but unreadable or invalid root has no sortable total.
        return "--";
    }

    // Formatting is intentionally deferred until painting. The cache retains
    // raw bytes for numeric sorting and avoids parsing presentation strings.
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
    // Invalid indexes do not represent a directory that can block sorting.
    if (!index.isValid())
    {
        return true;
    }

    // Files already have synchronous sizes, and symbolic-link directories are
    // terminal "--" rows because the worker deliberately never follows them.
    QFileInfo info = fileInfo(index);

    if (!info.isDir() || info.isSymbolicLink())
    {
        return true;
    }

    // A directory is sortable only after it has either a Ready byte count or
    // a terminal Unavailable state. Missing and Pending entries both block.
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
    // The output pointer makes availability explicit: false means callers
    // must use a fallback comparison rather than treating zero as the size.
    if (!index.isValid() || bytes == nullptr)
    {
        return false;
    }

    // Sorting receives source-model indexes, so filePath() can be called
    // directly without proxy mapping at this layer.
    QString path = normalizedPath(filePath(index));
    auto cacheIterator = sizeCache.constFind(path);

    if (
        cacheIterator == sizeCache.constEnd() ||
        cacheIterator->state != CacheState::Ready
    )
    {
        return false;
    }

    // Copy only the numeric total. Presentation details such as '~' belong to
    // data(), not the sorting comparator.
    *bytes = cacheIterator->bytes;
    return true;
}

void DirectorySizeFileSystemModel::requestDirectorySize(
    const QModelIndex &index
)
{
    // Header-triggered prefetching may inspect every child row. Ignore invalid
    // rows, regular files, and links because none need recursive background work.
    if (!index.isValid())
    {
        return;
    }

    QFileInfo info = fileInfo(index);

    if (!info.isDir() || info.isSymbolicLink())
    {
        return;
    }

    // ensureDirectorySizeRequested() owns deduplication, so this public entry
    // point is safe even when painting already requested the same directory.
    ensureDirectorySizeRequested(
        normalizedPath(info.absoluteFilePath())
    );
}

void DirectorySizeFileSystemModel::invalidatePaths(
    const QStringList &paths
)
{
    // Normalize all caller-provided paths before comparison. Empty paths can
    // arise from invalid model parents and must not invalidate the whole cache.
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

    // Keep removed keys long enough to repaint any rows that are still visible.
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
            // Cancellation is cooperative. Active workers stop between entries;
            // queued tasks are rejected by their missing cache generation.
            iterator->cancellation->store(true);
        }

        // erase() returns the next valid iterator, allowing removal during the
        // scan without incrementing an invalidated QHash iterator.
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
    // One cache entry represents one queued or active task. This check also
    // deduplicates the many queued requests data() may post while repainting.
    if (shuttingDown || sizeCache.contains(path))
    {
        return;
    }

    // The cancellation token is shared with the future worker. No worker owns
    // or accesses this CacheEntry directly, so cache state remains UI-thread-only.
    CacheEntry entry;
    entry.generation = nextGeneration++;
    entry.cancellation =
        std::make_shared<std::atomic_bool>(false);

    sizeCache.insert(path, entry);

    // Copy the immutable task snapshot into the FIFO. Its generation binds the
    // eventual result to this exact cache-entry lifetime.
    SizeTask task;
    task.path = path;
    task.generation = entry.generation;
    task.cancellation = entry.cancellation;
    queuedTasks.enqueue(task);

    // Fill any currently free worker slot immediately; otherwise the FIFO is
    // resumed when an active task reports completion.
    startQueuedTasks();
}

void DirectorySizeFileSystemModel::startQueuedTasks()
{
    // Queue bookkeeping runs only on the model thread. The loop launches no
    // more than the pool limit and leaves excess work waiting in FIFO order.
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

        // Reserve the slot before starting the runnable. finishTask() releases
        // it on the UI thread after the worker posts its plain-data result.
        activeTaskCount++;

        workerPool.start(
            QRunnable::create(
                [this, task]()
                {
                    // Only this pure filesystem traversal runs on the worker.
                    // It receives a path and atomic token, never a model index.
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
    // This method is reached through a queued invocation on the model thread,
    // so it is safe to mutate the UI-thread-owned cache and scheduler counters.
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
        // Translate the worker's plain result into the terminal cache state.
        // A partial result remains Ready because it still has a sortable total.
        cacheIterator->bytes = result.bytes;
        cacheIterator->partial = result.partial;
        cacheIterator->state =
            result.unavailable
                ? CacheState::Unavailable
                : CacheState::Ready;
        // Completed entries no longer need their shared cancellation token.
        cacheIterator->cancellation.reset();

        // Notify every proxy-backed tree that this source Size cell changed.
        refreshSizeCell(task.path);
    }

    // Releasing one slot may allow the next valid FIFO item to begin.
    startQueuedTasks();
}

void DirectorySizeFileSystemModel::refreshSizeCell(const QString &path)
{
    // Resolve the path again rather than retaining a QModelIndex across an
    // asynchronous job; filesystem model indexes may change during that time.
    QModelIndex nameIndex = index(path);

    if (!nameIndex.isValid())
    {
        return;
    }

    // The path lookup returns column 0. Move horizontally to the Size column
    // while retaining the same row and parent.
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
    // New children change every cached ancestor total containing the parent.
    connect(
        this,
        &QFileSystemModel::rowsInserted,
        this,
        [this](const QModelIndex &parent, int, int)
        {
            invalidatePaths({filePath(parent)});
        }
    );

    // Removed children have the same ancestor impact as inserted children.
    connect(
        this,
        &QFileSystemModel::rowsRemoved,
        this,
        [this](const QModelIndex &parent, int, int)
        {
            invalidatePaths({filePath(parent)});
        }
    );

    // A move changes totals under both the source and destination branches.
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

    // QFileSystemModel reports a rename as a directory plus old/new leaf names.
    // Reconstruct both paths so stale subtree keys and parent totals are cleared.
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

    // Metadata or content changes can affect a file's size. Convert every row
    // in the reported range back to column 0 paths before invalidating.
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
                // Ignore the Size-cell repaint emitted by refreshSizeCell(); it
                // represents a cache result, not a new filesystem mutation.
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

    // A reset invalidates every QModelIndex and every assumption represented by
    // the cache. Passing all keys cancels workers and repaints loaded rows.
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
    // Empty paths remain empty so callers can reject them rather than allowing
    // QFileInfo to reinterpret them as the current working directory.
    if (path.isEmpty())
    {
        return QString();
    }

    // Convert relative spelling and redundant separators into a stable absolute
    // cache key without resolving symlinks into their targets.
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
    // Validate the requested root before allocating traversal state. A root
    // symlink remains unavailable by design because links are never followed.
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

    // A mounted Windows drive can contain millions of files plus filesystem
    // metadata that is not meaningfully represented by summing visible file
    // entries. Walking the whole volume can therefore leave its Size cell on
    // "..." for a very long time and still disagree with Windows drive usage.
    //
    // QStorageInfo reads the filesystem's accounting directly. For an actual
    // NTFS/FAT/exFAT mount root, total minus available bytes matches the used
    // space reported by tools such as df and Windows drive properties. This
    // is constant-time and still runs in the worker so no storage query is
    // introduced into the UI thread.
    // QStorageInfo identifies both the containing filesystem and its mount root.
    // Subdirectories on the same filesystem must still use recursive totals.
    QStorageInfo storage(path);
    QByteArray filesystemType =
        storage.fileSystemType().toLower();
    QString storageRoot =
        QDir::cleanPath(storage.rootPath());
    QString requestedPath =
        QDir::cleanPath(rootInfo.absoluteFilePath());

    // Linux drivers report several names for Windows-compatible filesystems.
    // Matching only these formats prevents native Linux mount roots from
    // unexpectedly changing from folder totals to whole-filesystem usage.
    bool isWindowsFilesystem =
        filesystemType == "ntfs" ||
        filesystemType == "ntfs3" ||
        filesystemType == "fuseblk" ||
        filesystemType == "exfat" ||
        filesystemType == "vfat" ||
        filesystemType == "fat" ||
        filesystemType == "fat32";

    if (
        storage.isValid() &&
        storage.isReady() &&
        isWindowsFilesystem &&
        requestedPath == storageRoot
    )
    {
        // bytesAvailable() is the space available to the current user. On these
        // Windows filesystems it matches the used-space accounting requested by
        // the application: total capacity minus currently available capacity.
        qint64 totalBytes = storage.bytesTotal();
        qint64 availableBytes = storage.bytesAvailable();

        // Defensive bounds protect the displayed result if a filesystem
        // driver temporarily returns incomplete capacity information.
        if (
            totalBytes >= 0 &&
            availableBytes >= 0 &&
            availableBytes <= totalBytes
        )
        {
            result.bytes = totalBytes - availableBytes;
            return result;
        }
    }

    // Ordinary directories retain recursive logical-size behavior. The
    // explicit stack avoids call-stack growth on deeply nested Windows trees.
    QStringList pendingDirectories = {path};

    while (!pendingDirectories.isEmpty())
    {
        // Check once before opening each directory so invalidated large scans
        // can abandon an entire remaining subtree promptly.
        if (cancellation->load())
        {
            result.cancelled = true;
            return result;
        }

        // Taking from the end makes the QStringList an explicit depth-first
        // stack while keeping recursion off the C++ call stack.
        QString directoryPath = pendingDirectories.takeLast();
        QFileInfo directoryInfo(directoryPath);

        if (
            !directoryInfo.exists() ||
            !directoryInfo.isDir() ||
            !directoryInfo.isReadable()
        )
        {
            // A nested path may disappear or become unreadable during a scan.
            // Continue with accessible siblings and mark the final total '~'.
            result.partial = true;
            continue;
        }

        // Include hidden and system entries because they consume space, omit
        // '.' and '..', and never return symlinks that could form cycles or
        // count a target through multiple paths.
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
            // Per-entry checks keep cancellation responsive even inside a very
            // large directory that has no nested children.
            if (cancellation->load())
            {
                result.cancelled = true;
                return result;
            }

            if (entry.isDir())
            {
                // Defer child traversal by pushing its absolute path onto the
                // worker-local stack. No model or UI object enters the worker.
                pendingDirectories.append(entry.absoluteFilePath());
                continue;
            }

            if (!entry.isFile())
            {
                // Sockets, devices, and other special entries do not contribute
                // regular-file logical bytes and cannot be recursively entered.
                continue;
            }

            // QFileInfo::size() supplies logical length rather than allocated
            // blocks, matching the feature's ordinary-directory size definition.
            qint64 fileSize = entry.size();
            qint64 maximumSize = std::numeric_limits<qint64>::max();

            if (result.bytes > maximumSize - fileSize)
            {
                // Saturate instead of allowing signed overflow on unusually
                // large trees. Additional files leave the total at qint64 max.
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
    // Traditional units produce familiar binary-scaled labels such as KiB,
    // MiB, and GiB while QLocale supplies user-appropriate number formatting.
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
    // Equality is the simplest overlap: the changed item is itself cached.
    if (firstPath == secondPath)
    {
        return true;
    }

    // Appending a separator makes the relationship component-aware, so a path
    // such as /home/user does not incorrectly overlap /home/username. Root
    // already ends in '/', so it must not receive a second separator.
    QString firstPrefix =
        firstPath == "/" ? firstPath : firstPath + '/';
    QString secondPrefix =
        secondPath == "/" ? secondPath : secondPath + '/';

    // Either path may be the changed descendant or the cached ancestor. Cache
    // invalidation intentionally removes both directions of the relationship.
    return
        firstPath.startsWith(secondPrefix) ||
        secondPath.startsWith(firstPrefix);
}