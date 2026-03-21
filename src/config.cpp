#include "config.hpp"

#include "common.hpp"

#include <fstream>
#include <sstream>

namespace duc::config {

bool file_exists(const std::string& path) {
    std::ifstream in(path);
    return in.good();
}

bool load_kv_file(const std::string& path, KV* out, std::string* err) {
    std::ifstream in(path);
    if (!in.is_open()) {
        if (err) *err = "open config failed: " + path;
        return false;
    }

    KV kv;
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        line = duc::trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            if (err) {
                std::ostringstream oss;
                oss << "invalid config line " << line_no << ": " << line;
                *err = oss.str();
            }
            return false;
        }

        std::string key = duc::trim(line.substr(0, eq));
        std::string value = duc::trim(line.substr(eq + 1));
        if (key.empty()) {
            if (err) {
                std::ostringstream oss;
                oss << "empty key at line " << line_no;
                *err = oss.str();
            }
            return false;
        }
        kv[key] = value;
    }

    *out = std::move(kv);
    return true;
}

bool get_string(const KV& kv, const std::string& key, std::string* out) {
    const auto it = kv.find(key);
    if (it == kv.end()) {
        return false;
    }
    if (out) *out = it->second;
    return true;
}

bool get_int(const KV& kv, const std::string& key, int* out, std::string* err) {
    const auto it = kv.find(key);
    if (it == kv.end()) {
        return true;
    }

    try {
        if (out) *out = std::stoi(it->second);
    } catch (...) {
        if (err) *err = "invalid int for key: " + key;
        return false;
    }
    return true;
}

bool get_int64(const KV& kv, const std::string& key, int64_t* out, std::string* err) {
    const auto it = kv.find(key);
    if (it == kv.end()) {
        return true;
    }

    try {
        if (out) *out = std::stoll(it->second);
    } catch (...) {
        if (err) *err = "invalid int64 for key: " + key;
        return false;
    }
    return true;
}

}  // namespace duc::config
