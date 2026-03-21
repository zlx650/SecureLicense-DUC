#include "http.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>

namespace duc {

std::string http_status_text(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}

std::string make_json_response(int status_code, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << http_status_text(status_code) << "\r\n";
    oss << "Content-Type: application/json\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}

bool parse_request_line(const std::string& line, std::string* method, std::string* target) {
    std::istringstream iss(line);
    std::string m, t, v;
    if (!(iss >> m >> t >> v)) {
        return false;
    }
    if (method) *method = m;
    if (target) *target = t;
    return true;
}

HttpResponse http_get(const std::string& host, int port, const std::string& target, int timeout_ms) {
    HttpResponse out;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    std::string port_str = std::to_string(port);
    int gai = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai != 0) {
        out.error = std::string("getaddrinfo failed: ") + ::gai_strerror(gai);
        return out;
    }

    int fd = -1;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);

    if (fd < 0) {
        out.error = "connect failed";
        return out;
    }

    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::ostringstream req;
    req << "GET " << target << " HTTP/1.1\r\n";
    req << "Host: " << host << "\r\n";
    req << "Connection: close\r\n\r\n";

    const std::string req_data = req.str();
    ssize_t sent = ::send(fd, req_data.data(), req_data.size(), 0);
    if (sent < 0) {
        out.error = std::string("send failed: ") + std::strerror(errno);
        ::close(fd);
        return out;
    }

    std::string raw;
    char buf[4096];
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                out.error = "recv timeout";
            } else {
                out.error = std::string("recv failed: ") + std::strerror(errno);
            }
            ::close(fd);
            return out;
        }
        raw.append(buf, static_cast<size_t>(n));
    }

    ::close(fd);

    size_t line_end = raw.find("\r\n");
    if (line_end == std::string::npos) {
        out.error = "invalid http response line";
        return out;
    }

    std::string status_line = raw.substr(0, line_end);
    std::istringstream iss(status_line);
    std::string version;
    iss >> version >> out.status_code;
    if (out.status_code <= 0) {
        out.error = "invalid status code";
        return out;
    }

    size_t body_start = raw.find("\r\n\r\n");
    if (body_start == std::string::npos) {
        out.body.clear();
    } else {
        out.body = raw.substr(body_start + 4);
    }

    return out;
}

}  // namespace duc
