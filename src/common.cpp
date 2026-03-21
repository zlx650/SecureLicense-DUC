#include "common.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace duc {
namespace {

uint64_t fnv1a64(const std::string& s) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : s) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hex_u64(uint64_t v) {
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << v;
    return oss.str();
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
    return hex_u64(fnv1a64(secret + "|" + data));
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
