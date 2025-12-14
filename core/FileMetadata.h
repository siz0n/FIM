#pragma once

#include "FileStatus.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace core {

struct FileMetadata {
    std::string path;
    std::string hash;
    std::uint64_t size = 0;
    std::chrono::system_clock::time_point mtime{};
    std::uint64_t permissions = 0;
    std::string owner;
    std::string group;
    std::uint64_t inode = 0;
    FileStatus status = FileStatus::Ok;
};

}
