#include "DatabaseManager.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QMetaType>
#include <QDebug>
#include <QCryptographicHash>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QObject>

namespace {
bool isReadonlyError(const QSqlError &error) {
    const QString text = error.databaseText().isEmpty() ? error.text() : error.databaseText();
    return text.contains(QStringLiteral("readonly"), Qt::CaseInsensitive);
}
}

DatabaseManager::DatabaseManager(const QString &databasePath, QString connectionName)
    : m_databasePath(databasePath),
      m_connectionName(std::move(connectionName)) {}

void DatabaseManager::setHmacKey(const QByteArray &key) {
    m_hmacKey = key;
}

bool DatabaseManager::ensureConnection() const {
    if (m_database.isValid() && m_database.isOpen()) {
        return true;
    }

    if (QSqlDatabase::contains(m_connectionName)) {
        m_database = QSqlDatabase::database(m_connectionName);
    } else {
        m_database = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    }

    m_database.setDatabaseName(m_databasePath);

    if (!m_database.open()) {
        m_lastError = m_database.lastError().text();
        qWarning() << "Failed to open database:" << m_lastError;
        return false;
    }

    m_lastError.clear();
    return true;
}

bool DatabaseManager::createTables() const {
    QSqlQuery query(m_database);
    const QString createTableSql = R"(
        CREATE TABLE IF NOT EXISTS files (
            path TEXT PRIMARY KEY,
            hash TEXT NOT NULL,
            size INTEGER NOT NULL,
            mtime INTEGER NOT NULL,
            uid INTEGER NOT NULL,
            gid INTEGER NOT NULL,
            mode INTEGER NOT NULL,
            device INTEGER NOT NULL,
            inode INTEGER NOT NULL,
            hardlink_count INTEGER NOT NULL,
            permissions INTEGER,
            owner TEXT,
            group_name TEXT,
            status TEXT NOT NULL DEFAULT 'Ok',
            signature TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            last_checked TEXT NOT NULL,
            scanner_version TEXT NOT NULL
        );
    )";

    if (!query.exec(createTableSql)) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to create tables:" << m_lastError;
        return false;
    }

    // Backward-compatibility: ensure the status column exists for older databases.
    if (!query.exec(QStringLiteral("PRAGMA table_info(files);"))) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to inspect table schema:" << m_lastError;
        return false;
    }

    bool hasStatus = false;
    bool hasPermissions = false;
    bool hasOwner = false;
    bool hasGroupName = false;
    while (query.next()) {
        if (query.value(1).toString() == QLatin1String("status")) {
            hasStatus = true;
        } else if (query.value(1).toString() == QLatin1String("permissions")) {
            hasPermissions = true;
        } else if (query.value(1).toString() == QLatin1String("owner")) {
            hasOwner = true;
        } else if (query.value(1).toString() == QLatin1String("group_name")) {
            hasGroupName = true;
        }
    }

    if (!hasStatus) {
        QSqlQuery alter(m_database);
        if (!alter.exec(QStringLiteral("ALTER TABLE files ADD COLUMN status TEXT NOT NULL DEFAULT 'Unchanged';"))) {
            m_lastError = alter.lastError().text();
            qWarning() << "Failed to add status column:" << m_lastError;
            return false;
        }
    }

    if (!hasPermissions) {
        QSqlQuery alter(m_database);
        if (!alter.exec(QStringLiteral("ALTER TABLE files ADD COLUMN permissions INTEGER;"))) {
            m_lastError = alter.lastError().text();
            qWarning() << "Failed to add permissions column:" << m_lastError;
            return false;
        }
    }

    if (!hasOwner) {
        QSqlQuery alter(m_database);
        if (!alter.exec(QStringLiteral("ALTER TABLE files ADD COLUMN owner TEXT;"))) {
            m_lastError = alter.lastError().text();
            qWarning() << "Failed to add owner column:" << m_lastError;
            return false;
        }
    }

    if (!hasGroupName) {
        QSqlQuery alter(m_database);
        if (!alter.exec(QStringLiteral("ALTER TABLE files ADD COLUMN group_name TEXT;"))) {
            m_lastError = alter.lastError().text();
            qWarning() << "Failed to add group_name column:" << m_lastError;
            return false;
        }
    }

    const QList<QPair<QString, QString>> statusMigrations = {
        {QStringLiteral("Unchanged"), QStringLiteral("Ok")},
        {QStringLiteral("Modified"), QStringLiteral("Changed")},
        {QStringLiteral("MetaChanged"), QStringLiteral("Changed")},
        {QStringLiteral("Failed"), QStringLiteral("Error")},
        {QStringLiteral("SignatureError"), QStringLiteral("Error")}
    };

    for (const auto &pair : statusMigrations) {
        QSqlQuery migrate(m_database);
        migrate.prepare(QStringLiteral("UPDATE files SET status = :newStatus WHERE status = :oldStatus;"));
        migrate.bindValue(":newStatus", pair.second);
        migrate.bindValue(":oldStatus", pair.first);
        if (!migrate.exec()) {
            m_lastError = migrate.lastError().text();
            qWarning() << "Failed to migrate statuses" << pair.first << "->" << pair.second << m_lastError;
        }
    }

    return createHistoryTable();
}

