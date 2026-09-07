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

// Extends QFileSystemModel with lazily calculated recursive directory sizes.
// Files keep QFileSystemModel's normal Size-column behavior.
class DirectorySizeFileSystemModel : public QFileSystemModel
{
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

    // Return the raw recursive byte count used by the sorting proxy.
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
    enum class CacheState
    {
        Pending,
        Ready,
        Unavailable
    };

    struct CacheEntry
    {
        CacheState state = CacheState::Pending;
        qint64 bytes = 0;
        bool partial = false;
        quint64 generation = 0;
        std::shared_ptr<std::atomic_bool> cancellation;
    };

    struct SizeTask
    {
        QString path;
        quint64 generation = 0;
        std::shared_ptr<std::atomic_bool> cancellation;
    };

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

    // Calculate logical file bytes using worker-local filesystem objects.
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

    mutable QHash<QString, CacheEntry> sizeCache;
    QQueue<SizeTask> queuedTasks;
    QThreadPool workerPool;
    quint64 nextGeneration = 1;
    int activeTaskCount = 0;
    bool emittingSizeChange = false;
    bool shuttingDown = false;
};

#endif // DIRECTORYSIZEFILESYSTEMMODEL_H