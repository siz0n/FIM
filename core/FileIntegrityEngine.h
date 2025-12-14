#pragma once

#include "Config.h"
#include "FileScanner.h"
#include "IStorage.h"
#include "ScanSummary.h"

#include <memory>
#include <vector>

namespace core {

class FileIntegrityEngine {
public:
    FileIntegrityEngine();

    void setConfig(Config config);
    void setStorage(std::shared_ptr<IStorage> storage);
    void setHasher(IHasher *hasher);

    ScanResult runScan();
    std::vector<FileMetadata> getCurrentState() const;
    std::vector<HistoryEvent> getHistory(int limit = 500) const;

private:
    ScanSummary compareAndPersist(const std::vector<FileMetadata> &newState,
                                  const std::vector<FileMetadata> &oldState);

    Config m_config;
    std::shared_ptr<IStorage> m_storage;
    IHasher *m_hasher = nullptr;
    std::vector<FileMetadata> m_cachedState;
};

}
