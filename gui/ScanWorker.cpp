#include "ScanWorker.h"

#include <QCoreApplication>
#include <QThread>
#include <atomic>

namespace {
std::atomic<int> g_workerCounter{0};
}

ScanWorker::ScanWorker(const QString &databasePath,
                       const QByteArray &hmacKey,
                       const QVector<ExcludeRule> &rules,
                       bool recursive,
                       bool followSymlinks,
                       int maxDepth,
                       QObject *parent)
    : QObject(parent),
      m_databaseManager(databasePath, QStringLiteral("integrity_worker_%1").arg(++g_workerCounter)),
      m_fileMonitor(m_databaseManager),
      m_recursive(recursive),
      m_followSymlinks(followSymlinks),
      m_maxDepth(maxDepth) {
    m_databaseManager.setHmacKey(hmacKey);
    m_databaseManager.initialize();
    m_fileMonitor.setExcludeRules(rules);
}

void ScanWorker::startScan(const QStringList &directories) {
    try {
        QVector<FileRecordEntry> aggregated;
        int totalFiles = 0;
        int processedFiles = 0;

        for (const auto &dir : directories) {
            const auto results = m_fileMonitor.scanDirectory(dir, m_recursive, m_followSymlinks, m_maxDepth);
            totalFiles += results.size();
            emit progressChanged(processedFiles, totalFiles);
            for (const auto &rec : results) {
                ++processedFiles;
                emit progressChanged(processedFiles, totalFiles);
                emit fileProcessed(rec.metadata.path);
            }
            aggregated << results;
        }

        emit progressChanged(processedFiles, totalFiles);
        emit scanFinished(aggregated);
    } catch (const std::exception &ex) {
        emit scanError(QString::fromUtf8(ex.what()));
    } catch (...) {
        emit scanError(tr("Неизвестная ошибка при сканировании"));
    }
}
