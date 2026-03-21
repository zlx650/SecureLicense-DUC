#include "storage.hpp"

#include <sqlite3.h>

#include <sstream>

namespace duc {
namespace {

std::string sqlite_err(sqlite3* db, const char* prefix) {
    std::ostringstream oss;
    oss << prefix << ": " << (db ? sqlite3_errmsg(db) : "sqlite null db");
    return oss.str();
}

}  // namespace

LicenseStore::~LicenseStore() {
    close();
}

bool LicenseStore::open(const std::string& db_path, std::string* err) {
    close();

    const int rc = sqlite3_open_v2(db_path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        if (err) *err = sqlite_err(db_, "sqlite open failed");
        close();
        return false;
    }

    const char* kSchemaSql =
        "CREATE TABLE IF NOT EXISTS licenses ("
        "machine TEXT PRIMARY KEY,"
        "expires_at INTEGER NOT NULL,"
        "updated_at INTEGER NOT NULL"
        ");";

    char* errmsg = nullptr;
    const int schema_rc = sqlite3_exec(db_, kSchemaSql, nullptr, nullptr, &errmsg);
    if (schema_rc != SQLITE_OK) {
        if (err) {
            *err = std::string("sqlite schema init failed: ") + (errmsg ? errmsg : "unknown");
        }
        if (errmsg) sqlite3_free(errmsg);
        close();
        return false;
    }

    return true;
}

void LicenseStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool LicenseStore::upsert_license(const std::string& machine,
                                  int64_t expires_at,
                                  int64_t updated_at,
                                  std::string* err) {
    if (!db_) {
        if (err) *err = "sqlite db is not opened";
        return false;
    }

    const char* kSql =
        "INSERT INTO licenses(machine, expires_at, updated_at) VALUES(?, ?, ?) "
        "ON CONFLICT(machine) DO UPDATE SET expires_at=excluded.expires_at, updated_at=excluded.updated_at;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (err) *err = sqlite_err(db_, "sqlite prepare upsert failed");
        return false;
    }

    sqlite3_bind_text(stmt, 1, machine.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(expires_at));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(updated_at));

    rc = sqlite3_step(stmt);
    const bool ok = (rc == SQLITE_DONE);
    if (!ok && err) {
        *err = sqlite_err(db_, "sqlite upsert failed");
    }

    sqlite3_finalize(stmt);
    return ok;
}

bool LicenseStore::get_license_expiry(const std::string& machine,
                                      int64_t* expires_at,
                                      bool* found,
                                      std::string* err) {
    if (found) *found = false;

    if (!db_) {
        if (err) *err = "sqlite db is not opened";
        return false;
    }

    const char* kSql = "SELECT expires_at FROM licenses WHERE machine = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (err) *err = sqlite_err(db_, "sqlite prepare select failed");
        return false;
    }

    sqlite3_bind_text(stmt, 1, machine.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (expires_at) {
            *expires_at = static_cast<int64_t>(sqlite3_column_int64(stmt, 0));
        }
        if (found) *found = true;
        sqlite3_finalize(stmt);
        return true;
    }

    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return true;
    }

    if (err) *err = sqlite_err(db_, "sqlite select failed");
    sqlite3_finalize(stmt);
    return false;
}

}  // namespace duc
