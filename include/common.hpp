#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace duc {

struct TokenPayload {
    std::string machine;
    int64_t iat = 0;
    int64_t exp = 0;
    std::string jti;
};

struct ClientCache {
    std::string machine;
    std::string token;
    int64_t last_server_time = 0;
    int64_t last_heartbeat_time = 0;
    int64_t last_local_time = 0;
};

int64_t now_epoch_sec();
std::string random_jti();

std::string sign_payload(const std::string& data, const std::string& secret);
std::string make_token(const TokenPayload& payload, const std::string& secret);
bool parse_and_verify_token(const std::string& token,
                            const std::string& secret,
                            TokenPayload* payload,
                            std::string* err);

std::string url_encode(const std::string& in);
std::string url_decode(const std::string& in);
std::unordered_map<std::string, std::string> parse_query_from_target(const std::string& target);

bool json_get_string(const std::string& json, const std::string& key, std::string* out);
bool json_get_int64(const std::string& json, const std::string& key, int64_t* out);

bool load_cache(const std::string& path, ClientCache* cache);
bool save_cache(const std::string& path, const ClientCache& cache);

std::string trim(const std::string& s);

}  // namespace duc
