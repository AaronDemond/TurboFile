#pragma once
#ifndef DIRECTORYSIZEFILESYSTEMMODEL_H
#define DIRECTORYSIZEFILESYSTEMMODEL_H

#include <QFileSystemModel>
#include <QHash>
#include <QQueue>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <memory>

// Extends QFileSystemModel with lazy background directory-size calculations.
// Ordinary directories use recursive logical bytes, Windows drive roots use
// filesystem-used space, and files retain QFileSystemModel's normal behavior.
class DirectorySizeFileSystemModel : public QFileSystemModel
{
    // Q_OBJECT enables this subclass to publish cache invalidation through a
    // Qt signal even though the background workers themselves are not QObjects.
    Q_OBJECT

public:
    explicit DirectorySizeFileSystemModel(QObject *parent = nullptr);
    ~DirectorySizeFileSystemModel() override;

    // Return cached directory sizes and request missing values without
    // performing filesystem traversal on the UI thread.
    QVariant data(
        const QModelIndex &index,
        int role = Qt::DisplayRole
    ) const override;

    // Report whether a directory has finished calculating or is known to be
    // unavailable. Files and symbolic links never require background work.
    bool directorySizeReady(const QModelIndex &index) const;

    // Return the raw completed byte count used by the sorting proxy.
    bool directorySize(
        const QModelIndex &index,
        qint64 *bytes
    ) const;

    // Explicitly request one directory when a pending Size sort needs every
    // sibling value, including rows that have not yet been painted.
    void requestDirectorySize(const QModelIndex &index);

    // Clear cached totals affected by successful filesystem mutations.
    void invalidatePaths(const QStringList &paths);

signals:
    // Active Size sorts must be cancelled when their cached inputs go stale.
    void directorySizesInvalidated();

private:
    // Each path moves through this small state machine. Pending entries own a
    // queued or active job, Ready entries contain a usable byte count, and
    // Unavailable entries completed without a value that can be sorted.
    enum class CacheState
    {
        Pending,
        Ready,
        Unavailable
    };

    struct CacheEntry
    {
        // New entries begin Pending and are replaced by one terminal state
        // when finishTask() accepts the corresponding worker result.
        CacheState state = CacheState::Pending;

        // The raw byte count remains numeric in the cache so sorting never
        // needs to parse the human-readable text shown in the tree view.
        qint64 bytes = 0;

        // Partial results are valid best-effort totals. data() marks them with
        // a leading '~' so users can distinguish them from complete totals.
        bool partial = false;

        // A monotonically increasing generation identifies the exact job
        // allowed to populate this path after invalidation and rescheduling.
        quint64 generation = 0;

        // The cache and worker share this atomic token. Invalidation can ask a
        // worker to stop without accessing worker-owned objects or blocking.
        std::shared_ptr<std::atomic_bool> cancellation;
    };

    // A task is a value-only snapshot placed in the FIFO. It deliberately
    // contains no model index because QModelIndex and model state belong to
    // the UI thread while the path can safely be used by a worker.
    struct SizeTask
    {
        QString path;
        quint64 generation = 0;
        std::shared_ptr<std::atomic_bool> cancellation;
    };

    // Workers return plain data through a queued UI-thread callback. Separate
    // flags preserve the difference between an approximate result, a failed
    // root, and a calculation intentionally abandoned after invalidation.
    struct SizeResult
    {
        qint64 bytes = 0;
        bool partial = false;
        bool unavailable = false;
        bool cancelled = false;
    };

    // Queue one calculation after data() discovers an uncached directory.
    void ensureDirectorySizeRequested(const QString &path);

    // Start queued work while respecting the disk-friendly concurrency cap.
    void startQueuedTasks();

    // Accept a worker result only if its cache generation is still current.
    void finishTask(
        const SizeTask &task,
        const SizeResult &result
    );

    // Repaint one Size cell after its cached state changes.
    void refreshSizeCell(const QString &path);

    // React to QFileSystemModel changes made outside TurboFile's actions.
    void connectFilesystemInvalidationSignals();

    // Normalize paths so every tab shares the same cache key.
    static QString normalizedPath(const QString &path);

    // Calculate recursive logical bytes or mounted-drive used space using
    // worker-local filesystem objects.
    static SizeResult calculateDirectorySize(
        const QString &path,
        const std::shared_ptr<std::atomic_bool> &cancellation
    );

    // Format a byte total using the user's locale and binary units.
    static QString formattedSize(qint64 bytes);

    // Determine whether two paths have an ancestor/descendant relationship.
    static bool pathsOverlap(
        const QString &firstPath,
        const QString &secondPath
    );

    // data() is const because it overrides QAbstractItemModel::data(), but it
    // performs read-only cache lookups before queueing mutations to the UI
    // thread. The cache is mutable solely to permit those const lookups.
    mutable QHash<QString, CacheEntry> sizeCache;

    // Waiting tasks remain in insertion order so visible rows are generally
    // calculated in the same order that their Size cells were requested.
    QQueue<SizeTask> queuedTasks;

    // This model owns a private pool instead of using Qt's global pool, which
    // prevents slow disk scans from consuming threads needed by other work.
    QThreadPool workerPool;

    // Generations never repeat during one model lifetime, allowing completed
    // workers to prove that their cache entry has not since been replaced.
    quint64 nextGeneration = 1;

    // Only the UI thread changes queue and cache bookkeeping. Workers return
    // results through queued calls rather than mutating these members.
    int activeTaskCount = 0;

    // The source model also listens to dataChanged for external filesystem
    // updates. This guard identifies the repaint signal emitted by this class.
    bool emittingSizeChange = false;

    // Destruction sets this before cancelling work so no new jobs can leave
    // the FIFO while the worker pool is being drained.
    bool shuttingDown = false;
};

#endif // DIRECTORYSIZEFILESYSTEMMODEL_H