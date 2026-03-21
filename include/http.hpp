#pragma once

#include <string>

namespace duc {

struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::string error;

    bool ok() const { return error.empty() && status_code > 0; }
};

HttpResponse http_get(const std::string& host, int port, const std::string& target, int timeout_ms = 1500);

bool parse_request_line(const std::string& line, std::string* method, std::string* target);
std::string make_json_response(int status_code, const std::string& body);
std::string http_status_text(int status_code);

}  // namespace duc
