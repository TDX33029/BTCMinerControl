#include "config.h"
#include "json.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using json = nlohmann::json;

namespace {

uint16_t required_port(const json& value, const char* name) {
    if (!value.is_number_integer()) {
        throw std::invalid_argument(std::string(name) + " must be an integer");
    }
    const int64_t port = value.get<int64_t>();
    if (port < 1 || port > 65535) {
        throw std::out_of_range(std::string(name) + " must be 1..65535");
    }
    return static_cast<uint16_t>(port);
}

uint32_t bounded_u32(const json& value, const char* name,
                     uint32_t minimum, uint32_t maximum) {
    if (!value.is_number_integer()) {
        throw std::invalid_argument(std::string(name) + " must be an integer");
    }
    const int64_t parsed = value.get<int64_t>();
    if (parsed < minimum || static_cast<uint64_t>(parsed) > maximum) {
        throw std::out_of_range(std::string(name) + " is out of range");
    }
    return static_cast<uint32_t>(parsed);
}

std::string required_string(const json& value, const char* name,
                            bool allow_empty = false) {
    if (!value.is_string()) {
        throw std::invalid_argument(std::string(name) + " must be a string");
    }
    std::string parsed = value.get<std::string>();
    if (!allow_empty && parsed.empty()) {
        throw std::invalid_argument(std::string(name) + " must not be empty");
    }
    return parsed;
}

} // namespace

AppConfig load_config(const std::string& path) {
    AppConfig defaults;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cout << "[config] No config file found, using safe defaults" << std::endl;
        return defaults;
    }

    try {
        const json root = json::parse(file);
        if (!root.is_object()) throw std::invalid_argument("root must be an object");

        // Parse into a candidate and publish only after every field validates;
        // malformed input can no longer leave a partially updated config.
        AppConfig candidate = defaults;
        if (root.contains("pool")) {
            const auto& pool = root["pool"];
            if (!pool.is_object()) throw std::invalid_argument("pool must be an object");
            if (pool.contains("host")) candidate.pool_host = required_string(pool["host"], "pool.host");
            if (pool.contains("port")) candidate.pool_port = required_port(pool["port"], "pool.port");
            if (pool.contains("user")) candidate.pool_user = required_string(pool["user"], "pool.user");
            if (pool.contains("pass")) candidate.pool_pass = required_string(pool["pass"], "pool.pass", true);
        }
        if (root.contains("board_port")) candidate.board_port = required_port(root["board_port"], "board_port");
        if (root.contains("dashboard_port")) candidate.dashboard_port = required_port(root["dashboard_port"], "dashboard_port");
        if (root.contains("dashboard_bind")) candidate.dashboard_bind = required_string(root["dashboard_bind"], "dashboard_bind");
        if (root.contains("version_rolling")) {
            if (!root["version_rolling"].is_boolean()) throw std::invalid_argument("version_rolling must be boolean");
            candidate.version_rolling = root["version_rolling"].get<bool>();
        }
        if (root.contains("frequency_mhz")) candidate.default_frequency_mhz = bounded_u32(root["frequency_mhz"], "frequency_mhz", 1, 1000);
        if (root.contains("voltage_mv")) candidate.default_voltage_mv = bounded_u32(root["voltage_mv"], "voltage_mv", 0, 5000);
        if (root.contains("min_difficulty")) {
            if (!root["min_difficulty"].is_number()) throw std::invalid_argument("min_difficulty must be numeric");
            const double difficulty = root["min_difficulty"].get<double>();
            if (!std::isfinite(difficulty) || difficulty <= 0.0) throw std::out_of_range("min_difficulty must be finite and positive");
            candidate.min_difficulty = difficulty;
        }

        std::cout << "[config] Loaded and validated: " << path << std::endl;
        return candidate;
    } catch (const std::exception& error) {
        std::cerr << "[config] Invalid configuration: " << error.what()
                  << "; using safe defaults" << std::endl;
        return defaults;
    }
}