bool DatabaseManager::createHistoryTable() const {
    QSqlQuery query(m_database);
    const QString createHistorySql = R"(
        CREATE TABLE IF NOT EXISTS scan_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            scan_time TEXT NOT NULL,
            file_path TEXT NOT NULL,
            old_status INTEGER,
            new_status INTEGER NOT NULL,
            old_hash TEXT,
            new_hash TEXT,
            comment TEXT
        );
    )";

    if (!query.exec(createHistorySql)) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to create history table:" << m_lastError;
        return false;
    }

    return true;
}

bool DatabaseManager::initialize() {
    if (!ensureConnection()) {
        return false;
    }

    if (!createTables()) {
        return false;
    }

    return ensureSchemaVersion();
}

bool DatabaseManager::upsertFileRecord(const FileRecordEntry &record) {
    if (!ensureConnection()) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(R"(
        INSERT INTO files (path, hash, size, mtime, uid, gid, mode, device, inode, hardlink_count, permissions, owner, group_name, status, signature, updated_at, last_checked, scanner_version)
        VALUES (:path, :hash, :size, :mtime, :uid, :gid, :mode, :device, :inode, :hardlink_count, :permissions, :owner, :group_name, :status, :signature, :updated_at, :last_checked, :scanner_version)
        ON CONFLICT(path) DO UPDATE SET
            hash = excluded.hash,
            size = excluded.size,
            mtime = excluded.mtime,
            uid = excluded.uid,
            gid = excluded.gid,
            mode = excluded.mode,
            device = excluded.device,
            inode = excluded.inode,
            hardlink_count = excluded.hardlink_count,
            permissions = excluded.permissions,
            owner = excluded.owner,
            group_name = excluded.group_name,
            status = excluded.status,
            signature = excluded.signature,
            updated_at = excluded.updated_at,
            last_checked = excluded.last_checked,
            scanner_version = excluded.scanner_version;
    )");

    const QString signature = computeSignature(record.metadata);

    query.bindValue(":path", record.metadata.path);
    query.bindValue(":hash", record.metadata.hash);
    query.bindValue(":size", record.metadata.size);
    query.bindValue(":mtime", record.metadata.mtimeSeconds);
    query.bindValue(":uid", record.metadata.uid);
    query.bindValue(":gid", record.metadata.gid);
    query.bindValue(":mode", record.metadata.mode);
    query.bindValue(":device", QVariant::fromValue(static_cast<qulonglong>(record.metadata.device)));
    query.bindValue(":inode", QVariant::fromValue(static_cast<qulonglong>(record.metadata.inode)));
    query.bindValue(":hardlink_count", QVariant::fromValue(static_cast<qulonglong>(record.metadata.hardlinkCount)));
    query.bindValue(":permissions", QVariant::fromValue(static_cast<qulonglong>(record.metadata.permissions)));
    query.bindValue(":owner", record.metadata.owner);
    query.bindValue(":group_name", record.metadata.groupName);
    query.bindValue(":status", record.status);
    query.bindValue(":signature", signature);
    query.bindValue(":updated_at", record.updatedAt.toString(Qt::ISODate));
    query.bindValue(":last_checked", record.lastChecked.toString(Qt::ISODate));
    query.bindValue(":scanner_version", record.scannerVersion);

    if (!query.exec()) {
        const auto error = query.lastError();
        m_lastError = error.text();
        if (isReadonlyError(error)) {
            m_lastError = QObject::tr(
                "База данных доступна только для чтения. Проверьте права на файл или путь к базе.");
        }
        qWarning() << "Failed to upsert file record:" << m_lastError;
        return false;
    }

    return true;
}

