#include "config.h"
#include "json.hpp"
#include "platform/platform.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <cctype>
#include <limits>
#include <stdexcept>

using json = nlohmann::json;

namespace {

bool is_hex_string(const std::string& value) {
    if ((value.size() & 1U) != 0) return false;
    for (unsigned char c : value) {
        if (!std::isxdigit(c)) return false;
    }
    return true;
}

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

std::string required_http_url(const json& value, const char* name) {
    const std::string parsed = required_string(value, name, true);
    if (!parsed.empty() && parsed.rfind("http://", 0) != 0 &&
        parsed.rfind("https://", 0) != 0) {
        throw std::invalid_argument(std::string(name) +
                                    " must begin with http:// or https://");
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
            if (pool.contains("management_url")) candidate.pool_management_url = required_http_url(pool["management_url"], "pool.management_url");
        }
        if (root.contains("board_port")) candidate.board_port = required_port(root["board_port"], "board_port");
        if (root.contains("board_detection_interval_ms")) {
            candidate.board_detection_interval_ms = bounded_u32(
                root["board_detection_interval_ms"],
                "board_detection_interval_ms", 1000, 3600000);
        }
        if (root.contains("dashboard_port")) candidate.dashboard_port = required_port(root["dashboard_port"], "dashboard_port");
        if (root.contains("dashboard_bind")) candidate.dashboard_bind = required_string(root["dashboard_bind"], "dashboard_bind");
        if (root.contains("dashboard_password")) candidate.dashboard_password = required_string(root["dashboard_password"], "dashboard_password", true);
        if (root.contains("dashboard_password_sha256")) {
            candidate.dashboard_password_sha256 = required_string(root["dashboard_password_sha256"], "dashboard_password_sha256", true);
            if (!candidate.dashboard_password_sha256.empty() &&
                (candidate.dashboard_password_sha256.size() != 64 ||
                 !is_hex_string(candidate.dashboard_password_sha256))) {
                throw std::invalid_argument("dashboard_password_sha256 must be 64 hex characters");
            }
        }
        if (root.contains("dashboard_password_salt")) {
            candidate.dashboard_password_salt = required_string(root["dashboard_password_salt"], "dashboard_password_salt", true);
            if (!candidate.dashboard_password_salt.empty() &&
                !is_hex_string(candidate.dashboard_password_salt)) {
                throw std::invalid_argument("dashboard_password_salt must be hex");
            }
        }
        if (candidate.dashboard_password_sha256.empty() !=
            candidate.dashboard_password_salt.empty()) {
            throw std::invalid_argument(
                "dashboard_password_sha256 and dashboard_password_salt must be set together");
        }
        if (root.contains("frequency_control_enabled")) {
            if (!root["frequency_control_enabled"].is_boolean()) {
                throw std::invalid_argument("frequency_control_enabled must be boolean");
            }
            candidate.frequency_control_enabled =
                root["frequency_control_enabled"].get<bool>();
        }
        if (root.contains("version_rolling")) {
            if (!root["version_rolling"].is_boolean()) throw std::invalid_argument("version_rolling must be boolean");
            candidate.version_rolling = root["version_rolling"].get<bool>();
        }
        if (root.contains("frequency_mhz")) candidate.default_frequency_mhz = bounded_u32(root["frequency_mhz"], "frequency_mhz", 0, 600);
        if (root.contains("voltage_mv")) candidate.default_voltage_mv = bounded_u32(root["voltage_mv"], "voltage_mv", 0, 5000);
        if (root.contains("min_difficulty")) {
            if (!root["min_difficulty"].is_number()) throw std::invalid_argument("min_difficulty must be numeric");
            const double difficulty = root["min_difficulty"].get<double>();
            if (!std::isfinite(difficulty) || difficulty <= 0.0) throw std::out_of_range("min_difficulty must be finite and positive");
            candidate.min_difficulty = difficulty;
        }

        std::cout << "[config] ASIC frequency control: "
                  << (candidate.frequency_control_enabled ? "enabled" : "disabled")
                  << std::endl;
        if (candidate.dashboard_password_sha256.empty() &&
            candidate.dashboard_password.empty()) {
            std::cout << "[config] Dashboard login DISABLED (no password configured)"
                      << std::endl;
        } else if (!candidate.dashboard_password_sha256.empty()) {
            std::cout << "[config] Dashboard login uses salted SHA-256 password"
                      << std::endl;
        } else {
            std::cout << "[config] Dashboard login uses plaintext config password"
                      << std::endl;
        }
        std::cout << "[config] Loaded and validated: " << path << std::endl;
        return candidate;
    } catch (const std::exception& error) {
        std::cerr << "[config] Invalid configuration: " << error.what()
                  << "; using safe defaults" << std::endl;
        return defaults;
    }
}

bool save_runtime_settings(const std::string& path,
                           uint16_t board_port,
                           uint16_t dashboard_port,
                           uint32_t board_detection_interval_ms) {
    if (path.empty() || board_port == 0 || dashboard_port == 0 ||
        board_port == dashboard_port || board_detection_interval_ms < 1000 ||
        board_detection_interval_ms > 3600000) {
        return false;
    }

    try {
        json root = json::object();
        {
            std::ifstream input(path);
            if (input.is_open()) root = json::parse(input);
        }
        if (!root.is_object()) return false;

        root["board_port"] = board_port;
        root["dashboard_port"] = dashboard_port;
        root["board_detection_interval_ms"] = board_detection_interval_ms;
        // Preserve authentication fields. Listener changes must never clear
        // the dashboard action password.

        const std::string temporary_path = path + ".tmp";
        {
            std::ofstream output(temporary_path,
                                 std::ios::binary | std::ios::trunc);
            if (!output.is_open()) return false;
            output << root.dump(4) << '\n';
            if (!output.good()) {
                output.close();
                platform::remove_file_if_present(temporary_path);
                return false;
            }
        }
        if (!platform::atomic_replace_file(temporary_path, path)) {
            platform::remove_file_if_present(temporary_path);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
