#include "config.hpp"
#include "common.hpp"
#include "http.hpp"
#include "log.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

namespace {

struct ClientConfig {
    std::string host = "127.0.0.1";
    int port = 8088;
    std::string machine = "demo-machine";
    std::string cache_path = "./license_cache.txt";
    std::string secret = "DUC_DEMO_SECRET_CHANGE_ME";
    int64_t grace_sec = 120;
    int timeout_ms = 1500;
    std::string tls_ca_path;
    std::string tls_server_name;
    std::string log_level = "INFO";
    std::string config_path = "./config/client.conf";
};

void print_usage(const char* exe) {
    std::cout
        << "Usage:\n"
        << "  " << exe << " activate [options]\n"
        << "  " << exe << " run [options]\n\n"
        << "Options:\n"
        << "  --config ./config/client.conf\n"
        << "  --host 127.0.0.1\n"
        << "  --port 8088\n"
        << "  --machine demo-machine\n"
        << "  --cache ./license_cache.txt\n"
        << "  --secret DUC_DEMO_SECRET_CHANGE_ME\n"
        << "  --grace 120\n"
        << "  --timeout 1500\n"
        << "  --tls-ca ./certs/ca.crt\n"
        << "  --tls-server-name duc-demo.local\n"
        << "  --log-level INFO\n";
}

bool apply_config_file(const std::string& path, bool required, ClientConfig* cfg) {
    if (!required && !duc::config::file_exists(path)) {
        return true;
    }

    duc::config::KV kv;
    std::string err;
    if (!duc::config::load_kv_file(path, &kv, &err)) {
        std::cerr << "load config failed: " << err << "\n";
        return false;
    }

    if (!duc::config::get_int(kv, "client.port", &cfg->port, &err)) {
        std::cerr << "invalid config client.port: " << err << "\n";
        return false;
    }
    if (!duc::config::get_int64(kv, "client.grace_sec", &cfg->grace_sec, &err)) {
        std::cerr << "invalid config client.grace_sec: " << err << "\n";
        return false;
    }
    if (!duc::config::get_int(kv, "client.timeout_ms", &cfg->timeout_ms, &err)) {
        std::cerr << "invalid config client.timeout_ms: " << err << "\n";
        return false;
    }

    std::string value;
    if (duc::config::get_string(kv, "client.host", &value)) cfg->host = value;
    if (duc::config::get_string(kv, "client.machine", &value)) cfg->machine = value;
    if (duc::config::get_string(kv, "client.cache_path", &value)) cfg->cache_path = value;
    if (duc::config::get_string(kv, "client.secret", &value)) cfg->secret = value;
    if (duc::config::get_string(kv, "client.tls_ca_path", &value)) cfg->tls_ca_path = value;
    if (duc::config::get_string(kv, "client.tls_server_name", &value)) cfg->tls_server_name = value;
    if (duc::config::get_string(kv, "client.log_level", &value)) cfg->log_level = value;
    return true;
}

bool parse_common_args(int argc, char** argv, int start_idx, ClientConfig* cfg) {
    bool explicit_config = false;

    for (int i = start_idx; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            cfg->config_path = argv[++i];
            explicit_config = true;
        } else if (arg == "--config") {
            std::cerr << "missing value for --config\n";
            return false;
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return false;
        }
    }

    if (!apply_config_file(cfg->config_path, explicit_config, cfg)) {
        return false;
    }

