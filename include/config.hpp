#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace duc::config {

using KV = std::unordered_map<std::string, std::string>;

bool load_kv_file(const std::string& path, KV* out, std::string* err);
bool file_exists(const std::string& path);

bool get_string(const KV& kv, const std::string& key, std::string* out);
bool get_int(const KV& kv, const std::string& key, int* out, std::string* err);
bool get_int64(const KV& kv, const std::string& key, int64_t* out, std::string* err);

}  // namespace duc::config
