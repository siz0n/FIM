#ifndef QTSTORAGEADAPTER_H
#define QTSTORAGEADAPTER_H

#include "DatabaseManager.h"
#include "core/IStorage.h"

#include <memory>

class QtStorageAdapter : public core::IStorage {
public:
    explicit QtStorageAdapter(std::shared_ptr<DatabaseManager> db);

    bool beginTransaction() override;
    bool commitTransaction() override;
    void rollbackTransaction() override;
    std::vector<core::FileMetadata> loadCurrentState() override;
    void saveCurrentState(const std::vector<core::FileMetadata> &files) override;
    void appendHistoryRecord(const core::HistoryEvent &rec) override;
    std::vector<core::HistoryEvent> loadHistory(int limit = 500) override;

private:
    std::shared_ptr<DatabaseManager> m_db;
};

#endif // QTSTORAGEADAPTER_H
