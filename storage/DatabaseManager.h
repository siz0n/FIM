#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QSqlDatabase>
#include <QVector>
#include <QString>
#include <QDateTime>
#include <QByteArray>
#include <QSqlQuery>

struct FileMetadata {
    QString path;
    QString hash;
    qint64 size = 0;
    qint64 mtimeSeconds = 0;
    quint32 uid = 0;
    quint32 gid = 0;
    quint32 mode = 0;
    quint64 device = 0;
    quint64 inode = 0;
    quint64 hardlinkCount = 0;
    quint64 permissions = 0;
    QString owner;
    QString groupName;
    QString errorReason;
};

struct FileRecordEntry {
    FileMetadata metadata;
    QString signature;
    QDateTime updatedAt;
    QDateTime lastChecked;
    QString scannerVersion;
    QString status;
    QString previousHash;
    QString errorReason;
    bool signatureValid = true;
    bool metadataChanged = false;
    bool permissionsChanged = false;
    bool ownerChanged = false;
    bool mtimeChanged = false;
    bool inodeChanged = false;
};

struct HistoryRecord {
    QDateTime scanTime;
    QString filePath;
    int oldStatus = -1;
    int newStatus = -1;
    QString oldHash;
    QString newHash;
    QString comment;
};

class DatabaseManager {
public:
    explicit DatabaseManager(const QString &databasePath,
                             QString connectionName = QStringLiteral("integrity_connection"));
    bool initialize();
    void setHmacKey(const QByteArray &key);
    bool upsertFileRecord(const FileRecordEntry &record);
    bool clearAllRecords();
    QString fetchHash(const QString &path) const;
    FileRecordEntry fetchRecord(const QString &path) const;
    QVector<FileRecordEntry> fetchAllRecords() const;
    bool insertHistoryRecord(const QString &filePath,
                             int oldStatus,
                             int newStatus,
                             const QString &oldHash,
                             const QString &newHash,
                             const QString &comment);
    QVector<HistoryRecord> fetchHistory(int limit = 500) const;
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();
    QString lastError() const { return m_lastError; }

private:
    bool ensureConnection() const;
    bool createTables() const;
    bool createHistoryTable() const;
    bool ensureSchemaVersion();
    bool setSchemaVersion(int version) const;
    QString computeSignature(const FileMetadata &metadata) const;
    FileRecordEntry hydrateRecord(QSqlQuery &query) const;
    bool verifySignature(const FileRecordEntry &record) const;

    QString m_databasePath;
    QString m_connectionName;
    mutable QSqlDatabase m_database;
    QByteArray m_hmacKey;
    mutable QString m_lastError;
};

#endif // DATABASEMANAGER_H
