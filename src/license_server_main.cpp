#include "config.hpp"
#include "common.hpp"
#include "http.hpp"
#include "log.hpp"
#include "storage.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

namespace {

struct ServerConfig {
    int port = 8088;
    std::string secret = "DUC_DEMO_SECRET_CHANGE_ME";
    int64_t duration_sec = 3600;
    std::string db_path = "./license_store.db";
    std::string tls_cert_path;
    std::string tls_key_path;
    std::string log_level = "INFO";
    std::string config_path = "./config/server.conf";
};

void print_usage(const char* exe) {
    std::cout << "Usage: " << exe
              << " [--config ./config/server.conf] [--port 8088] [--duration 3600] "
                 "[--secret xxx] [--db ./license_store.db] [--tls-cert ./certs/server.crt] "
                 "[--tls-key ./certs/server.key] [--log-level INFO]\n";
}

bool apply_config_file(const std::string& path, bool required, ServerConfig* cfg) {
    if (!required && !duc::config::file_exists(path)) {
        return true;
    }

    duc::config::KV kv;
    std::string err;
    if (!duc::config::load_kv_file(path, &kv, &err)) {
        duc::logging::error("server", "-", "load config failed", {{"path", path}, {"error", err}});
        return false;
    }

    if (!duc::config::get_int(kv, "server.port", &cfg->port, &err)) {
        duc::logging::error("server", "-", "invalid config", {{"key", "server.port"}, {"error", err}});
        return false;
    }
    if (!duc::config::get_int64(kv, "server.duration_sec", &cfg->duration_sec, &err)) {
        duc::logging::error("server", "-", "invalid config", {{"key", "server.duration_sec"}, {"error", err}});
        return false;
    }
    std::string value;
    if (duc::config::get_string(kv, "server.secret", &value)) cfg->secret = value;
    if (duc::config::get_string(kv, "server.db_path", &value)) cfg->db_path = value;
    if (duc::config::get_string(kv, "server.tls_cert_path", &value)) cfg->tls_cert_path = value;
    if (duc::config::get_string(kv, "server.tls_key_path", &value)) cfg->tls_key_path = value;
    if (duc::config::get_string(kv, "server.log_level", &value)) cfg->log_level = value;
    return true;
}

bool parse_args(int argc, char** argv, ServerConfig* cfg) {
    bool explicit_config = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            cfg->config_path = argv[++i];
            explicit_config = true;
        } else if (arg == "--config") {
            duc::logging::error("server", "-", "missing value for --config");
            return false;
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return false;
        }
    }

    if (!apply_config_file(cfg->config_path, explicit_config, cfg)) {
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            ++i;
        } else if (arg == "--config") {
            duc::logging::error("server", "-", "missing value for --config");
            return false;
        } else if (arg == "--port" && i + 1 < argc) {
            cfg->port = std::stoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            cfg->duration_sec = std::stoll(argv[++i]);
        } else if (arg == "--secret" && i + 1 < argc) {
            cfg->secret = argv[++i];
        } else if (arg == "--db" && i + 1 < argc) {
            cfg->db_path = argv[++i];
        } else if (arg == "--tls-cert" && i + 1 < argc) {
            cfg->tls_cert_path = argv[++i];
        } else if (arg == "--tls-key" && i + 1 < argc) {
            cfg->tls_key_path = argv[++i];
        } else if (arg == "--log-level" && i + 1 < argc) {
            cfg->log_level = argv[++i];
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return false;
        } else {
            duc::logging::error("server", "-", "unknown arg", {{"arg", arg}});
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

bool tls_enabled(const ServerConfig& cfg) {
    return !cfg.tls_cert_path.empty() && !cfg.tls_key_path.empty();
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string path_only(const std::string& target) {
    size_t q = target.find('?');
    if (q == std::string::npos) {
        return target;
    }
    return target.substr(0, q);
}

std::string read_http_request(int fd, SSL* ssl) {
    std::string req;
    char buf[2048];
    while (req.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = 0;
        if (ssl) {
            n = SSL_read(ssl, buf, static_cast<int>(sizeof(buf)));
            if (n <= 0) {
                int err = SSL_get_error(ssl, static_cast<int>(n));
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                    continue;
                }
                break;
            }
        } else {
            n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
        }
        if (n <= 0) {
            break;
        }
        req.append(buf, static_cast<size_t>(n));
        if (req.size() > 16384) {
            break;
        }
    }
    return req;
}

std::pair<int, std::string> handle_route(
    const std::string& target,
    const std::string& request_id,
    const ServerConfig& cfg,
    duc::LicenseStore* store) {

    const int64_t now = duc::now_epoch_sec();
    const std::string path = path_only(target);
    const auto query = duc::parse_query_from_target(target);

    if (path == "/time") {
        std::ostringstream oss;
        oss << "{\"status\":\"ok\",\"server_time\":" << now << "}";
        return {200, oss.str()};
    }

    if (path == "/activate") {
        auto it = query.find("machine");
        if (it == query.end() || it->second.empty()) {
            duc::logging::warn("server", request_id, "activate missing machine");
            return {400, "{\"status\":\"error\",\"message\":\"missing machine\"}"};
        }

        const std::string machine = it->second;
        const int64_t exp = now + cfg.duration_sec;

        duc::TokenPayload payload;
        payload.machine = machine;
        payload.iat = now;
        payload.exp = exp;
        payload.jti = duc::random_jti();

        const std::string token = duc::make_token(payload, cfg.secret);
        std::string db_err;
        if (!store->upsert_license(machine, exp, now, &db_err)) {
            duc::logging::error("server", request_id, "db upsert failed", {{"error", db_err}});
            return {500, "{\"status\":\"error\",\"message\":\"internal db error\"}"};
        }

        duc::logging::info("server", request_id, "license activated",
                           {{"machine", machine}, {"expires_at", std::to_string(exp)}});

        std::ostringstream oss;
        oss << "{"
            << "\"status\":\"ok\"," 
            << "\"machine\":\"" << json_escape(machine) << "\"," 
            << "\"token\":\"" << json_escape(token) << "\"," 
            << "\"server_time\":" << now << "," 
            << "\"expires_at\":" << exp
            << "}";
        return {200, oss.str()};
    }

    if (path == "/heartbeat") {
        auto it_machine = query.find("machine");
        auto it_token = query.find("token");
        if (it_machine == query.end() || it_token == query.end()) {
            duc::logging::warn("server", request_id, "heartbeat missing machine/token");
            return {400, "{\"status\":\"error\",\"message\":\"missing machine/token\"}"};
        }

        const std::string machine = it_machine->second;
        const std::string token = it_token->second;

        duc::TokenPayload payload;
        std::string err;
        if (!duc::parse_and_verify_token(token, cfg.secret, &payload, &err)) {
            duc::logging::warn("server", request_id, "heartbeat invalid token", {{"reason", err}});
            std::ostringstream oss;
            oss << "{\"status\":\"error\",\"message\":\"invalid token: " << json_escape(err) << "\"}";
            return {401, oss.str()};
        }

        if (payload.machine != machine) {
            duc::logging::warn("server", request_id, "token-machine mismatch",
                               {{"machine", machine}, {"token_machine", payload.machine}});
            return {401, "{\"status\":\"error\",\"message\":\"token-machine mismatch\"}"};
        }

        if (now > payload.exp) {
            duc::logging::warn("server", request_id, "license expired",
                               {{"machine", machine}, {"exp", std::to_string(payload.exp)}});
            return {403, "{\"status\":\"error\",\"message\":\"license expired\"}"};
        }

        int64_t db_exp = 0;
        bool found = false;
        std::string db_err;
        if (!store->get_license_expiry(machine, &db_exp, &found, &db_err)) {
            duc::logging::error("server", request_id, "db query failed", {{"error", db_err}});
            return {500, "{\"status\":\"error\",\"message\":\"internal db error\"}"};
        }
        if (!found) {
            duc::logging::warn("server", request_id, "license not found in db", {{"machine", machine}});
            return {403, "{\"status\":\"error\",\"message\":\"license not found\"}"};
        }

        if (now > db_exp) {
            duc::logging::warn("server", request_id, "server policy expired",
                               {{"machine", machine}, {"exp", std::to_string(db_exp)}});
            return {403, "{\"status\":\"error\",\"message\":\"server policy expired\"}"};
        }

        std::ostringstream oss;
        oss << "{"
            << "\"status\":\"ok\"," 
            << "\"server_time\":" << now << "," 
            << "\"expires_at\":" << payload.exp
            << "}";
        duc::logging::debug("server", request_id, "heartbeat ok", {{"machine", machine}});
        return {200, oss.str()};
    }

    duc::logging::warn("server", request_id, "route not found", {{"target", target}});
    return {404, "{\"status\":\"error\",\"message\":\"not found\"}"};
}

}  // namespace

