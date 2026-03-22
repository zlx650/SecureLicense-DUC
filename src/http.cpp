#include "http.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>

namespace duc {
namespace {

std::string collect_ssl_errors() {
    std::string text;
    unsigned long code = 0;
    while ((code = ERR_get_error()) != 0) {
        if (!text.empty()) {
            text += "; ";
        }
        text += ERR_error_string(code, nullptr);
    }
    return text.empty() ? "unknown ssl error" : text;
}

bool send_all_plain(int fd, const std::string& data, std::string* err) {
    size_t sent_total = 0;
    while (sent_total < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent_total, data.size() - sent_total, 0);
        if (n <= 0) {
            if (err) {
                *err = std::string("send failed: ") + std::strerror(errno);
            }
            return false;
        }
        sent_total += static_cast<size_t>(n);
    }
    return true;
}

bool send_all_tls(SSL* ssl, const std::string& data, std::string* err) {
    size_t sent_total = 0;
    while (sent_total < data.size()) {
        const int n = SSL_write(ssl, data.data() + sent_total, static_cast<int>(data.size() - sent_total));
        if (n <= 0) {
            const int ssl_err = SSL_get_error(ssl, n);
            if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                continue;
            }
            if (err) {
                *err = "ssl write failed: " + collect_ssl_errors();
            }
            return false;
        }
        sent_total += static_cast<size_t>(n);
    }
    return true;
}

bool recv_all_plain(int fd, std::string* raw, std::string* err) {
    char buf[4096];
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (err) *err = "recv timeout";
            } else {
                if (err) *err = std::string("recv failed: ") + std::strerror(errno);
            }
            return false;
        }
        raw->append(buf, static_cast<size_t>(n));
    }
    return true;
}

bool recv_all_tls(SSL* ssl, std::string* raw, std::string* err) {
    char buf[4096];
    while (true) {
        const int n = SSL_read(ssl, buf, static_cast<int>(sizeof(buf)));
        if (n > 0) {
            raw->append(buf, static_cast<size_t>(n));
            continue;
        }

        const int ssl_err = SSL_get_error(ssl, n);
        if (ssl_err == SSL_ERROR_ZERO_RETURN) {
            break;
        }
        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
            continue;
        }
        if (ssl_err == SSL_ERROR_SYSCALL && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (err) *err = "recv timeout";
            return false;
        }
        if (err) {
            *err = "ssl read failed: " + collect_ssl_errors();
        }
        return false;
    }
    return true;
}

}  // namespace

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

HttpResponse http_get(const std::string& host,
                      int port,
                      const std::string& target,
                      int timeout_ms,
                      const HttpTlsOptions& tls) {
    HttpResponse out;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    const std::string port_str = std::to_string(port);
    const int gai = ::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
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

    std::string raw;
    SSL_CTX* ssl_ctx = nullptr;
    SSL* ssl = nullptr;

    if (tls.enable_tls) {
        ssl_ctx = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx) {
            out.error = "ssl ctx init failed: " + collect_ssl_errors();
            ::close(fd);
            return out;
        }
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, nullptr);

        int ca_ok = 0;
        if (!tls.ca_cert_path.empty()) {
            ca_ok = SSL_CTX_load_verify_locations(ssl_ctx, tls.ca_cert_path.c_str(), nullptr);
        } else {
            ca_ok = SSL_CTX_set_default_verify_paths(ssl_ctx);
        }
        if (ca_ok != 1) {
            out.error = "load ca failed: " + collect_ssl_errors();
            SSL_CTX_free(ssl_ctx);
            ::close(fd);
            return out;
        }

        ssl = SSL_new(ssl_ctx);
        if (!ssl) {
            out.error = "ssl object init failed: " + collect_ssl_errors();
            SSL_CTX_free(ssl_ctx);
            ::close(fd);
            return out;
        }

        SSL_set_fd(ssl, fd);
        const std::string verify_name = tls.server_name.empty() ? host : tls.server_name;
        if (!verify_name.empty()) {
            (void)SSL_set_tlsext_host_name(ssl, verify_name.c_str());
            if (SSL_set1_host(ssl, verify_name.c_str()) != 1) {
                out.error = "set tls verify host failed: " + collect_ssl_errors();
                SSL_free(ssl);
                SSL_CTX_free(ssl_ctx);
                ::close(fd);
                return out;
            }
        }

        if (SSL_connect(ssl) != 1) {
            out.error = "tls handshake failed: " + collect_ssl_errors();
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            ::close(fd);
            return out;
        }

        const long verify_rc = SSL_get_verify_result(ssl);
        if (verify_rc != X509_V_OK) {
            out.error = std::string("certificate verify failed: ") + X509_verify_cert_error_string(verify_rc);
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            ::close(fd);
            return out;
        }

        if (!send_all_tls(ssl, req_data, &out.error)) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            ::close(fd);
            return out;
        }
        if (!recv_all_tls(ssl, &raw, &out.error)) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            ::close(fd);
            return out;
        }
        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ssl_ctx);
        ::close(fd);
    } else {
        if (!send_all_plain(fd, req_data, &out.error)) {
            ::close(fd);
            return out;
        }
        if (!recv_all_plain(fd, &raw, &out.error)) {
            ::close(fd);
            return out;
        }
        ::close(fd);
    }

    const size_t line_end = raw.find("\r\n");
    if (line_end == std::string::npos) {
        out.error = "invalid http response line";
        return out;
    }

    const std::string status_line = raw.substr(0, line_end);
    std::istringstream iss(status_line);
    std::string version;
    iss >> version >> out.status_code;
    if (out.status_code <= 0) {
        out.error = "invalid status code";
        return out;
    }

    const size_t body_start = raw.find("\r\n\r\n");
    if (body_start == std::string::npos) {
        out.body.clear();
    } else {
        out.body = raw.substr(body_start + 4);
    }

    return out;
}

}  // namespace duc
