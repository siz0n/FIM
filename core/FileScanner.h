#pragma once

#include "Config.h"
#include "FileMetadata.h"
#include "IHasher.h"

#include <filesystem>
#include <vector>

namespace core {

class FileScanner {
public:
    FileScanner(Config config, IHasher &hasher);
    std::vector<FileMetadata> scan() const;

private:
    bool isExcluded(const std::filesystem::path &path) const;
    FileMetadata buildMetadata(const std::filesystem::directory_entry &entry) const;

    Config m_config;
    IHasher &m_hasher;
};

}
