#pragma once

#include <QTime>
#include <QString>
#include <QtCore/qlogging.h>

#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

namespace AetherSDR {

class AsyncLogWriter {
public:
    struct Counters {
        quint64 queuedLines{0};
        quint64 writtenLines{0};
        quint64 droppedDebugInfoLines{0};
        quint64 droppedHighPriorityLines{0};
        quint64 maxQueueDepth{0};
        quint64 maxBatchSize{0};
    };

    AsyncLogWriter();
    ~AsyncLogWriter();

    AsyncLogWriter(const AsyncLogWriter&) = delete;
    AsyncLogWriter& operator=(const AsyncLogWriter&) = delete;

    bool start(const QString& path, bool mirrorToStderr);
    void shutdown();

    bool isRunning() const;

    void enqueue(QtMsgType type,
                 const QTime& timestamp,
                 const QString& category,
                 const QString& message);

    void flush();
    void clearLog();

    Counters counters() const;

private:
    struct LogMessage {
        QtMsgType type{QtDebugMsg};
        QTime timestamp;
        QString category;
        QString message;
    };

    enum class ItemKind {
        Log,
        Flush,
        Clear,
        Stop,
    };

    struct SyncPoint {
        std::mutex mutex;
        std::condition_variable cv;
        bool done{false};
    };

    struct QueueItem {
        ItemKind kind{ItemKind::Log};
        LogMessage log;
        std::shared_ptr<SyncPoint> sync;
    };

    void run(std::promise<bool> opened);
    bool enqueueControlAndWait(ItemKind kind);
    void markDone(const std::shared_ptr<SyncPoint>& sync);

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<QueueItem> m_queue;
    std::thread m_worker;

    QString m_filePath;
    bool m_mirrorToStderr{false};
    bool m_started{false};
    bool m_accepting{false};
    bool m_stopping{false};

    Counters m_counters;
    quint64 m_pendingDroppedDebugInfo{0};
    quint64 m_pendingDroppedHighPriority{0};
};

} // namespace AetherSDR