bool DatabaseManager::clearAllRecords() {
    if (!ensureConnection()) {
        return false;
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("DELETE FROM files;"))) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to clear records:" << m_lastError;
        return false;
    }

    if (!query.exec(QStringLiteral("DELETE FROM scan_history;"))) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to clear history:" << m_lastError;
        return false;
    }

    return true;
}

QString DatabaseManager::fetchHash(const QString &path) const {
    const FileRecordEntry rec = fetchRecord(path);
    if (!rec.signatureValid) {
        qWarning() << "Signature mismatch for" << path;
    }
    return rec.metadata.hash;
}

FileRecordEntry DatabaseManager::hydrateRecord(QSqlQuery &query) const {
    FileRecordEntry record;
    record.metadata.path = query.value(0).toString();
    record.metadata.hash = query.value(1).toString();
    record.metadata.size = query.value(2).toLongLong();
    record.metadata.mtimeSeconds = query.value(3).toLongLong();
    record.metadata.uid = query.value(4).toUInt();
    record.metadata.gid = query.value(5).toUInt();
    record.metadata.mode = query.value(6).toUInt();
    record.metadata.device = query.value(7).toULongLong();
    record.metadata.inode = query.value(8).toULongLong();
    record.metadata.hardlinkCount = query.value(9).toULongLong();
    record.metadata.permissions = query.value(10).toULongLong();
    record.metadata.owner = query.value(11).toString();
    record.metadata.groupName = query.value(12).toString();
    record.previousHash = record.metadata.hash;
    record.status = query.value(13).toString();
    record.signature = query.value(14).toString();
    record.updatedAt = QDateTime::fromString(query.value(15).toString(), Qt::ISODate);
    record.lastChecked = QDateTime::fromString(query.value(16).toString(), Qt::ISODate);
    record.scannerVersion = query.value(17).toString();
    record.signatureValid = verifySignature(record);
    return record;
}

FileRecordEntry DatabaseManager::fetchRecord(const QString &path) const {
    if (!ensureConnection()) {
        return {};
    }

    QSqlQuery query(m_database);
    query.prepare(R"(
        SELECT path, hash, size, mtime, uid, gid, mode, device, inode, hardlink_count, permissions, owner, group_name, status, signature, updated_at, last_checked, scanner_version
        FROM files WHERE path = :path LIMIT 1;
    )");
    query.bindValue(":path", path);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to fetch record:" << m_lastError;
        return {};
    }

    if (query.next()) {
        return hydrateRecord(query);
    }

    return {};
}

QVector<FileRecordEntry> DatabaseManager::fetchAllRecords() const {
    QVector<FileRecordEntry> records;

    if (!ensureConnection()) {
        return records;
    }

    QSqlQuery query(m_database);
    if (!query.exec(R"(
            SELECT path, hash, size, mtime, uid, gid, mode, device, inode, hardlink_count, permissions, owner, group_name, status, signature, updated_at, last_checked, scanner_version
            FROM files ORDER BY path ASC;
        )")) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to fetch records:" << m_lastError;
        return records;
    }

    while (query.next()) {
        records.append(hydrateRecord(query));
    }

    return records;
}

bool DatabaseManager::insertHistoryRecord(const QString &filePath,
                                          int oldStatus,
                                          int newStatus,
                                          const QString &oldHash,
                                          const QString &newHash,
                                          const QString &comment) {
    if (!ensureConnection()) {
        return false;
    }

    QSqlQuery query(m_database);
    query.prepare(R"(
        INSERT INTO scan_history (scan_time, file_path, old_status, new_status, old_hash, new_hash, comment)
        VALUES (:scan_time, :file_path, :old_status, :new_status, :old_hash, :new_hash, :comment);
    )");

    query.bindValue(":scan_time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.bindValue(":file_path", filePath);
    if (oldStatus < 0) {
        query.bindValue(":old_status", QVariant(QMetaType::fromType<int>()));
    } else {
        query.bindValue(":old_status", oldStatus);
    }
    query.bindValue(":new_status", newStatus);
    query.bindValue(":old_hash", oldHash);
    query.bindValue(":new_hash", newHash);
    query.bindValue(":comment", comment);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to insert history record:" << m_lastError;
        return false;
    }

    return true;
}

