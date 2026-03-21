#pragma once

#include <string>
#include <utility>
#include <vector>

namespace duc::logging {

enum class Level {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

using Fields = std::vector<std::pair<std::string, std::string>>;

void set_min_level(Level level);
bool parse_level(const std::string& text, Level* out);
std::string level_to_string(Level level);
std::string generate_request_id();

void write(Level level,
           const std::string& component,
           const std::string& request_id,
           const std::string& message,
           const Fields& fields = {});

void debug(const std::string& component,
           const std::string& request_id,
           const std::string& message,
           const Fields& fields = {});

void info(const std::string& component,
          const std::string& request_id,
          const std::string& message,
          const Fields& fields = {});

void warn(const std::string& component,
          const std::string& request_id,
          const std::string& message,
          const Fields& fields = {});

void error(const std::string& component,
           const std::string& request_id,
           const std::string& message,
           const Fields& fields = {});

}  // namespace duc::logging
