#include "config.h"
#include "json.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "Crypt32.lib")

using json = nlohmann::json;

namespace {

const char kCredentialEntropy[] =
    "BTCMinerControl dashboard credentials v1";

bool bytes_to_base64(const BYTE* data, DWORD size, std::string& encoded) {
    DWORD characters = 0;
    const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    if (!CryptBinaryToStringA(data, size, flags, nullptr, &characters)) {
        return false;
    }
    std::string buffer(characters, '\0');
    if (!CryptBinaryToStringA(data, size, flags, buffer.data(), &characters)) {
        return false;
    }
    if (!buffer.empty() && buffer.back() == '\0') buffer.pop_back();
    encoded = std::move(buffer);
    return true;
}

bool base64_to_bytes(const std::string& encoded, std::vector<BYTE>& bytes) {
    DWORD size = 0;
    if (!CryptStringToBinaryA(encoded.c_str(), static_cast<DWORD>(encoded.size()),
                              CRYPT_STRING_BASE64, nullptr, &size,
                              nullptr, nullptr)) {
        return false;
    }
    bytes.assign(size, 0);
    return CryptStringToBinaryA(encoded.c_str(),
        static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64,
        bytes.data(), &size, nullptr, nullptr) != FALSE;
}

DATA_BLOB credential_entropy_blob() {
    DATA_BLOB entropy{};
    entropy.pbData = reinterpret_cast<BYTE*>(
        const_cast<char*>(kCredentialEntropy));
    entropy.cbData = static_cast<DWORD>(sizeof(kCredentialEntropy) - 1);
    return entropy;
}

bool protect_credentials(const std::string& username,
                         const std::string& password,
                         std::string& protected_text) {
    const std::string plaintext = json{
        {"username", username}, {"password", password}
    }.dump();
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(
        const_cast<char*>(plaintext.data()));
    input.cbData = static_cast<DWORD>(plaintext.size());
    DATA_BLOB entropy = credential_entropy_blob();
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"BTCMinerControl Web credentials",
                          &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return false;
    }
    const bool encoded = bytes_to_base64(output.pbData, output.cbData,
                                         protected_text);
    LocalFree(output.pbData);
    return encoded;
}

bool unprotect_credentials(const std::string& protected_text,
                           std::string& username,
                           std::string& password) {
    std::vector<BYTE> encrypted;
    if (!base64_to_bytes(protected_text, encrypted) || encrypted.empty()) {
        return false;
    }
    DATA_BLOB input{};
    input.pbData = encrypted.data();
    input.cbData = static_cast<DWORD>(encrypted.size());
    DATA_BLOB entropy = credential_entropy_blob();
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return false;
    }
    try {
        const std::string plaintext(
            reinterpret_cast<const char*>(output.pbData), output.cbData);
        const json credentials = json::parse(plaintext);
        if (!credentials.is_object() || !credentials.contains("username") ||
            !credentials.contains("password") ||
            !credentials["username"].is_string() ||
            !credentials["password"].is_string()) {
            throw std::invalid_argument("invalid protected credentials");
        }
        username = credentials["username"].get<std::string>();
        password = credentials["password"].get<std::string>();
        if (username.empty() || password.empty()) {
            throw std::invalid_argument("empty protected credentials");
        }
        LocalFree(output.pbData);
        return true;
    } catch (...) {
        LocalFree(output.pbData);
        return false;
    }
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
        if (root.contains("dashboard_credentials")) {
            const std::string protected_text = required_string(
                root["dashboard_credentials"], "dashboard_credentials");
            if (!unprotect_credentials(protected_text,
                                       candidate.dashboard_username,
                                       candidate.dashboard_password)) {
                throw std::invalid_argument(
                    "dashboard_credentials cannot be decrypted for this Windows user");
            }
            candidate.dashboard_credentials_protected = true;
        } else {
            if (root.contains("dashboard_username")) candidate.dashboard_username = required_string(root["dashboard_username"], "dashboard_username");
            if (root.contains("dashboard_password")) candidate.dashboard_password = required_string(root["dashboard_password"], "dashboard_password");
        }
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

bool save_dashboard_credentials(const std::string& path,
                                const std::string& username,
                                const std::string& password) {
    if (path.empty() || username.empty() || password.empty()) return false;

    try {
        json root = json::object();
        {
            std::ifstream input(path);
            if (input.is_open()) root = json::parse(input);
        }
        if (!root.is_object()) return false;

        std::string protected_text;
        if (!protect_credentials(username, password, protected_text)) {
            return false;
        }
        root.erase("dashboard_username");
        root.erase("dashboard_password");
        root["dashboard_credentials"] = protected_text;

        const std::string temporary_path = path + ".tmp";
        {
            std::ofstream output(temporary_path,
                                 std::ios::binary | std::ios::trunc);
            if (!output.is_open()) return false;
            output << root.dump(4) << '\n';
            if (!output.good()) {
                output.close();
                DeleteFileA(temporary_path.c_str());
                return false;
            }
        }
        if (!MoveFileExA(temporary_path.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileA(temporary_path.c_str());
            return false;
        }
        return true;
    } catch (...) {
        return false;
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

        const std::string temporary_path = path + ".tmp";
        {
            std::ofstream output(temporary_path,
                                 std::ios::binary | std::ios::trunc);
            if (!output.is_open()) return false;
            output << root.dump(4) << '\n';
            if (!output.good()) {
                output.close();
                DeleteFileA(temporary_path.c_str());
                return false;
            }
        }
        if (!MoveFileExA(temporary_path.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileA(temporary_path.c_str());
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
