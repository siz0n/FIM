#include "FileScanner.h"

#include <filesystem>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <set>

namespace core {

FileScanner::FileScanner(Config config, IHasher &hasher) : m_config(std::move(config)), m_hasher(hasher) {}

bool FileScanner::isExcluded(const std::filesystem::path &path) const {
    const auto normalized = std::filesystem::weakly_canonical(path).string();
    const auto fileName = path.filename().string();

    for (const auto &rule : m_config.excludeRules) {
        switch (rule.type) {
        case ExcludeType::Path:
            if (normalized.rfind(rule.pattern, 0) == 0) {
                return true;
            }
            break;
        case ExcludeType::Glob:
            // simple glob: support prefix/suffix with '*'
            if (!rule.pattern.empty()) {
                const auto &p = rule.pattern;
                if (p.front() == '*' && p.back() == '*') {
                    const auto needle = p.substr(1, p.size() - 2);
                    if (fileName.find(needle) != std::string::npos) {
                        return true;
                    }
                } else if (p.front() == '*') {
                    const auto needle = p.substr(1);
                    if (fileName.size() >= needle.size() && fileName.compare(fileName.size() - needle.size(), needle.size(), needle) == 0) {
                        return true;
                    }
                } else if (p.back() == '*') {
                    const auto needle = p.substr(0, p.size() - 1);
                    if (fileName.rfind(needle, 0) == 0) {
                        return true;
                    }
                } else if (fileName == p) {
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

FileMetadata FileScanner::buildMetadata(const std::filesystem::directory_entry &entry) const {
    FileMetadata meta;
    const auto path = entry.path();
    meta.path = path.string();
    meta.size = entry.file_size();
    const auto ftime = entry.last_write_time();
    const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
    meta.mtime = sctp;
    meta.permissions = static_cast<std::uint64_t>(entry.status().permissions());

#ifdef __unix__
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0) {
        meta.inode = static_cast<std::uint64_t>(st.st_ino);
        if (auto *pwd = ::getpwuid(st.st_uid)) {
            meta.owner = pwd->pw_name;
        }
        if (auto *grp = ::getgrgid(st.st_gid)) {
            meta.group = grp->gr_name;
        }
    }
#endif

    meta.hash = m_hasher.compute(path);
    return meta;
}

std::vector<FileMetadata> FileScanner::scan() const {
    std::vector<FileMetadata> files;
    for (const auto &dir : m_config.directories) {
        std::filesystem::path base(dir);
        if (!std::filesystem::exists(base) || !std::filesystem::is_directory(base)) {
            continue;
        }

        auto options = std::filesystem::directory_options::skip_permission_denied;
        if (m_config.followSymlinks) {
            options |= std::filesystem::directory_options::follow_directory_symlink;
        }

        if (m_config.recursive) {
            std::set<std::string> visited;
            for (auto it = std::filesystem::recursive_directory_iterator(base, options); it != std::filesystem::recursive_directory_iterator(); ++it) {
                if (m_config.maxDepth >= 0 && it.depth() > m_config.maxDepth) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_directory()) {
                    const auto canonical = std::filesystem::weakly_canonical(it->path()).string();
                    if (!visited.insert(canonical).second) {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (!it->is_regular_file()) {
                    continue;
                }
                if (isExcluded(it->path())) {
                    continue;
                }
                files.push_back(buildMetadata(*it));
            }
        } else {
            for (const auto &entry : std::filesystem::directory_iterator(base, options)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                if (isExcluded(entry.path())) {
                    continue;
                }
                files.push_back(buildMetadata(entry));
            }
        }
    }
    return files;
}

} // namespace core
