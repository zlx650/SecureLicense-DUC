#include "common.hpp"
#include "http.hpp"
#include "log.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace {

struct ServerConfig {
    int port = 8088;
    std::string secret = "DUC_DEMO_SECRET_CHANGE_ME";
    int64_t duration_sec = 3600;
};

void print_usage(const char* exe) {
    std::cout << "Usage: " << exe << " [--port 8088] [--duration 3600] [--secret xxx]\n";
}

bool parse_args(int argc, char** argv, ServerConfig* cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            cfg->port = std::stoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            cfg->duration_sec = std::stoll(argv[++i]);
        } else if (arg == "--secret" && i + 1 < argc) {
            cfg->secret = argv[++i];
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

std::string read_http_request(int fd) {
    std::string req;
    char buf[2048];
    while (req.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
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
    std::unordered_map<std::string, int64_t>* machine_exp) {

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
        (*machine_exp)[machine] = exp;
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

        auto found = machine_exp->find(machine);
        if (found != machine_exp->end() && now > found->second) {
            duc::logging::warn("server", request_id, "server policy expired",
                               {{"machine", machine}, {"exp", std::to_string(found->second)}});
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
    duc::logging::set_min_level(duc::logging::Level::Info);

    ServerConfig cfg;
    if (!parse_args(argc, argv, &cfg)) {
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

    duc::logging::info("server", "-", "server started",
                       {{"listen", "0.0.0.0:" + std::to_string(cfg.port)},
                        {"license_duration_sec", std::to_string(cfg.duration_sec)}});

    std::unordered_map<std::string, int64_t> machine_exp;

    while (true) {
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        int conn_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (conn_fd < 0) {
            duc::logging::error("server", "-", "accept failed", {{"errno", std::strerror(errno)}});
            continue;
        }

        const std::string request_id = duc::logging::generate_request_id();
        std::string req = read_http_request(conn_fd);
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
                auto result = handle_route(target, request_id, cfg, &machine_exp);
                status = result.first;
                body = result.second;
            }
        } else {
            duc::logging::warn("server", request_id, "invalid request line", {{"line", first_line}});
        }

        const std::string resp = duc::make_json_response(status, body);
        (void)::send(conn_fd, resp.data(), resp.size(), 0);
        ::close(conn_fd);

        duc::logging::info("server", request_id, "request completed",
                           {{"status", std::to_string(status)}, {"method", method}, {"target", target}});
    }

    ::close(listen_fd);
    return 0;
}