    for (int i = start_idx; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            ++i;
        } else if (arg == "--config") {
            std::cerr << "missing value for --config\n";
            return false;
        } else if (arg == "--host" && i + 1 < argc) {
            cfg->host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            cfg->port = std::stoi(argv[++i]);
        } else if (arg == "--machine" && i + 1 < argc) {
            cfg->machine = argv[++i];
        } else if (arg == "--cache" && i + 1 < argc) {
            cfg->cache_path = argv[++i];
        } else if (arg == "--secret" && i + 1 < argc) {
            cfg->secret = argv[++i];
        } else if (arg == "--grace" && i + 1 < argc) {
            cfg->grace_sec = std::stoll(argv[++i]);
        } else if (arg == "--timeout" && i + 1 < argc) {
            cfg->timeout_ms = std::stoi(argv[++i]);
        } else if (arg == "--tls-ca" && i + 1 < argc) {
            cfg->tls_ca_path = argv[++i];
        } else if (arg == "--tls-server-name" && i + 1 < argc) {
            cfg->tls_server_name = argv[++i];
        } else if (arg == "--log-level" && i + 1 < argc) {
            cfg->log_level = argv[++i];
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown arg: " << arg << "\n";
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

int cmd_activate(const ClientConfig& cfg) {
    const std::string request_id = duc::logging::generate_request_id();
    duc::logging::info("client", request_id, "activate start",
                       {{"host", cfg.host}, {"port", std::to_string(cfg.port)}, {"machine", cfg.machine}});

    duc::HttpTlsOptions tls;
    tls.enable_tls = !cfg.tls_ca_path.empty();
    tls.ca_cert_path = cfg.tls_ca_path;
    tls.server_name = cfg.tls_server_name;

    std::string target = "/activate?machine=" + duc::url_encode(cfg.machine);
    duc::HttpResponse resp = duc::http_get(cfg.host, cfg.port, target, cfg.timeout_ms, tls);
    if (!resp.ok()) {
        duc::logging::error("client", request_id, "activate request failed",
                            {{"error", resp.error}, {"host", cfg.host}, {"port", std::to_string(cfg.port)}});
        std::cerr << "[activate] request failed: " << resp.error << "\n";
        return 2;
    }
    if (resp.status_code != 200) {
        duc::logging::warn("client", request_id, "activate rejected",
                           {{"status", std::to_string(resp.status_code)}});
        std::cerr << "[activate] server reject, status=" << resp.status_code
                  << ", body=" << resp.body << "\n";
        return 2;
    }

    std::string token;
    int64_t server_time = 0;
    int64_t expires_at = 0;
    if (!duc::json_get_string(resp.body, "token", &token) ||
        !duc::json_get_int64(resp.body, "server_time", &server_time) ||
        !duc::json_get_int64(resp.body, "expires_at", &expires_at)) {
        duc::logging::error("client", request_id, "activate response parse failed");
        std::cerr << "[activate] invalid server response: " << resp.body << "\n";
        return 2;
    }

    duc::ClientCache cache;
    cache.machine = cfg.machine;
    cache.token = token;
    cache.last_server_time = server_time;
    cache.last_heartbeat_time = server_time;
    cache.last_local_time = duc::now_epoch_sec();

    if (!duc::save_cache(cfg.cache_path, cache)) {
        duc::logging::error("client", request_id, "activate save cache failed", {{"cache", cfg.cache_path}});
        std::cerr << "[activate] save cache failed: " << cfg.cache_path << "\n";
        return 2;
    }

    duc::logging::info("client", request_id, "activate success",
                       {{"machine", cfg.machine}, {"expires_at", std::to_string(expires_at)}});

    std::cout << "[activate] success\n";
    std::cout << "  machine    : " << cfg.machine << "\n";
    std::cout << "  expires_at : " << expires_at << "\n";
    std::cout << "  cache      : " << cfg.cache_path << "\n";
    return 0;
}

bool precheck_local(const ClientConfig& cfg,
                    duc::ClientCache* cache,
                    duc::TokenPayload* payload,
                    std::string* reason) {
    if (cache->machine.empty()) {
        cache->machine = cfg.machine;
    }

    if (cache->machine != cfg.machine) {
        *reason = "cache machine mismatch";
        return false;
    }

    std::string err;
    if (!duc::parse_and_verify_token(cache->token, cfg.secret, payload, &err)) {
        *reason = "invalid token: " + err;
        return false;
    }

    const int64_t now_local = duc::now_epoch_sec();
    if (cache->last_local_time > 0 && now_local + 2 < cache->last_local_time) {
        *reason = "local clock rollback detected";
        return false;
    }

    const int64_t trusted_now = std::max(now_local, cache->last_server_time);
    if (trusted_now > payload->exp) {
        *reason = "license expired";
        return false;
    }

    return true;
}

int cmd_run(const ClientConfig& cfg) {
    const std::string request_id = duc::logging::generate_request_id();
    duc::logging::info("client", request_id, "run start",
                       {{"host", cfg.host}, {"port", std::to_string(cfg.port)}, {"machine", cfg.machine}});

    duc::ClientCache cache;
    if (!duc::load_cache(cfg.cache_path, &cache)) {
        duc::logging::error("client", request_id, "run cache load failed", {{"cache", cfg.cache_path}});
        std::cerr << "[run] no cache found, run activate first: " << cfg.cache_path << "\n";
        return 3;
    }

    duc::TokenPayload payload;
    std::string reason;
    if (!precheck_local(cfg, &cache, &payload, &reason)) {
        cache.last_local_time = duc::now_epoch_sec();
        duc::save_cache(cfg.cache_path, cache);
        duc::logging::warn("client", request_id, "run local precheck denied", {{"reason", reason}});
        std::cerr << "[run] denied (local precheck): " << reason << "\n";
        return 3;
    }

    std::string target = "/heartbeat?machine=" + duc::url_encode(cfg.machine)
                       + "&token=" + duc::url_encode(cache.token);
    duc::HttpTlsOptions tls;
    tls.enable_tls = !cfg.tls_ca_path.empty();
    tls.ca_cert_path = cfg.tls_ca_path;
    tls.server_name = cfg.tls_server_name;
    duc::HttpResponse resp = duc::http_get(cfg.host, cfg.port, target, cfg.timeout_ms, tls);

    if (resp.ok() && resp.status_code == 200) {
        int64_t server_time = 0;
        if (!duc::json_get_int64(resp.body, "server_time", &server_time)) {
            duc::logging::error("client", request_id, "heartbeat parse failed");
            std::cerr << "[run] denied: heartbeat response parse failed\n";
            return 3;
        }

        if (cache.last_server_time > 0 && server_time + 2 < cache.last_server_time) {
            duc::logging::warn("client", request_id, "server time rollback detected");
            std::cerr << "[run] denied: server time rollback detected\n";
            return 3;
        }

        cache.last_server_time = server_time;
        cache.last_heartbeat_time = server_time;
        cache.last_local_time = duc::now_epoch_sec();
        if (!duc::save_cache(cfg.cache_path, cache)) {
            duc::logging::warn("client", request_id, "save cache warning", {{"cache", cfg.cache_path}});
            std::cerr << "[run] warning: cache save failed\n";
        }

        duc::logging::info("client", request_id, "run allowed online");
        std::cout << "[run] allowed (online heartbeat ok)\n";
        return 0;
    }

    const int64_t now = duc::now_epoch_sec();
    const int64_t offline_for = (cache.last_heartbeat_time > 0) ? (now - cache.last_heartbeat_time) : (1LL << 60);

    if (offline_for <= cfg.grace_sec) {
        cache.last_local_time = now;
        duc::save_cache(cfg.cache_path, cache);

        duc::logging::warn("client", request_id, "run allowed by offline grace",
                           {{"offline_for", std::to_string(offline_for)},
                            {"grace_sec", std::to_string(cfg.grace_sec)}});
        std::cout << "[run] allowed (offline grace mode)\n";
        std::cout << "  offline_for: " << offline_for << " sec\n";
        std::cout << "  grace_sec  : " << cfg.grace_sec << " sec\n";
        return 0;
    }

    cache.last_local_time = now;
    duc::save_cache(cfg.cache_path, cache);
    duc::logging::error("client", request_id, "run denied: grace exceeded",
                        {{"offline_for", std::to_string(offline_for)},
                         {"grace_sec", std::to_string(cfg.grace_sec)}});
    std::cerr << "[run] denied: heartbeat failed and grace exceeded\n";
    if (!resp.error.empty()) {
        std::cerr << "  network_error: " << resp.error << "\n";
    } else {
        std::cerr << "  server_status: " << resp.status_code << ", body=" << resp.body << "\n";
    }
    return 3;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string command = argv[1];

    ClientConfig cfg;
    if (!parse_common_args(argc, argv, 2, &cfg)) {
        return 1;
    }

    duc::logging::Level level = duc::logging::Level::Info;
    if (!duc::logging::parse_level(cfg.log_level, &level)) {
        std::cerr << "invalid log level: " << cfg.log_level << "\n";
        return 1;
    }
    duc::logging::set_min_level(level);

    if (command == "activate") {
        return cmd_activate(cfg);
    }
    if (command == "run") {
        return cmd_run(cfg);
    }

    std::cerr << "Unknown command: " << command << "\n";
    print_usage(argv[0]);
    return 1;
}
