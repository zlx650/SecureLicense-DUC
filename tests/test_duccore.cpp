#include "common.hpp"
#include "http.hpp"
#include "log.hpp"
#include "storage.hpp"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

using TestFn = bool (*)();

struct TestCase {
    const char* name;
    TestFn fn;
};

#define EXPECT_TRUE(cond)                                                                 \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::cerr << "  assertion failed: " #cond << " (line " << __LINE__ << ")\n"; \
            return false;                                                                 \
        }                                                                                 \
    } while (0)

#define EXPECT_EQ(a, b)                                                                   \
    do {                                                                                  \
        if (!((a) == (b))) {                                                              \
            std::cerr << "  assertion failed: " #a " == " #b << " (line " << __LINE__ << ")\n"; \
            return false;                                                                 \
        }                                                                                 \
    } while (0)

bool test_token_roundtrip() {
    duc::TokenPayload in;
    in.machine = "node-A";
    in.iat = 100;
    in.exp = 200;
    in.jti = "abc123";

    const std::string secret = "secret-key";
    const std::string token = duc::make_token(in, secret);

    duc::TokenPayload out;
    std::string err;
    EXPECT_TRUE(duc::parse_and_verify_token(token, secret, &out, &err));
    EXPECT_EQ(out.machine, in.machine);
    EXPECT_EQ(out.iat, in.iat);
    EXPECT_EQ(out.exp, in.exp);
    EXPECT_EQ(out.jti, in.jti);
    return true;
}

bool test_token_tampered() {
    duc::TokenPayload in;
    in.machine = "node-B";
    in.iat = 100;
    in.exp = 300;
    in.jti = "xyz";

    const std::string secret = "secret-key";
    std::string token = duc::make_token(in, secret);
    token.back() = (token.back() == 'a') ? 'b' : 'a';

    duc::TokenPayload out;
    std::string err;
    EXPECT_TRUE(!duc::parse_and_verify_token(token, secret, &out, &err));
    return true;
}

bool test_wrong_secret() {
    duc::TokenPayload in;
    in.machine = "node-C";
    in.iat = 1;
    in.exp = 2;
    in.jti = "j1";

    const std::string token = duc::make_token(in, "secret-1");

    duc::TokenPayload out;
    std::string err;
    EXPECT_TRUE(!duc::parse_and_verify_token(token, "secret-2", &out, &err));
    return true;
}

bool test_query_parse() {
    const auto q = duc::parse_query_from_target("/heartbeat?machine=node-1&token=a%2Bb%3D%3D");
    EXPECT_TRUE(q.count("machine") == 1);
    EXPECT_TRUE(q.count("token") == 1);
    EXPECT_EQ(q.at("machine"), "node-1");
    EXPECT_EQ(q.at("token"), "a+b==");
    return true;
}

bool test_json_parse() {
    const std::string json = "{\"status\":\"ok\",\"server_time\":123,\"token\":\"abc\"}";
    std::string token;
    int64_t st = 0;
    EXPECT_TRUE(duc::json_get_string(json, "token", &token));
    EXPECT_TRUE(duc::json_get_int64(json, "server_time", &st));
    EXPECT_EQ(token, "abc");
    EXPECT_EQ(st, 123);
    return true;
}

bool test_request_line_parse() {
    std::string method;
    std::string target;
    EXPECT_TRUE(duc::parse_request_line("GET /activate?machine=a HTTP/1.1", &method, &target));
    EXPECT_EQ(method, "GET");
    EXPECT_EQ(target, "/activate?machine=a");
    return true;
}

bool test_cache_roundtrip() {
    const std::string path = "/tmp/duc_test_cache_roundtrip.txt";

    duc::ClientCache in;
    in.machine = "m1";
    in.token = "token";
    in.last_server_time = 11;
    in.last_heartbeat_time = 22;
    in.last_local_time = 33;

    EXPECT_TRUE(duc::save_cache(path, in));

    duc::ClientCache out;
    EXPECT_TRUE(duc::load_cache(path, &out));
    EXPECT_EQ(out.machine, in.machine);
    EXPECT_EQ(out.token, in.token);
    EXPECT_EQ(out.last_server_time, in.last_server_time);
    EXPECT_EQ(out.last_heartbeat_time, in.last_heartbeat_time);
    EXPECT_EQ(out.last_local_time, in.last_local_time);

    std::remove(path.c_str());
    return true;
}

bool test_sign_is_deterministic() {
    const std::string s1 = duc::sign_payload("abc", "k1");
    const std::string s2 = duc::sign_payload("abc", "k1");
    const std::string s3 = duc::sign_payload("abc", "k2");
    EXPECT_EQ(s1, s2);
    EXPECT_TRUE(s1 != s3);
    EXPECT_TRUE(s1.size() == 64);
    return true;
}

bool test_sqlite_store_roundtrip() {
    const std::string path = "/tmp/duc_test_store_roundtrip.db";
    std::remove(path.c_str());

    duc::LicenseStore store;
    std::string err;
    EXPECT_TRUE(store.open(path, &err));
    EXPECT_TRUE(store.upsert_license("node-1", 12345, 12300, &err));

    bool found = false;
    int64_t exp = 0;
    EXPECT_TRUE(store.get_license_expiry("node-1", &exp, &found, &err));
    EXPECT_TRUE(found);
    EXPECT_EQ(exp, 12345);

    store.close();
    std::remove(path.c_str());
    return true;
}

bool test_log_level_parse() {
    duc::logging::Level level = duc::logging::Level::Info;
    EXPECT_TRUE(duc::logging::parse_level("debug", &level));
    EXPECT_EQ(duc::logging::level_to_string(level), "DEBUG");
    EXPECT_TRUE(duc::logging::parse_level("WARNING", &level));
    EXPECT_EQ(duc::logging::level_to_string(level), "WARN");
    EXPECT_TRUE(!duc::logging::parse_level("INVALID", &level));
    return true;
}

bool test_request_id_unique() {
    const std::string a = duc::logging::generate_request_id();
    const std::string b = duc::logging::generate_request_id();
    EXPECT_TRUE(!a.empty());
    EXPECT_TRUE(!b.empty());
    EXPECT_TRUE(a != b);
    return true;
}

}  // namespace

int main() {
    const std::vector<TestCase> cases = {
        {"token_roundtrip", test_token_roundtrip},
        {"token_tampered", test_token_tampered},
        {"wrong_secret", test_wrong_secret},
        {"query_parse", test_query_parse},
        {"json_parse", test_json_parse},
        {"request_line_parse", test_request_line_parse},
        {"cache_roundtrip", test_cache_roundtrip},
        {"sign_is_deterministic", test_sign_is_deterministic},
        {"sqlite_store_roundtrip", test_sqlite_store_roundtrip},
        {"log_level_parse", test_log_level_parse},
        {"request_id_unique", test_request_id_unique},
    };

    int pass = 0;
    int fail = 0;

    for (const auto& t : cases) {
        const bool ok = t.fn();
        if (ok) {
            ++pass;
            std::cout << "[PASS] " << t.name << "\n";
        } else {
            ++fail;
            std::cout << "[FAIL] " << t.name << "\n";
        }
    }

    std::cout << "[SUMMARY] pass=" << pass << " fail=" << fail << "\n";
    return (fail == 0) ? 0 : 1;
}
