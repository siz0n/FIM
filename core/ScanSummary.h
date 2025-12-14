#pragma once

#include "FileStatus.h"

#include <cstdint>

namespace core {

struct ScanSummary {
    std::uint64_t totalFiles = 0;
    std::uint64_t changedCount = 0;
    std::uint64_t newCount = 0;
    std::uint64_t deletedCount = 0;
    std::uint64_t errorCount = 0;

    FileStatus overallStatus() const {
        if (errorCount > 0) {
            return FileStatus::Error;
        }
        if (changedCount > 0 || newCount > 0 || deletedCount > 0) {
            return FileStatus::Changed;
        }
        return FileStatus::Ok;
    }
};

}
