#pragma once

#include "FileStatus.h"

#include <string>
#include <vector>

namespace core {

enum class ExcludeType {
    Path,
    Glob
};

struct ExcludeRule {
    ExcludeType type{ExcludeType::Path};
    std::string pattern;
};

struct Config {
    std::vector<std::string> directories;
    std::vector<ExcludeRule> excludeRules;
    bool recursive = true;
    bool followSymlinks = false;
    int maxDepth = 20; // <0 disables the limit
};

}
