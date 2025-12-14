#include "FileMonitor.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QByteArrayView>
#include <QSet>
#include <QPair>
#include <QObject>
#include <QStringList>
#include <QDebug>
#include <utility>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

FileMonitor::FileMonitor(DatabaseManager &databaseManager, QString scannerVersion)
    : m_databaseManager(databaseManager), m_scannerVersion(std::move(scannerVersion)) {}

QString FileMonitor::calculateHash(const QString &filePath, QString *errorReason) const {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorReason) {
            errorReason->clear();
            if (file.error() == QFileDevice::PermissionsError) {
                *errorReason = QObject::tr("Недостаточно прав (Permission denied)");
            } else {
                *errorReason = file.errorString();
            }
        }
        return {};
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    constexpr qint64 bufferSize = 1024 * 1024; // 1 MB
    QByteArray buffer;
    buffer.resize(bufferSize);

    while (!file.atEnd()) {
        const qint64 bytesRead = file.read(buffer.data(), bufferSize);
        if (bytesRead > 0) {
            hasher.addData(QByteArrayView(buffer.constData(), bytesRead));
        }
    }

    return hasher.result().toHex();
}

QVector<FileRecordEntry> FileMonitor::scanDirectory(const QString &directoryPath,
                                                   bool recursive,
                                                   bool followSymlinks,
                                                   int maxDepth) {
    QVector<FileRecordEntry> results;
    QFileInfo info(directoryPath);

    if (!info.exists() || !info.isDir()) {
        return results;
    }

    m_seenInodes.clear();
    QSet<QString> seenPaths;
    QSet<QString> visitedDirs;
    int permissionDeniedCount = 0;

    const auto existingRecords = m_databaseManager.fetchAllRecords();
    const QString basePath = QDir(directoryPath).absolutePath();
    const QString baseWithSep = basePath.endsWith(QDir::separator()) ? basePath : basePath + QDir::separator();

    if (!m_databaseManager.beginTransaction()) {
        FileRecordEntry failure;
        failure.status = QObject::tr("Error");
        failure.errorReason = m_databaseManager.lastError();
        results.append(failure);
        return results;
    }

    QList<QPair<QFileInfo, int>> stack;
    stack.append({QFileInfo(directoryPath), 0});

    while (!stack.isEmpty()) {
        const auto current = stack.takeLast();
        const QFileInfo currentInfo = current.first;
        const int depth = current.second;
        if (!currentInfo.isDir()) {
            continue;
        }

        const QString currentPath = currentInfo.absoluteFilePath();
        if (visitedDirs.contains(currentPath)) {
            continue;
        }
        visitedDirs.insert(currentPath);

        const QFileInfoList entries = QDir(currentPath).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries,
                                                                     QDir::Name | QDir::DirsFirst);
        for (const QFileInfo &entry : entries) {
            const QString filePath = entry.absoluteFilePath();

            if (isExcluded(filePath)) {
                continue;
            }

            if (entry.isSymLink() && entry.isDir()) {
                if (!followSymlinks) {
                    continue;
                }
                const QString target = QFileInfo(entry.symLinkTarget()).absoluteFilePath();
                if (visitedDirs.contains(target)) {
                    continue;
                }
            }

            if (entry.isDir()) {
                if (!recursive) {
                    continue;
                }
                if (maxDepth >= 0 && depth + 1 > maxDepth) {
                    continue;
                }
                stack.append({entry, depth + 1});
                continue;
            }

#ifdef Q_OS_UNIX
            struct stat st { };
            if (::lstat(filePath.toUtf8().constData(), &st) == 0) {
                if (!S_ISREG(st.st_mode)) {
                    continue;
                }
            }
#else
            if (!entry.isFile() || entry.isSymLink()) {
                continue;
            }
#endif

            FileRecordEntry record;
            record.metadata = buildMetadata(filePath);
            record.updatedAt = QDateTime::currentDateTimeUtc();
            record.lastChecked = record.updatedAt;
            record.scannerVersion = m_scannerVersion;

            if (record.metadata.hash.isEmpty()) {
                record.status = QStringLiteral("Error");
                if (record.metadata.errorReason.isEmpty()) {
                    record.errorReason = QObject::tr("Не удалось прочитать файл");
                } else {
                    record.errorReason = record.metadata.errorReason;
                    if (record.errorReason.contains(QStringLiteral("Permission denied"), Qt::CaseInsensitive)) {
                        record.errorReason = QObject::tr("Недостаточно прав (Permission denied)");
                        ++permissionDeniedCount;
                    }
                }
                record.scannerVersion += " (error_read)";
                results.append(record);
                continue;
            }

            const QString inodeKey = QStringLiteral("%1:%2").arg(record.metadata.device).arg(record.metadata.inode);
            if (record.metadata.inode != 0 && m_seenInodes.contains(inodeKey)) {
                continue;
            }
            m_seenInodes.insert(inodeKey);

            const FileRecordEntry oldRecord = m_databaseManager.fetchRecord(filePath);
            const QString oldHash = oldRecord.metadata.hash;
            record.previousHash = oldHash;
            const QString oldStatus = oldRecord.status.isEmpty() ? QStringLiteral("Ok") : oldRecord.status;
            const bool hasOldRecord = !oldRecord.metadata.path.isEmpty();
#ifdef QT_DEBUG
            qDebug() << "[DEBUG]" << filePath
                     << "file_mtime=" << record.metadata.mtimeSeconds
                     << "db_mtime=" << (hasOldRecord ? oldRecord.metadata.mtimeSeconds : static_cast<qint64>(-1));
#endif
            const bool signatureMismatch = hasOldRecord && !oldRecord.signatureValid && !oldRecord.signature.isEmpty();
            record.permissionsChanged = hasOldRecord && (oldRecord.metadata.permissions != record.metadata.permissions
                                                         || oldRecord.metadata.mode != record.metadata.mode);
            record.ownerChanged = hasOldRecord && (oldRecord.metadata.owner != record.metadata.owner
                                                   || oldRecord.metadata.groupName != record.metadata.groupName
                                                   || oldRecord.metadata.uid != record.metadata.uid
                                                   || oldRecord.metadata.gid != record.metadata.gid);
            record.mtimeChanged = hasOldRecord && oldRecord.metadata.mtimeSeconds != record.metadata.mtimeSeconds;
            record.inodeChanged = hasOldRecord && oldRecord.metadata.inode != record.metadata.inode;
            record.metadataChanged = record.permissionsChanged || record.ownerChanged || record.mtimeChanged || record.inodeChanged;

            if (!hasOldRecord) {
                record.status = QStringLiteral("New");
            } else if (signatureMismatch) {
                record.status = QStringLiteral("Changed");
            } else if (oldHash == record.metadata.hash && !record.metadataChanged) {
                record.status = QStringLiteral("Ok");
            } else {
                record.status = QStringLiteral("Changed");
            }

            const bool statusChanged = oldStatus != record.status;
            const bool hashChanged = oldHash != record.metadata.hash;

            if (!hasOldRecord) {
                if (!m_databaseManager.insertHistoryRecord(record.metadata.path,
                                                           -1,
                                                           statusCode(record.status),
                                                           oldRecord.metadata.hash,
                                                           record.metadata.hash,
                                                           QObject::tr("Новый файл обнаружен"))) {
                    m_databaseManager.rollbackTransaction();
                    record.status = QStringLiteral("Error");
                    record.errorReason = m_databaseManager.lastError();
                    results.append(record);
                    return results;
                }
            } else if (statusChanged || hashChanged) {
                if (!m_databaseManager.insertHistoryRecord(record.metadata.path,
                                                           statusCode(oldStatus),
                                                           statusCode(record.status),
                                                           oldRecord.metadata.hash,
                                                           record.metadata.hash,
                                                           QString())) {
                    m_databaseManager.rollbackTransaction();
                    record.status = QStringLiteral("Error");
                    record.errorReason = m_databaseManager.lastError();
                    results.append(record);
                    return results;
                }
            }

            if (!hasOldRecord || statusChanged || hashChanged || record.metadataChanged) {
                if (!m_databaseManager.upsertFileRecord(record)) {
                    m_databaseManager.rollbackTransaction();
                    record.status = QStringLiteral("Error");
                    record.errorReason = m_databaseManager.lastError();
                    results.append(record);
                    return results;
                }
            }
            results.append(record);
            seenPaths.insert(QFileInfo(record.metadata.path).absoluteFilePath());
        }
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const auto &existing : existingRecords) {
        const QString absolutePath = QFileInfo(existing.metadata.path).absoluteFilePath();
        if (seenPaths.contains(absolutePath)) {
            continue;
        }
        if (!isPathInDirectory(absolutePath, baseWithSep)) {
            continue;
        }
        if (QFileInfo::exists(absolutePath)) {
            continue;
        }

        FileRecordEntry deleted = buildDeletedRecord(existing, now);
        const QString oldStatus = existing.status.isEmpty() ? QStringLiteral("Ok") : existing.status;
        const bool statusTransition = statusCode(oldStatus) != statusCode(deleted.status);
        if (statusTransition) {
            if (!m_databaseManager.insertHistoryRecord(deleted.metadata.path,
                                                       statusCode(oldStatus),
                                                       statusCode(deleted.status),
                                                       existing.metadata.hash,
                                                       deleted.metadata.hash,
                                                       QObject::tr("Файл удалён"))) {
                m_databaseManager.rollbackTransaction();
                deleted.status = QStringLiteral("Error");
                deleted.errorReason = m_databaseManager.lastError();
                results.append(deleted);
                return results;
            }
        }
        if (!m_databaseManager.upsertFileRecord(deleted)) {
            m_databaseManager.rollbackTransaction();
            deleted.status = QStringLiteral("Error");
            deleted.errorReason = m_databaseManager.lastError();
            results.append(deleted);
            return results;
        }
        results.append(deleted);
    }

    if (!m_databaseManager.commitTransaction()) {
        m_databaseManager.rollbackTransaction();
        FileRecordEntry failure;
        failure.status = QStringLiteral("Error");
        failure.errorReason = m_databaseManager.lastError();
        results.append(failure);
    }

