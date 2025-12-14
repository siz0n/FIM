#include "FileIntegrityEngine.h"

#include <algorithm>
#include <unordered_map>

namespace core {

FileIntegrityEngine::FileIntegrityEngine() = default;

void FileIntegrityEngine::setConfig(Config config) { m_config = std::move(config); }

void FileIntegrityEngine::setStorage(std::shared_ptr<IStorage> storage) { m_storage = std::move(storage); }

void FileIntegrityEngine::setHasher(IHasher *hasher) { m_hasher = hasher; }

ScanResult FileIntegrityEngine::runScan() {
    ScanResult result{};
    if (!m_storage || !m_hasher) {
        return result;
    }

    FileScanner scanner(m_config, *m_hasher);
    auto newState = scanner.scan();
    auto oldState = m_storage->loadCurrentState();
    auto summary = compareAndPersist(newState, oldState);
    m_cachedState = std::move(newState);
    result.files = m_cachedState;
    result.overallStatus = summary.overallStatus();
    return result;
}

std::vector<FileMetadata> FileIntegrityEngine::getCurrentState() const { return m_cachedState; }

std::vector<HistoryEvent> FileIntegrityEngine::getHistory(int limit) const {
    return m_storage ? m_storage->loadHistory(limit) : std::vector<HistoryEvent>{};
}

ScanSummary FileIntegrityEngine::compareAndPersist(const std::vector<FileMetadata> &newState,
                                                   const std::vector<FileMetadata> &oldState) {
    ScanSummary summary{};
    if (!m_storage) {
        return summary;
    }

    std::unordered_map<std::string, FileMetadata> oldByPath;
    for (const auto &meta : oldState) {
        oldByPath.emplace(meta.path, meta);
    }

    std::vector<FileMetadata> merged = newState;

    for (auto &meta : merged) {
        summary.totalFiles++;
        const auto it = oldByPath.find(meta.path);
        FileStatus oldStatus = FileStatus::Ok;
        std::string oldHash;
        bool hasOld = false;
        if (it != oldByPath.end()) {
            oldStatus = it->second.status;
            oldHash = it->second.hash;
            hasOld = true;
        }

        if (meta.hash.empty()) {
            meta.status = FileStatus::Error;
            summary.errorCount++;
        } else if (!hasOld) {
            meta.status = FileStatus::New;
            summary.newCount++;
        } else {
            const auto &oldMeta = it->second;
            if (meta.hash != oldMeta.hash || meta.permissions != oldMeta.permissions || meta.owner != oldMeta.owner ||
                meta.group != oldMeta.group || meta.inode != oldMeta.inode || meta.mtime != oldMeta.mtime ||
                meta.size != oldMeta.size) {
                meta.status = FileStatus::Changed;
                summary.changedCount++;
            } else {
                meta.status = FileStatus::Ok;
            }
            oldByPath.erase(it);
        }

        if (hasOld && meta.status != FileStatus::Ok) {
            HistoryEvent rec;
            rec.filePath = meta.path;
            rec.oldStatus = static_cast<int>(oldStatus);
            rec.newStatus = static_cast<int>(meta.status);
            rec.oldHash = oldHash;
            rec.newHash = meta.hash;
            rec.scanTime = std::chrono::system_clock::now();
            m_storage->appendHistoryRecord(rec);
        }
    }

    for (const auto &remaining : oldByPath) {
        FileMetadata meta = remaining.second;
        meta.status = FileStatus::Deleted;
        merged.push_back(meta);
        summary.deletedCount++;
        HistoryEvent rec;
        rec.filePath = meta.path;
        rec.oldStatus = static_cast<int>(meta.status);
        rec.newStatus = static_cast<int>(FileStatus::Deleted);
        rec.oldHash = meta.hash;
        rec.scanTime = std::chrono::system_clock::now();
        m_storage->appendHistoryRecord(rec);
    }

    m_storage->saveCurrentState(merged);
    return summary;
}

} // namespace core
