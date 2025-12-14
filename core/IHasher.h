#pragma once

#include <filesystem>
#include <string>

namespace core {

class IHasher {
public:
    virtual ~IHasher() = default;
    virtual std::string compute(const std::filesystem::path &path) = 0;
};

}
