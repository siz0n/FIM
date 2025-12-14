#ifndef FILEMONITOR_H
#define FILEMONITOR_H

#include "DatabaseManager.h"

#include <QVector>
#include <QString>
#include <QDateTime>
#include <QSet>
#include <QDir>

enum class ExcludeType {
    Path,
    Glob
};

struct ExcludeRule {
    ExcludeType type = ExcludeType::Path;
    QString pattern;
};

class FileMonitor {
public:
    explicit FileMonitor(DatabaseManager &databaseManager, QString scannerVersion = QStringLiteral("1.0.0"));

    QVector<FileRecordEntry> scanDirectory(const QString &directoryPath,
                                           bool recursive = true,
                                           bool followSymlinks = false,
                                           int maxDepth = 20);
    QString calculateHash(const QString &filePath, QString *errorReason = nullptr) const;
    void setExcludeRules(const QVector<ExcludeRule> &rules) { m_excludeRules = rules; }
    bool isExcluded(const QString &filePath) const;

private:
    FileMetadata buildMetadata(const QString &filePath) const;
    FileRecordEntry buildDeletedRecord(const FileRecordEntry &existing, const QDateTime &timestamp) const;
    bool isPathInDirectory(const QString &filePath, const QString &directoryPath) const;
    int statusCode(const QString &status) const;

    DatabaseManager &m_databaseManager;
    QString m_scannerVersion;
    mutable QSet<QString> m_seenInodes;
    QVector<ExcludeRule> m_excludeRules;
};

#endif // FILEMONITOR_H
