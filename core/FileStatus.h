#pragma once

#include <cstdint>

namespace core {

enum class FileStatus : std::uint8_t {
    Ok = 0,
    Changed = 1,
    New = 2,
    Deleted = 3,
    Error = 4
};

}