#ifdef QT_DEBUG
    if (permissionDeniedCount > 0) {
        qDebug() << "Permission denied count" << permissionDeniedCount;
    }
#endif

    return results;
}

FileMetadata FileMonitor::buildMetadata(const QString &filePath) const {
    FileMetadata metadata;
    metadata.path = filePath;

    QFileInfo info(filePath);
    metadata.size = info.size();
    metadata.mtimeSeconds = info.lastModified().toSecsSinceEpoch();
    metadata.owner = info.owner();
    metadata.groupName = info.group();
    metadata.permissions = static_cast<quint64>(info.permissions());
#ifdef Q_OS_UNIX
    struct stat st;
    if (::lstat(filePath.toUtf8().constData(), &st) == 0) {
        metadata.uid = st.st_uid;
        metadata.gid = st.st_gid;
        metadata.mode = st.st_mode;
        metadata.device = st.st_dev;
        metadata.inode = st.st_ino;
        metadata.hardlinkCount = st.st_nlink;
    }
#endif
    QString errorReason;
    metadata.hash = calculateHash(filePath, &errorReason);
    if (!errorReason.isEmpty()) {
        metadata.errorReason = errorReason;
    }
    return metadata;
}

FileRecordEntry FileMonitor::buildDeletedRecord(const FileRecordEntry &existing, const QDateTime &timestamp) const {
    FileRecordEntry deleted = existing;
    deleted.status = QStringLiteral("Deleted");
    deleted.lastChecked = timestamp;
    deleted.updatedAt = timestamp;
    return deleted;
}