int main(int argc, char** argv) {
    ServerConfig cfg;
    if (!parse_args(argc, argv, &cfg)) {
        return 1;
    }

    duc::logging::Level level = duc::logging::Level::Info;
    if (!duc::logging::parse_level(cfg.log_level, &level)) {
        duc::logging::error("server", "-", "invalid log level", {{"log_level", cfg.log_level}});
        return 1;
    }
    duc::logging::set_min_level(level);

    if ((!cfg.tls_cert_path.empty() && cfg.tls_key_path.empty()) ||
        (cfg.tls_cert_path.empty() && !cfg.tls_key_path.empty())) {
        duc::logging::error("server", "-", "invalid tls config",
                            {{"tls_cert_path", cfg.tls_cert_path}, {"tls_key_path", cfg.tls_key_path}});
        return 1;
    }

    duc::LicenseStore store;
    std::string db_err;
    if (!store.open(cfg.db_path, &db_err)) {
        duc::logging::error("server", "-", "db open failed",
                            {{"db_path", cfg.db_path}, {"error", db_err}});
        return 1;
    }

    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        duc::logging::error("server", "-", "socket failed", {{"errno", std::strerror(errno)}});
        return 1;
    }

    int reuse = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(cfg.port));

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        duc::logging::error("server", "-", "bind failed", {{"errno", std::strerror(errno)}});
        ::close(listen_fd);
        return 1;
    }

    if (::listen(listen_fd, 32) != 0) {
        duc::logging::error("server", "-", "listen failed", {{"errno", std::strerror(errno)}});
        ::close(listen_fd);
        return 1;
    }

    SSL_CTX* tls_ctx = nullptr;
    if (tls_enabled(cfg)) {
        tls_ctx = SSL_CTX_new(TLS_server_method());
        if (!tls_ctx) {
            duc::logging::error("server", "-", "tls ctx init failed");
            ::close(listen_fd);
            return 1;
        }
        SSL_CTX_set_min_proto_version(tls_ctx, TLS1_2_VERSION);
        if (SSL_CTX_use_certificate_file(tls_ctx, cfg.tls_cert_path.c_str(), SSL_FILETYPE_PEM) != 1) {
            duc::logging::error("server", "-", "load tls cert failed", {{"path", cfg.tls_cert_path}});
            SSL_CTX_free(tls_ctx);
            ::close(listen_fd);
            return 1;
        }
        if (SSL_CTX_use_PrivateKey_file(tls_ctx, cfg.tls_key_path.c_str(), SSL_FILETYPE_PEM) != 1) {
            duc::logging::error("server", "-", "load tls key failed", {{"path", cfg.tls_key_path}});
            SSL_CTX_free(tls_ctx);
            ::close(listen_fd);
            return 1;
        }
        if (SSL_CTX_check_private_key(tls_ctx) != 1) {
            duc::logging::error("server", "-", "tls key mismatch with cert");
            SSL_CTX_free(tls_ctx);
            ::close(listen_fd);
            return 1;
        }
    }

    duc::logging::info("server", "-", "server started",
                       {{"listen", "0.0.0.0:" + std::to_string(cfg.port)},
                        {"license_duration_sec", std::to_string(cfg.duration_sec)},
                        {"db_path", cfg.db_path},
                        {"transport", tls_enabled(cfg) ? "https" : "http"}});

    while (true) {
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        int conn_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (conn_fd < 0) {
            duc::logging::error("server", "-", "accept failed", {{"errno", std::strerror(errno)}});
            continue;
        }

        SSL* ssl = nullptr;
        if (tls_ctx) {
            ssl = SSL_new(tls_ctx);
            if (!ssl) {
                duc::logging::error("server", "-", "ssl object init failed");
                ::close(conn_fd);
                continue;
            }
            SSL_set_fd(ssl, conn_fd);
            if (SSL_accept(ssl) != 1) {
                duc::logging::warn("server", "-", "tls handshake failed");
                SSL_free(ssl);
                ::close(conn_fd);
                continue;
            }
        }

        const std::string request_id = duc::logging::generate_request_id();
        std::string req = read_http_request(conn_fd, ssl);
        size_t line_end = req.find("\r\n");
        std::string first_line = (line_end == std::string::npos) ? req : req.substr(0, line_end);

        std::string method, target;
        int status = 400;
        std::string body = "{\"status\":\"error\",\"message\":\"bad request\"}";

        if (!first_line.empty() && duc::parse_request_line(first_line, &method, &target)) {
            duc::logging::info("server", request_id, "request received",
                               {{"method", method}, {"target", target}});
            if (method != "GET") {
                status = 405;
                body = "{\"status\":\"error\",\"message\":\"only GET allowed\"}";
                duc::logging::warn("server", request_id, "method not allowed", {{"method", method}});
            } else {
                auto result = handle_route(target, request_id, cfg, &store);
                status = result.first;
                body = result.second;
            }
        } else {
            duc::logging::warn("server", request_id, "invalid request line", {{"line", first_line}});
        }

        const std::string resp = duc::make_json_response(status, body);
        if (ssl) {
            size_t sent_total = 0;
            while (sent_total < resp.size()) {
                int n = SSL_write(ssl, resp.data() + sent_total, static_cast<int>(resp.size() - sent_total));
                if (n <= 0) {
                    break;
                }
                sent_total += static_cast<size_t>(n);
            }
            SSL_shutdown(ssl);
            SSL_free(ssl);
        } else {
            (void)::send(conn_fd, resp.data(), resp.size(), 0);
        }
        ::close(conn_fd);

        duc::logging::info("server", request_id, "request completed",
                           {{"status", std::to_string(status)}, {"method", method}, {"target", target}});
    }

    if (tls_ctx) {
        SSL_CTX_free(tls_ctx);
    }
    ::close(listen_fd);
    return 0;
}
