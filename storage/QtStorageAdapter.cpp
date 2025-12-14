#include "QtStorageAdapter.h"

#include <QDateTime>
#include <QDebug>
#include <chrono>

namespace {
core::FileStatus fromString(const QString &status) {
    if (status.compare("Changed", Qt::CaseInsensitive) == 0) return core::FileStatus::Changed;
    if (status.compare("New", Qt::CaseInsensitive) == 0) return core::FileStatus::New;
    if (status.compare("Deleted", Qt::CaseInsensitive) == 0) return core::FileStatus::Deleted;
    if (status.compare("Error", Qt::CaseInsensitive) == 0) return core::FileStatus::Error;
    if (status.compare("Unchanged", Qt::CaseInsensitive) == 0) return core::FileStatus::Ok;
    return core::FileStatus::Ok;
}

QString toString(core::FileStatus status) {
    switch (status) {
    case core::FileStatus::Changed: return "Changed";
    case core::FileStatus::New: return "New";
    case core::FileStatus::Deleted: return "Deleted";
    case core::FileStatus::Error: return "Error";
    default: return "Ok";
    }
}

    std::chrono::system_clock::time_point toChrono(const QDateTime &dt) {
        return std::chrono::system_clock::from_time_t(dt.toSecsSinceEpoch());
    }
}

QtStorageAdapter::QtStorageAdapter(std::shared_ptr<DatabaseManager> db) : m_db(std::move(db)) {}

bool QtStorageAdapter::beginTransaction() { return m_db->beginTransaction(); }

bool QtStorageAdapter::commitTransaction() { return m_db->commitTransaction(); }

void QtStorageAdapter::rollbackTransaction() { m_db->rollbackTransaction(); }

std::vector<core::FileMetadata> QtStorageAdapter::loadCurrentState() {
    std::vector<core::FileMetadata> result;
    const auto records = m_db->fetchAllRecords();
    result.reserve(records.size());
    for (const auto &rec : records) {
        core::FileMetadata meta;
        meta.path = rec.metadata.path.toStdString();
        meta.hash = rec.metadata.hash.toStdString();
        meta.size = static_cast<std::uint64_t>(rec.metadata.size);
        meta.permissions = rec.metadata.permissions;
        meta.owner = rec.metadata.owner.toStdString();
        meta.group = rec.metadata.groupName.toStdString();
        meta.inode = rec.metadata.inode;
        meta.mtime = std::chrono::system_clock::from_time_t(rec.metadata.mtimeSeconds);
        meta.status = fromString(rec.status);
        result.push_back(std::move(meta));
    }
    return result;
}

void QtStorageAdapter::saveCurrentState(const std::vector<core::FileMetadata> &files) {
    m_db->clearAllRecords();
    for (const auto &meta : files) {
        FileRecordEntry rec;
        rec.metadata.path = QString::fromStdString(meta.path);
        rec.metadata.hash = QString::fromStdString(meta.hash);
        rec.metadata.size = static_cast<qint64>(meta.size);
        rec.metadata.permissions = meta.permissions;
        rec.metadata.owner = QString::fromStdString(meta.owner);
        rec.metadata.groupName = QString::fromStdString(meta.group);
        rec.metadata.inode = meta.inode;
        rec.metadata.mtimeSeconds = std::chrono::system_clock::to_time_t(meta.mtime);
        rec.status = toString(meta.status);
        rec.updatedAt = QDateTime::currentDateTimeUtc();
        rec.lastChecked = rec.updatedAt;
        rec.signatureValid = true;
        m_db->upsertFileRecord(rec);
    }
}

void QtStorageAdapter::appendHistoryRecord(const core::HistoryEvent &rec) {
    const auto scanTime = QDateTime::fromSecsSinceEpoch(std::chrono::system_clock::to_time_t(rec.scanTime), Qt::UTC);
    const auto ok = m_db->insertHistoryRecord(QString::fromStdString(rec.filePath), rec.oldStatus, rec.newStatus,
                                              QString::fromStdString(rec.oldHash), QString::fromStdString(rec.newHash),
                                              QString::fromStdString(rec.comment));
    if (!ok) {
        qWarning() << "Failed to append history record for" << QString::fromStdString(rec.filePath);
    }
}

std::vector<core::HistoryEvent> QtStorageAdapter::loadHistory(int limit) {
    std::vector<core::HistoryEvent> result;
    const auto records = m_db->fetchHistory(limit);
    result.reserve(records.size());
    for (const auto &rec : records) {
        core::HistoryEvent h;
        h.scanTime = toChrono(rec.scanTime);
        h.filePath = rec.filePath.toStdString();
        h.oldStatus = rec.oldStatus;
        h.newStatus = rec.newStatus;
        h.oldHash = rec.oldHash.toStdString();
        h.newHash = rec.newHash.toStdString();
        h.comment = rec.comment.toStdString();
        result.push_back(std::move(h));
    }
    return result;
}
