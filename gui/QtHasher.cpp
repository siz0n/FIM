#include "QtHasher.h"

#include <QFile>

std::string QtHasher::compute(const std::filesystem::path &path) {
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    constexpr qint64 bufferSize = 1024 * 1024;
    QByteArray buffer;
    buffer.resize(bufferSize);

    while (!file.atEnd()) {
        const qint64 bytesRead = file.read(buffer.data(), bufferSize);
        if (bytesRead > 0) {
            hasher.addData(QByteArrayView(buffer.constData(), bytesRead));
        }
    }

    return hasher.result().toHex().toStdString();
}
