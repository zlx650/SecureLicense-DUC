#pragma once

#include <cstdint>
#include <string>

struct sqlite3;

namespace duc {

class LicenseStore {
public:
    LicenseStore() = default;
    ~LicenseStore();

    LicenseStore(const LicenseStore&) = delete;
    LicenseStore& operator=(const LicenseStore&) = delete;

    bool open(const std::string& db_path, std::string* err);
    void close();

    bool upsert_license(const std::string& machine,
                        int64_t expires_at,
                        int64_t updated_at,
                        std::string* err);

    bool get_license_expiry(const std::string& machine,
                            int64_t* expires_at,
                            bool* found,
                            std::string* err);

private:
    sqlite3* db_ = nullptr;
};

}  // namespace duc
