#pragma once

#include "FileMetadata.h"
#include "FileStatus.h"

#include <vector>

namespace core {

struct HistoryEvent {
    std::chrono::system_clock::time_point scanTime{};
    std::string filePath;
    int oldStatus = -1;
    int newStatus = -1;
    std::string oldHash;
    std::string newHash;
    std::string comment;
};

struct ScanResult {
    std::vector<FileMetadata> files;
    FileStatus overallStatus = FileStatus::Ok;
};

class IStorage {
public:
    virtual ~IStorage() = default;
    virtual bool beginTransaction() = 0;
    virtual bool commitTransaction() = 0;
    virtual void rollbackTransaction() = 0;
    virtual std::vector<FileMetadata> loadCurrentState() = 0;
    virtual void saveCurrentState(const std::vector<FileMetadata> &files) = 0;
    virtual void appendHistoryRecord(const HistoryEvent &rec) = 0;
    virtual std::vector<HistoryEvent> loadHistory(int limit = 500) = 0;
};

}