bool FileMonitor::isPathInDirectory(const QString &filePath, const QString &directoryPath) const {
    if (filePath == directoryPath || filePath.startsWith(directoryPath)) {
        return true;
    }

    QString normalizedDirectory = directoryPath;
    if (!normalizedDirectory.endsWith(QDir::separator())) {
        normalizedDirectory += QDir::separator();
    }
    return filePath.startsWith(normalizedDirectory);
}

bool FileMonitor::isExcluded(const QString &filePath) const {
    const QString normalized = QDir::cleanPath(filePath);
    const QString fileName = QFileInfo(normalized).fileName();

    for (const auto &rule : m_excludeRules) {
        if (rule.pattern.isEmpty()) {
            continue;
        }

        switch (rule.type) {
        case ExcludeType::Path: {
            const QString normalizedRule = QDir::cleanPath(rule.pattern);
            if (normalized == normalizedRule || normalized.startsWith(normalizedRule + QDir::separator())) {
                return true;
            }
            break;
        }
        case ExcludeType::Glob:
            if (QDir::match(rule.pattern, fileName)) {
                return true;
            }
            break;
        }
    }

    return false;
}

int FileMonitor::statusCode(const QString &status) const {
    if (status == QLatin1String("Changed")) {
        return 1;
    }
    if (status == QLatin1String("New")) {
        return 2;
    }
    if (status == QLatin1String("Deleted")) {
        return 3;
    }
    if (status == QLatin1String("Error")) {
        return 4;
    }
    return 0;
}
