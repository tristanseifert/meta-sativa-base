#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <type_traits>

#include <fmt/format.h>
#include <plog/Log.h>
#include <SQLiteCpp/SQLiteCpp.h>

#include "DataStore.h"
#include "Types.h"
#include "version.h"

/**
 * @brief Open the data store at the given path
 *
 * If there is not yet a datastore at the given path, it's initialized with the current schema.
 * Otherwise, it's simply opened as is, with a few basic consistency checks.
 *
 * @param dbPath Path on disk of the sqlite3 database
 */
DataStore::DataStore(const std::filesystem::path &dbPath) {
    // open db and apply pragmas
    PLOG_INFO << "opening db: " << dbPath.native();
    this->db = std::make_unique<SQLite::Database>(dbPath,
            (SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE));

    this->db->exec("PRAGMA foreign_keys = ON;");

    // check if db needs to be initialized
    if(!this->db->tableExists(kMetaTableName)) {
        PLOG_WARNING << "db is empty! initializing schema";
        this->initSchema();
    }

    // validate schema version
    auto versionStr = this->getMetaValue("schema.version");
    if(!versionStr) {
        throw std::logic_error("failed to get schema version!");
    }

    const auto version = std::stoul(*versionStr);
    PLOG_DEBUG << "Current schema version: " << version;

    if(version > kCurrentSchemaVersion) {
        throw std::runtime_error(fmt::format("unsupported schema version {} (expected {})",
                    version, kCurrentSchemaVersion));
    }
}

/**
 * @brief Apply the default schema to the database
 *
 * Create the default tables in the database, and fills in some metadata information.
 */
