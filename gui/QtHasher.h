#ifndef QTHASHER_H
#define QTHASHER_H

#include "core/IHasher.h"

#include <QCryptographicHash>
#include <filesystem>

class QtHasher : public core::IHasher {
public:
    std::string compute(const std::filesystem::path &path) override;
};

#endif // QTHASHER_H
