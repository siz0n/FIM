#ifndef SCANWORKER_H
#define SCANWORKER_H

#include <QObject>
#include <QStringList>

#include "FileMonitor.h"

class ScanWorker : public QObject {
    Q_OBJECT
public:
    ScanWorker(const QString &databasePath,
               const QByteArray &hmacKey,
               const QVector<ExcludeRule> &rules,
               bool recursive,
               bool followSymlinks,
               int maxDepth,
               QObject *parent = nullptr);

public slots:
    void startScan(const QStringList &directories);

signals:
    void progressChanged(int current, int total);
    void fileProcessed(const QString &path);
    void scanFinished(const QVector<FileRecordEntry> &results);
    void scanError(const QString &message);

private:
    DatabaseManager m_databaseManager;
    FileMonitor m_fileMonitor;
    bool m_recursive;
    bool m_followSymlinks;
    int m_maxDepth;
};

#endif // SCANWORKER_H