void DataStore::initSchema() {
    SQLite::Transaction txn(*this->db);

    /*
     * Create the metadata table
     *
     * This table holds general information about the contents of the data store, including the
     * schema version and application version used to edit this data.
     */
    this->db->exec(R"STR(
CREATE TABLE MetaInfo (
    id integer PRIMARY KEY AUTOINCREMENT,
    key text,
    value text
);
CREATE UNIQUE INDEX MetaInfo_i1 ON MetaInfo(key);
)STR");

    /*
     * Create the property keys meta table
     *
     * It's used to hold the string property keys (used for accessing properties) and map them to
     * an associated type, timestamps, and an unique id.
     *
     * An index is created on the key for fast searching.
     */
    this->db->exec(R"STR(
CREATE TABLE PropertyKeys (
    id integer PRIMARY KEY AUTOINCREMENT,
    key text,
    valueType integer,
    createdAt datetime DEFAULT (strftime('%s','now')),
    updatedAt datetime DEFAULT (strftime('%s','now'))
);
CREATE UNIQUE INDEX PropertyKeys_i1 ON PropertyKeys(key);
)STR");

    /*
     * Create property value tables
     *
     * For each of the different value types, create one table that has a `value` column of the
     * appropriate type. Splitting them out ensures that we don't accidentally coerce types between
     * columns.
     *
     * An index is created on the `propertyId` column of each table.
     */
    this->db->exec(R"STR(
CREATE TABLE PropertyValuesString (
    id integer PRIMARY KEY AUTOINCREMENT,
    propertyId integer,
    value text,
    FOREIGN KEY(propertyId) REFERENCES PropertyKeys(id) ON DELETE CASCADE
);
CREATE UNIQUE INDEX PropertyValuesString_i1 ON PropertyValuesString(propertyId);
CREATE INDEX PropertyValuesString_i2 ON PropertyValuesString(propertyId);

CREATE TABLE PropertyValuesBlob (
    id integer PRIMARY KEY AUTOINCREMENT,
    propertyId integer,
    value blob,
    FOREIGN KEY(propertyId) REFERENCES PropertyKeys(id) ON DELETE CASCADE
);
CREATE UNIQUE INDEX PropertyValuesBlob_i1 ON PropertyValuesBlob(propertyId);
CREATE INDEX PropertyValuesBlob_i2 ON PropertyValuesBlob(propertyId);

CREATE TABLE PropertyValuesInteger (
    id integer PRIMARY KEY AUTOINCREMENT,
    propertyId integer,
    value integer,
    FOREIGN KEY(propertyId) REFERENCES PropertyKeys(id) ON DELETE CASCADE
);
CREATE UNIQUE INDEX PropertyValuesInteger_i1 ON PropertyValuesInteger(propertyId);
CREATE INDEX PropertyValuesInteger_i2 ON PropertyValuesInteger(propertyId);

CREATE TABLE PropertyValuesReal (
    id integer PRIMARY KEY AUTOINCREMENT,
    propertyId integer,
    value real,
    FOREIGN KEY(propertyId) REFERENCES PropertyKeys(id) ON DELETE CASCADE
);
CREATE UNIQUE INDEX PropertyValuesReal_i1 ON PropertyValuesReal(propertyId);
CREATE INDEX PropertyValuesReal_i2 ON PropertyValuesReal(propertyId);
)STR");

    /*
     * Insert some metadata about the schema version, collect garbage, then commit the transaction
     * to ensure the database on disk is updated.
     */
    SQLite::Statement insMeta(*this->db, "INSERT INTO MetaInfo(key, value) VALUES (:key, :value);");

    insMeta.bind(":key", "creator.swversion");
    insMeta.bind(":value", kVersion);
    insMeta.exec();
    insMeta.reset();

    const auto now = std::chrono::system_clock::now();

    insMeta.bind(":key", "creator.timestamp");
    insMeta.bind(":value",
            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
    insMeta.exec();
    insMeta.reset();

    insMeta.bind(":key", "schema.version");
    insMeta.bind(":value", kCurrentSchemaVersion);
    insMeta.exec();

    // finally, commit the transaction so all changes are applied at once
    txn.commit();
}



/**
 * @brief Get the value for a property key
 *
 * Retrieve the value for the configuration option with the specified name, which must match
 * exactly.
 *
 * @throw std::logic_error Database consistency error
 *
 * @return Property value (or std::monostate if not found)
 */
PropertyValue DataStore::getKey(const std::string_view &name) {
    std::lock_guard lg(this->dbLock);

    // get the id and type information
    SQLite::Statement stmtInfo(*this->db, "SELECT id, valueType FROM PropertyKeys WHERE key = :keyName;");
    stmtInfo.bind(":keyName", name.data());

    if(!stmtInfo.executeStep()) {
        return std::monostate();
    }

    const uint32_t keyId = stmtInfo.getColumn("id");
    const uint32_t valueType = stmtInfo.getColumn("valueType");

    if(valueType == static_cast<uint32_t>(PropertyValueType::Null)) {
        // if the value is `null`, we have nothing more to do
        return nullptr;
    } // TODO: validate the type value better here

    // set up the query for the value
    const auto tableName = ValueTableName(static_cast<PropertyValueType>(valueType));
    if(tableName.empty()) {
        throw std::logic_error(fmt::format("unknown value table for type ${:x}", valueType));
    }

    // XXX: is there a better way than formatting in the table name?
    SQLite::Statement stmtQuery(*this->db, fmt::format("SELECT id, propertyId, value FROM {} "
            "WHERE propertyId = :keyId;", tableName));
    stmtQuery.bind(":keyId", keyId);

    // finally, get its value
    if(!stmtQuery.executeStep()) {
        throw std::logic_error(
                fmt::format("property '{}' ({}) (type ${:x}, table '{}') has no value!", name,
                    keyId, valueType, tableName));
    }

    switch(static_cast<PropertyValueType>(valueType)) {
        case PropertyValueType::String:
            return static_cast<std::string>(stmtQuery.getColumn("value"));
        case PropertyValueType::Blob: {
            const auto byteStr = static_cast<std::string>(stmtQuery.getColumn("value"));

            std::vector<std::byte> vec;

            auto bytePtr = reinterpret_cast<const std::byte *>(byteStr.data());
            std::copy(bytePtr, bytePtr + byteStr.size(), std::back_inserter(vec));
            return vec;
        }
        case PropertyValueType::Integer:
            return static_cast<uint64_t>(static_cast<long long>(stmtQuery.getColumn("value")));
        case PropertyValueType::Real:
            return static_cast<double>(stmtQuery.getColumn("value"));

        default:
            throw std::logic_error(fmt::format("unsupported value type ${:x} (key {}/{})",
                        valueType, name, keyId));
    }
}

/**
 * @brief Set a property value
 *
 * If the key does not exist already, it will be created. If it does exist, its value is updated.
 *
 * @param name Name of the key to set or update
 * @param vlaue Value to set the key to
 *
 * @throw std::logic_error Database consistency error
 *
 * @remark If an existing key is updated, it must be updated with data of the same type as it was
 *         originally created with. To change the value type of a key, delete the key and create
 *         it anew; or set its value to `null` before changing to delete the old value.
 */
void DataStore::setKey(const std::string_view &name, const PropertyValue &value) {
    std::lock_guard lg(this->dbLock);

    // get the id and type information
    SQLite::Statement stmtInfo(*this->db, "SELECT id, valueType FROM PropertyKeys WHERE key = :keyName;");
    stmtInfo.bind(":keyName", name.data());

    if(!stmtInfo.executeStep()) {
        // key doesn't exist yet, so insert it
        return this->insertKey(name, value);
    }

    /*
     * Get info about the existing key and its type. There's several different cases we take based
     * on the combination of the old and new value type:
     *
     * - Old type = null:  The key's value is updated without any additional constraints.
     * - Old type = other: The key's value may only be set to null or the old type.
     *
     * This is a mostly arbitrary restriction intended to detect potential bugs in client
     * applications. (That's why setting a key to `null` is allowed: to callers, that's not really
     * a different type but a value, yet we treat it as a different value type.)
     */
    const uint32_t keyId = stmtInfo.getColumn("id");
    const uint32_t oldValueType = stmtInfo.getColumn("valueType");

    if(oldValueType == static_cast<uint32_t>(PropertyValueType::Null)) {
        // allow any type to be set without further checking
        return this->updateKey(keyId, static_cast<PropertyValueType>(oldValueType), value);
    } else {
        // ensure the new value type is either null…
        if(std::holds_alternative<std::nullptr_t>(value)) {
            return this->updateKey(keyId, static_cast<PropertyValueType>(oldValueType), value);
        }
        // …or the existing value type
        else if(oldValueType == static_cast<uint32_t>(TypeForValue(value))) {
            return this->updateKey(keyId, static_cast<PropertyValueType>(oldValueType), value);
        }
        // otherwise, the set attempt is not permitted
        else {
            throw std::invalid_argument(fmt::format("changing type of key '{}' not allowed", name));
        }
    }
}

/**
 * @brief Delete a configuration key
 *
 * This function will delete only individual keys, whose key matches the specified name exactly.
 *
 * @throw std::runtime_error The provided key path is not terminal (e.g. it has children)
 *
 * @return Number of deleted keys
 */
size_t DataStore::deleteKey(const std::string_view &name) {
    // ensure this is a terminal (has value) key
    if(this->hasChildren(name)) {
        throw std::runtime_error(fmt::format("key '{}' has children", name));
    }

    // delete PropertyKeys row; the associated PropertyValues* entry is deleted by constraint
    SQLite::Statement stmt(*this->db, "DELETE FROM PropertyKeys WHERE key = :keyName;");
    stmt.bind(":keyName", name.data());

    return stmt.exec();
}

/**
 * @brief Delete all keys under a given key path
 *
 * All keys whose name starts with the provided key path will be deleted.
 *
 * @return Number of deleted keys
 */
size_t DataStore::deleteSubkeys(const std::string_view &namePrefix) {
    SQLite::Statement stmt(*this->db, "DELETE FROM PropertyKeys WHERE key LIKE :keyPrefix;");
    // match _at least_ one extra character after prefix (should be a period)
    stmt.bind(":keyPrefix", fmt::format("{}.%", namePrefix));

    return stmt.exec();
}



/**
 * @brief Check whether the given key path has child keys
 *
 * Queries whether there exist any keys whose name starts with the specified key path.
 */
bool DataStore::hasChildren(const std::string_view &name) {
    SQLite::Statement stmt(*this->db, "SELECT COUNT(*) FROM PropertyKeys WHERE key LIKE :keyName;");
    stmt.bind(":keyPrefix", fmt::format("{}.%", name));

    return stmt.exec() != 0;
}

/**
 * @brief Retrieve the value of a metadata key
 */
std::optional<std::string> DataStore::getMetaValue(const std::string_view &key) {
    SQLite::Statement stmt(*this->db, "SELECT id, key, value FROM MetaInfo WHERE key = :keyName;");
    stmt.bind(":keyName", key.data());

    if(!stmt.executeStep()) {
        // no such key
        return nullptr;
    }

    // read out the value column
    return static_cast<std::string>(stmt.getColumn("value"));
}

/**
 * @brief Insert a new key in the data store
 *
 * Allocate a new row into the data store, including a new value row if needed.
 *
 * @param keyName Name of the new key
 * @param value Value of the new key
 *
 * @throw std::runtime_error Database consistency errors
 */
void DataStore::insertKey(const std::string_view &keyName, const PropertyValue &value) {
    int err;

    // validate inputs
    if(keyName.empty()) {
        throw std::invalid_argument("invalid name");
    } else if(std::holds_alternative<std::monostate>(value)) {
        throw std::invalid_argument("invalid value");
    }

    // set up a transaction
    SQLite::Transaction txn(*this->db);

    // insert the key row
    const auto type = TypeForValue(value);

    PLOG_DEBUG << "set key '" << keyName << "' type " << (uint32_t) type;

    SQLite::Statement infoStmt(*this->db, "INSERT INTO PropertyKeys (key, valueType) "
            "VALUES (:key, :type);");
    infoStmt.bind(":key", keyName.data());
    infoStmt.bind(":type", static_cast<long>(type));

    err = infoStmt.exec();
    if(!err) {
        throw std::runtime_error("failed to insert property key info");
    }

    const auto keyId = this->db->getLastInsertRowid();
    PLOG_VERBOSE << "inserted key '" << keyName << "': " << keyId;

    // if value is non-null, insert a value row
    if(type != PropertyValueType::Null) {
        SQLite::Statement valueStmt(*this->db, fmt::format("INSERT INTO {} (propertyId, value) "
                "VALUES(:keyId, :value);", ValueTableName(type)));
        valueStmt.bind(":keyId", keyId);
        BindValue(valueStmt, ":value", value);

        err = valueStmt.exec();
        if(!err) {
            throw std::runtime_error("failed to insert property key value");
        }
    }

    // commit the transaction
    txn.commit();
}

/**
 * @brief Update the value of an existing key
 *
 * @param keyId Primary key id of the key to update
 * @param oldValueType Type of the old value of this property
 * @param newValue New value to assign to the key
 */
void DataStore::updateKey(const uint32_t keyId, const PropertyValueType oldValueType,
        const PropertyValue &newValue) {
    const auto newValueType = TypeForValue(newValue);

    PLOG_WARNING << "update key " << keyId << " old type " << (uint32_t) oldValueType << " new "
        << (uint32_t) newValueType;

    // start a transaction
    SQLite::Transaction txn(*this->db);

    // update the type of the key (if needed)
    if(oldValueType != newValueType) {
        // update the info row
        SQLite::Statement updateStmt(*this->db, "UPDATE PropertyKeys SET valueType = :newValueType "
                "WHERE id = :keyId;");
        updateStmt.bind(":keyId", keyId);
        updateStmt.bind(":newValueType", static_cast<uint32_t>(newValueType));

        if(!updateStmt.exec()) {
            throw std::runtime_error("failed to update property key type");
        }

        // delete old value row, if exists
        SQLite::Statement delValStmt(*this->db, fmt::format("DELETE FROM {} "
                    "WHERE propertyId = :keyId;", ValueTableName(oldValueType)));
        delValStmt.bind(":keyId", keyId);
        delValStmt.exec();
    }

    // insert (or update) a value row
    if(newValueType != PropertyValueType::Null) {
        SQLite::Statement stmt(*this->db, fmt::format("INSERT INTO {} (propertyId, value) "
                    "VALUES (:keyId, :value) ON CONFLICT(propertyId) DO UPDATE SET value=:value",
                ValueTableName(newValueType)));
        stmt.bind(":keyId", keyId);
        BindValue(stmt, ":value", newValue);

        if(!stmt.exec()) {
            throw std::runtime_error("failed to upsert value");
        }
    }

    // update the entry's "last modified" timestamp and commit changes
    this->updateKeyTimestamp(keyId);

    txn.commit();
}

/**
 * @brief Update the "last modified" timestamp of a key
 *
 * @param keyId Primary key id of the key
 *
 * @remark This should be wrapped in an outer transaction.
 */
void DataStore::updateKeyTimestamp(const uint32_t keyId) {
    SQLite::Statement stmt(*this->db, "UPDATE PropertyKeys SET updatedAt = strftime('%s','now') "
            "WHERE id = :keyId;");
    stmt.bind(":keyId", keyId);

    int err = stmt.exec();
    if(!err) {
        throw std::runtime_error("failed to update property key timestamp");
    }
}