QVector<HistoryRecord> DatabaseManager::fetchHistory(int limit) const {
    QVector<HistoryRecord> history;

    if (!ensureConnection()) {
        return history;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT scan_time, file_path, old_status, new_status, old_hash, new_hash, comment "
        "FROM scan_history ORDER BY id DESC LIMIT :limit"));
    query.bindValue(":limit", limit);

    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to fetch history:" << m_lastError;
        return history;
    }

    while (query.next()) {
        HistoryRecord rec;
        rec.scanTime = QDateTime::fromString(query.value(0).toString(), Qt::ISODate);
        rec.filePath = query.value(1).toString();
        rec.oldStatus = query.value(2).isNull() ? -1 : query.value(2).toInt();
        rec.newStatus = query.value(3).toInt();
        rec.oldHash = query.value(4).toString();
        rec.newHash = query.value(5).toString();
        rec.comment = query.value(6).toString();
        history.append(rec);
    }

    return history;
}

bool DatabaseManager::beginTransaction() {
    if (!ensureConnection()) {
        return false;
    }

    if (!m_database.transaction()) {
        m_lastError = m_database.lastError().text();
        qWarning() << "Failed to start transaction:" << m_lastError;
        return false;
    }
    return true;
}

bool DatabaseManager::commitTransaction() {
    if (!ensureConnection()) {
        return false;
    }

    if (!m_database.commit()) {
        m_lastError = m_database.lastError().text();
        qWarning() << "Failed to commit transaction:" << m_lastError;
        return false;
    }
    return true;
}

void DatabaseManager::rollbackTransaction() {
    if (!m_database.isOpen()) {
        return;
    }
    if (!m_database.rollback()) {
        m_lastError = m_database.lastError().text();
        qWarning() << "Failed to rollback transaction:" << m_lastError;
    }
}

QString DatabaseManager::computeSignature(const FileMetadata &metadata) const {
    if (m_hmacKey.isEmpty()) {
        return {};
    }

    QByteArray payload = metadata.path.toUtf8();
    payload += '|' + QByteArray::number(metadata.size);
    payload += '|' + QByteArray::number(metadata.mtimeSeconds);
    payload += '|' + QByteArray::number(metadata.uid);
    payload += '|' + QByteArray::number(metadata.gid);
    payload += '|' + QByteArray::number(metadata.mode);
    payload += '|' + metadata.hash.toUtf8();

    QByteArray key = m_hmacKey;
    const int blockSize = 64;
    if (key.size() > blockSize) {
        key = QCryptographicHash::hash(key, QCryptographicHash::Sha256);
    }
    key = key.leftJustified(blockSize, '\0', true);

    QByteArray oKeyPad(blockSize, '\x5c');
    QByteArray iKeyPad(blockSize, '\x36');
    for (int i = 0; i < blockSize; ++i) {
        oKeyPad[i] = oKeyPad[i] ^ key[i];
        iKeyPad[i] = iKeyPad[i] ^ key[i];
    }

    QByteArray innerHash = QCryptographicHash::hash(iKeyPad + payload, QCryptographicHash::Sha256);
    QByteArray mac = QCryptographicHash::hash(oKeyPad + innerHash, QCryptographicHash::Sha256);
    return mac.toHex();
}

bool DatabaseManager::verifySignature(const FileRecordEntry &record) const {
    // If HMAC is not configured, treat signature as implicitly valid to avoid false errors on baseline
    // scans when no key was provided.
    if (m_hmacKey.isEmpty()) {
        return true;
    }

    const QString expected = computeSignature(record.metadata);
    if (expected.isEmpty()) {
        return false;
    }
    return expected == record.signature;
}

bool DatabaseManager::ensureSchemaVersion() {
    if (!ensureConnection()) {
        return false;
    }

    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);"))) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to create meta table:" << m_lastError;
        return false;
    }

    if (!query.exec(QStringLiteral("SELECT value FROM meta WHERE key = 'schema_version' LIMIT 1;"))) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to read schema version:" << m_lastError;
        return false;
    }

    int currentVersion = 0;
    if (query.next()) {
        currentVersion = query.value(0).toInt();
    }

    constexpr int kCurrentSchemaVersion = 1;
    if (currentVersion == 0) {
        return setSchemaVersion(kCurrentSchemaVersion);
    }

    if (currentVersion < kCurrentSchemaVersion) {
        // Future migrations would be placed here; currently we only need to bump the version.
        return setSchemaVersion(kCurrentSchemaVersion);
    }

    return true;
}

bool DatabaseManager::setSchemaVersion(int version) const {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO meta (key, value) VALUES ('schema_version', :version) "
                                 "ON CONFLICT(key) DO UPDATE SET value = excluded.value;"));
    query.bindValue(":version", version);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        qWarning() << "Failed to update schema version:" << m_lastError;
        return false;
    }
    return true;
}
