#include "common.hpp"
#include "http.hpp"

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
            std::cerr << "Unknown arg: " << arg << "\n";
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
            return {400, "{\"status\":\"error\",\"message\":\"missing machine/token\"}"};
        }

        const std::string machine = it_machine->second;
        const std::string token = it_token->second;

        duc::TokenPayload payload;
        std::string err;
        if (!duc::parse_and_verify_token(token, cfg.secret, &payload, &err)) {
            std::ostringstream oss;
            oss << "{\"status\":\"error\",\"message\":\"invalid token: " << json_escape(err) << "\"}";
            return {401, oss.str()};
        }

        if (payload.machine != machine) {
            return {401, "{\"status\":\"error\",\"message\":\"token-machine mismatch\"}"};
        }

        if (now > payload.exp) {
            return {403, "{\"status\":\"error\",\"message\":\"license expired\"}"};
        }

        auto found = machine_exp->find(machine);
        if (found != machine_exp->end() && now > found->second) {
            return {403, "{\"status\":\"error\",\"message\":\"server policy expired\"}"};
        }

        std::ostringstream oss;
        oss << "{"
            << "\"status\":\"ok\"," 
            << "\"server_time\":" << now << "," 
            << "\"expires_at\":" << payload.exp
            << "}";
        return {200, oss.str()};
    }

    return {404, "{\"status\":\"error\",\"message\":\"not found\"}"};
}

}  // namespace

int main(int argc, char** argv) {
    ServerConfig cfg;
    if (!parse_args(argc, argv, &cfg)) {
        return 1;
    }

    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    int reuse = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(cfg.port));

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "bind failed: " << std::strerror(errno) << "\n";
        ::close(listen_fd);
        return 1;
    }

    if (::listen(listen_fd, 32) != 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << "\n";
        ::close(listen_fd);
        return 1;
    }

    std::cout << "[server] listening on 0.0.0.0:" << cfg.port
              << ", license duration=" << cfg.duration_sec << " sec\n";

    std::unordered_map<std::string, int64_t> machine_exp;

    while (true) {
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        int conn_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (conn_fd < 0) {
            std::cerr << "accept failed: " << std::strerror(errno) << "\n";
            continue;
        }

        std::string req = read_http_request(conn_fd);
        size_t line_end = req.find("\r\n");
        std::string first_line = (line_end == std::string::npos) ? req : req.substr(0, line_end);

        std::string method, target;
        int status = 400;
        std::string body = "{\"status\":\"error\",\"message\":\"bad request\"}";

        if (!first_line.empty() && duc::parse_request_line(first_line, &method, &target)) {
            if (method != "GET") {
                status = 405;
                body = "{\"status\":\"error\",\"message\":\"only GET allowed\"}";
            } else {
                auto result = handle_route(target, cfg, &machine_exp);
                status = result.first;
                body = result.second;
            }
        }

        const std::string resp = duc::make_json_response(status, body);
        (void)::send(conn_fd, resp.data(), resp.size(), 0);
        ::close(conn_fd);

        std::cout << "[server] " << method << " " << target << " -> " << status << "\n";
    }

    ::close(listen_fd);
    return 0;
}
