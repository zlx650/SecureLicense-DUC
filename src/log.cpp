#include "log.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace duc::logging {
namespace {

std::atomic<int> g_min_level{static_cast<int>(Level::Info)};
std::atomic<uint64_t> g_request_counter{0};
std::mutex g_log_mutex;

const char* level_name(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string escape_json(const std::string& in) {
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

std::string now_iso8601_utc() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto now_sec = time_point_cast<seconds>(now);
    const auto ms = duration_cast<milliseconds>(now - now_sec).count();

    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << ms
        << 'Z';
    return oss.str();
}

}  // namespace

void set_min_level(Level level) {
    g_min_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

bool parse_level(const std::string& text, Level* out) {
    std::string s = text;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    Level level = Level::Info;
    if (s == "DEBUG") {
        level = Level::Debug;
    } else if (s == "INFO") {
        level = Level::Info;
    } else if (s == "WARN" || s == "WARNING") {
        level = Level::Warn;
    } else if (s == "ERROR") {
        level = Level::Error;
    } else {
        return false;
    }

    if (out) *out = level;
    return true;
}

std::string level_to_string(Level level) {
    return level_name(level);
}

std::string generate_request_id() {
    using namespace std::chrono;
    const auto ts = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
    const uint64_t seq = g_request_counter.fetch_add(1, std::memory_order_relaxed);

    std::ostringstream oss;
    oss << std::hex << ts << '-' << seq;
    return oss.str();
}

void write(Level level,
           const std::string& component,
           const std::string& request_id,
           const std::string& message,
           const Fields& fields) {
    if (static_cast<int>(level) < g_min_level.load(std::memory_order_relaxed)) {
        return;
    }

    std::ostringstream oss;
    oss << '{'
        << "\"ts\":\"" << now_iso8601_utc() << "\"," 
        << "\"level\":\"" << level_name(level) << "\"," 
        << "\"component\":\"" << escape_json(component) << "\"," 
        << "\"request_id\":\"" << escape_json(request_id.empty() ? "-" : request_id) << "\"," 
        << "\"message\":\"" << escape_json(message) << "\"";

    for (const auto& kv : fields) {
        oss << ",\"" << escape_json(kv.first) << "\":\"" << escape_json(kv.second) << "\"";
    }
    oss << '}';

    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::ostream& out = (level == Level::Error || level == Level::Warn) ? std::cerr : std::cout;
    out << oss.str() << std::endl;
}

void debug(const std::string& component,
           const std::string& request_id,
           const std::string& message,
           const Fields& fields) {
    write(Level::Debug, component, request_id, message, fields);
}

void info(const std::string& component,
          const std::string& request_id,
          const std::string& message,
          const Fields& fields) {
    write(Level::Info, component, request_id, message, fields);
}

void warn(const std::string& component,
          const std::string& request_id,
          const std::string& message,
          const Fields& fields) {
    write(Level::Warn, component, request_id, message, fields);
}

void error(const std::string& component,
           const std::string& request_id,
           const std::string& message,
           const Fields& fields) {
    write(Level::Error, component, request_id, message, fields);
}

}  // namespace duc::logging
