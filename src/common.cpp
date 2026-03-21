#include "common.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace duc {
namespace {

std::string hex_u64(uint64_t v) {
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << v;
    return oss.str();
}

std::string hex_bytes(const uint8_t* data, size_t len) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0x0F]);
        out.push_back(kHex[data[i] & 0x0F]);
    }
    return out;
}

inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

std::array<uint8_t, 32> sha256_bytes(const std::string& input) {
    static constexpr uint32_t kTable[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
        0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
        0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
        0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
        0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
        0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
        0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
        0xc67178f2U};

    uint32_t h[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

    std::vector<uint8_t> msg(input.begin(), input.end());
    const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8ULL;
    msg.push_back(0x80);
    while ((msg.size() % 64U) != 56U) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFFU));
    }

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64] = {0};
        for (int i = 0; i < 16; ++i) {
            const size_t j = chunk + static_cast<size_t>(i) * 4U;
            w[i] = (static_cast<uint32_t>(msg[j]) << 24) |
                   (static_cast<uint32_t>(msg[j + 1]) << 16) |
                   (static_cast<uint32_t>(msg[j + 2]) << 8) |
                   (static_cast<uint32_t>(msg[j + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0];
        uint32_t b = h[1];
        uint32_t c = h[2];
        uint32_t d = h[3];
        uint32_t e = h[4];
        uint32_t f = h[5];
        uint32_t g = h[6];
        uint32_t hh = h[7];

        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = hh + s1 + ch + kTable[i] + w[i];
            const uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;

            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[static_cast<size_t>(i) * 4U] = static_cast<uint8_t>((h[i] >> 24) & 0xFFU);
        out[static_cast<size_t>(i) * 4U + 1U] = static_cast<uint8_t>((h[i] >> 16) & 0xFFU);
        out[static_cast<size_t>(i) * 4U + 2U] = static_cast<uint8_t>((h[i] >> 8) & 0xFFU);
        out[static_cast<size_t>(i) * 4U + 3U] = static_cast<uint8_t>(h[i] & 0xFFU);
    }
    return out;
}

std::string hmac_sha256_hex(const std::string& key, const std::string& data) {
    static constexpr size_t kBlockSize = 64;

    std::array<uint8_t, kBlockSize> key_block{};
    if (key.size() > kBlockSize) {
        const auto key_hash = sha256_bytes(key);
        std::copy(key_hash.begin(), key_hash.end(), key_block.begin());
    } else {
        std::copy(key.begin(), key.end(), key_block.begin());
    }

    std::array<uint8_t, kBlockSize> i_key_pad{};
    std::array<uint8_t, kBlockSize> o_key_pad{};
    for (size_t i = 0; i < kBlockSize; ++i) {
        i_key_pad[i] = static_cast<uint8_t>(key_block[i] ^ 0x36U);
        o_key_pad[i] = static_cast<uint8_t>(key_block[i] ^ 0x5cU);
    }

    std::string inner(reinterpret_cast<const char*>(i_key_pad.data()), i_key_pad.size());
    inner += data;
    const auto inner_hash = sha256_bytes(inner);

    std::string outer(reinterpret_cast<const char*>(o_key_pad.data()), o_key_pad.size());
    outer.append(reinterpret_cast<const char*>(inner_hash.data()), inner_hash.size());
    const auto out_hash = sha256_bytes(outer);

    return hex_bytes(out_hash.data(), out_hash.size());
}

std::string base64_encode(const std::string& in) {
    static const char* kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);

    int val = 0;
    int valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kTable[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(kTable[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

bool base64_decode(const std::string& in, std::string* out) {
    static const std::vector<int> kTable = [] {
        std::vector<int> t(256, -1);
        const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < static_cast<int>(chars.size()); ++i) {
            t[static_cast<unsigned char>(chars[i])] = i;
        }
        return t;
    }();

    std::string result;
    int val = 0;
    int valb = -8;
    for (unsigned char c : in) {
        if (std::isspace(c)) {
            continue;
        }
        if (c == '=') {
            break;
        }
        int d = kTable[c];
        if (d < 0) {
            return false;
        }
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    *out = result;
    return true;
}

std::string payload_to_text(const TokenPayload& p) {
    std::ostringstream oss;
    oss << "machine=" << url_encode(p.machine)
        << ";iat=" << p.iat
        << ";exp=" << p.exp
        << ";jti=" << p.jti;
    return oss.str();
}

bool text_to_payload(const std::string& text, TokenPayload* p) {
    std::unordered_map<std::string, std::string> kv;
    size_t start = 0;
    while (start < text.size()) {
        size_t semi = text.find(';', start);
        if (semi == std::string::npos) {
            semi = text.size();
        }
        std::string item = text.substr(start, semi - start);
        size_t eq = item.find('=');
        if (eq != std::string::npos) {
            kv[item.substr(0, eq)] = item.substr(eq + 1);
        }
        start = semi + 1;
    }

    if (!kv.count("machine") || !kv.count("iat") || !kv.count("exp") || !kv.count("jti")) {
        return false;
    }

    try {
        p->machine = url_decode(kv["machine"]);
        p->iat = std::stoll(kv["iat"]);
        p->exp = std::stoll(kv["exp"]);
        p->jti = kv["jti"];
    } catch (...) {
        return false;
    }
    return true;
}

std::string json_string_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

}  // namespace

int64_t now_epoch_sec() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string random_jti() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    return hex_u64(dist(rng));
}

std::string sign_payload(const std::string& data, const std::string& secret) {
    return hmac_sha256_hex(secret, data);
}

std::string make_token(const TokenPayload& payload, const std::string& secret) {
    std::string payload_text = payload_to_text(payload);
    std::string b64 = base64_encode(payload_text);
    std::string sig = sign_payload(b64, secret);
    return b64 + "." + sig;
}

bool parse_and_verify_token(const std::string& token,
                            const std::string& secret,
                            TokenPayload* payload,
                            std::string* err) {
    size_t dot = token.rfind('.');
    if (dot == std::string::npos) {
        if (err) *err = "token format invalid";
        return false;
    }

    std::string b64 = token.substr(0, dot);
    std::string sig = token.substr(dot + 1);

    std::string expected = sign_payload(b64, secret);
    if (sig != expected) {
        if (err) *err = "token signature mismatch";
        return false;
    }

    std::string payload_text;
    if (!base64_decode(b64, &payload_text)) {
        if (err) *err = "token payload decode failed";
        return false;
    }

    TokenPayload p;
    if (!text_to_payload(payload_text, &p)) {
        if (err) *err = "token payload parse failed";
        return false;
    }

    if (payload) {
        *payload = p;
    }
    return true;
}

std::string url_encode(const std::string& in) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase;
    for (unsigned char c : in) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return oss.str();
}

std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int value = 0;
            std::istringstream iss(in.substr(i + 1, 2));
            if (iss >> std::hex >> value) {
                out.push_back(static_cast<char>(value));
                i += 2;
            } else {
                out.push_back(in[i]);
            }
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::unordered_map<std::string, std::string> parse_query_from_target(const std::string& target) {
    std::unordered_map<std::string, std::string> query;
    size_t qpos = target.find('?');
    if (qpos == std::string::npos || qpos + 1 >= target.size()) {
        return query;
    }

    std::string s = target.substr(qpos + 1);
    size_t start = 0;
    while (start < s.size()) {
        size_t amp = s.find('&', start);
        if (amp == std::string::npos) {
            amp = s.size();
        }
        std::string item = s.substr(start, amp - start);
        size_t eq = item.find('=');
        if (eq != std::string::npos) {
            std::string k = url_decode(item.substr(0, eq));
            std::string v = url_decode(item.substr(eq + 1));
            query[k] = v;
        }
        start = amp + 1;
    }
    return query;
}

bool json_get_string(const std::string& json, const std::string& key, std::string* out) {
    const std::string pat = "\"" + key + "\"";
    size_t k = json.find(pat);
    if (k == std::string::npos) return false;
    size_t colon = json.find(':', k + pat.size());
    if (colon == std::string::npos) return false;
    size_t first_quote = json.find('"', colon + 1);
    if (first_quote == std::string::npos) return false;

    std::string value;
    for (size_t i = first_quote + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            value.push_back(json[i + 1]);
            ++i;
            continue;
        }
        if (c == '"') {
            *out = value;
            return true;
        }
        value.push_back(c);
    }
    return false;
}

