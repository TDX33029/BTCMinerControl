#include "config.h"
#include "json.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

AppConfig load_config(const std::string& path) {
    AppConfig cfg;

    std::ifstream f(path);
    if (!f.is_open()) {
        std::cout << "[config] No config file found, using defaults. "
                  << "Pool: " << cfg.pool_host << ":" << cfg.pool_port << std::endl;
        return cfg;
    }

    try {
        json j = json::parse(f);

        if (j.contains("pool")) {
            auto& p = j["pool"];
            if (p.contains("host")) cfg.pool_host = p["host"];
            if (p.contains("port")) cfg.pool_port = p["port"];
            if (p.contains("user")) cfg.pool_user = p["user"];
            if (p.contains("pass")) cfg.pool_pass = p["pass"];
        }
        if (j.contains("board_port"))    cfg.board_port     = j["board_port"];
        if (j.contains("dashboard_port")) cfg.dashboard_port = j["dashboard_port"];
        if (j.contains("version_rolling")) cfg.version_rolling = j["version_rolling"];
        if (j.contains("frequency_mhz"))  cfg.default_frequency_mhz = j["frequency_mhz"];
        if (j.contains("voltage_mv"))     cfg.default_voltage_mv    = j["voltage_mv"];
        if (j.contains("min_difficulty")) cfg.min_difficulty        = j["min_difficulty"];

        std::cout << "[config] Loaded: " << path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[config] Parse error: " << e.what() << ", using defaults." << std::endl;
    }

    return cfg;
}