bool json_get_int64(const std::string& json, const std::string& key, int64_t* out) {
    const std::string pat = "\"" + key + "\"";
    size_t k = json.find(pat);
    if (k == std::string::npos) return false;
    size_t colon = json.find(':', k + pat.size());
    if (colon == std::string::npos) return false;

    size_t i = colon + 1;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
        ++i;
    }
    size_t j = i;
    if (j < json.size() && (json[j] == '-' || json[j] == '+')) {
        ++j;
    }
    while (j < json.size() && std::isdigit(static_cast<unsigned char>(json[j]))) {
        ++j;
    }
    if (j == i) return false;

    try {
        *out = std::stoll(json.substr(i, j - i));
    } catch (...) {
        return false;
    }
    return true;
}

bool load_cache(const std::string& path, ClientCache* cache) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    ClientCache c;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        try {
            if (key == "machine") c.machine = val;
            else if (key == "token") c.token = val;
            else if (key == "last_server_time") c.last_server_time = std::stoll(val);
            else if (key == "last_heartbeat_time") c.last_heartbeat_time = std::stoll(val);
            else if (key == "last_local_time") c.last_local_time = std::stoll(val);
        } catch (...) {
            return false;
        }
    }

    *cache = c;
    return true;
}

bool save_cache(const std::string& path, const ClientCache& cache) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "machine=" << cache.machine << "\n";
    out << "token=" << cache.token << "\n";
    out << "last_server_time=" << cache.last_server_time << "\n";
    out << "last_heartbeat_time=" << cache.last_heartbeat_time << "\n";
    out << "last_local_time=" << cache.last_local_time << "\n";
    return true;
}

std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

}  // namespace duc
